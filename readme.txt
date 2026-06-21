================================================================================
  STMUSBATMXT640 工程说明 (test-V2.7)
  平台: STM32F103 + maXTouch640 (ATMXT640UD)
  功能: 通过 SPI 抓取触摸芯片硬件调试口数据，经 USB CDC 虚拟串口上传 PC
================================================================================

一、工程概述
--------------------------------------------------------------------------------
本工程基于 STM32CubeMX / Keil MDK 构建，主要模块如下：

  模块              源文件                          说明
  ----------------  ------------------------------  ---------------------------
  SSN 帧解析        Core/Src/gpio.c                 MISO 监视 → 虚拟 SSN 合成
  SPI 接收          Core/Src/spi.c                  SPI1 从机中断接收
                    USB_DEVICE/App/mxt/mxt_spi_stream.c
  USB 传输          USB_DEVICE/App/usbd_cdc_if.c    CDC 虚拟串口
                    USB_DEVICE/App/mxt/mxt_usb_io.c   文本/二进制发送封装
  命令控制          USB_DEVICE/App/mxt/mxt_cmd.c      SPISTART / SPISTOP 等
  I2C 配置          USB_DEVICE/App/mxt/mxt_touch.c    DEBUGCTRL2 使能 SPI 调试输出

主循环 (Core/Src/main.c) 依次执行：
  MXT_ProcessCommand() → MXT_ProcessSPICheck() → MXT_SSN_Poll()
  → MXT_FlushMessageBuffer() → MXT_CheckAndProcessMessages()
  → MXT_TimerDiagnosticRead()

硬件引脚 (Core/Inc/main.h)：
  PA5  SPI1_SCK      SPI 时钟（从机输入）
  PA7  SPI_MOSI      SPI 数据（从机 RX-only 接收）
  PA9  SSN_OUT       虚拟 SSN 输出（推挽，帧间 idle 为高）
  PA10 CLK_MON       MISO 信号监视（双边沿 EXTI）
  PB10/PB11          I2C2（配置 maXTouch T6 DEBUGCTRL2）
  USB FS             PA11/PA12 CDC 虚拟串口


二、SSN 解析方式
--------------------------------------------------------------------------------
2.1 背景
  maXTouch 硬件调试口通过 SPI 输出诊断数据，片选信号 SSN 标识一帧数据的起止。
  本板 SPI 从机采用软件 NSS（SSI=0，始终选中），不依赖 PA4 硬件 SSN。
  因此固件通过监视 PA10（MISO 线）电平变化，在 PA9 合成"虚拟 SSN"，
  供 SPI 数据流做帧边界判定。

2.2 实现位置
  Core/Src/gpio.c  — MXT_SSN_* 系列函数
  Core/Src/stm32f1xx_it.c — TIM1_UP / EXTI15_10 中断入口

2.3 工作原理

  ┌─────────────┐    监视 MISO     ┌──────────────────┐    输出虚拟 SSN
  │  maXTouch   │ ──────────────→ │  PA10 (CLK_MON)  │ ──────────────→ PA9
  │  SPI Master │                  │  EXTI 双边沿      │   (SSN_OUT)
  └─────────────┘                  │  + TIM1 2us 采样  │
                                   └──────────────────┘

  状态机两种状态：
    g_ssn_in_gap = 1  → 帧间（SSN 无效，PA9 输出高电平）
    g_ssn_in_gap = 0  → 帧内（SSN 有效，PA9 输出低电平）

  查询接口：MXT_SSN_IsSelected() 返回 1 表示当前处于帧内（SSN 有效）。

2.4 帧间 → 帧内（EnterActive）条件
  1) 当前处于帧间 (in_gap=1)
  2) 帧间 MISO 连续低电平累计 > SSN_GAP_MIN_US (500us)，置 gap_ready=1
  3) MISO 出现上升沿（低→高）
  4) MISO 高电平持续 > SSN_GAP_HIGH_CONFIRM_US (4us) 后确认进入帧内
  5) PA9 拉低，触发 MXT_SPI_OnSsnActive() 复位首字节标志

2.5 帧内 → 帧间（EndActive）条件（满足任一即退出）
  A) SPI 无活动超时：SPI 接收标志/状态非 busy 且持续 > SSN_SPI_IDLE_US (2500us)
  B) MISO 低电平持续 > SSN_LOW_PULL_US (20us)（MISO 为高时清零计数）

  退出时：PA9 拉高，调用 MXT_SPI_OnSsnGap()
    mode0 → 置 gap_extract_pending，帧间从 DMA ring 提取本帧
    其他  → MXT_SPI_QueueGapMarker() 向 SPI 字节队列插入 GAP 标记

2.6 定时采样
  TIM1 以 2us (SSN_SAMPLE_US) 周期中断，执行 MXT_SSN_Sample() 做上述计时。
  PA10 MISO 边沿触发 EXTI15_10，执行 MXT_SSN_OnMisoEdge() 做快速边沿响应。
  SPI 每收到一次数据调用 MXT_SSN_NotifySpiRx() 清零 no_spi 超时计数。

2.7 关键时序参数（gpio.c 宏定义）
  参数                      值        含义
  ------------------------  --------  ----------------------------------
  SSN_SAMPLE_US             2us       TIM1 采样周期
  SSN_GAP_MIN_US            500us     帧间 MISO 低电平最小时长（确认 gap）
  SSN_GAP_HIGH_CONFIRM_US   4us       MISO 高电平确认进入帧内
  SSN_SPI_IDLE_US           2500us    SPI 无活动判定帧结束
  SSN_LOW_PULL_US           20us      MISO 低电平判定帧结束
  SSN_STOP_PULLUP_US        250us     SPISTOP 延迟后强制拉高 SSN

2.8 SPISTOP 收尾
  发送 SPISTOP 命令后调用 MXT_SSN_StopPullup()：
  等待 SPI 接收队列排空 → 延迟 250us → 强制 EndActive 拉高 PA9
  → 关闭 DEBUGCTRL2 → 停止 TIM1。

2.9 调试命令 SPIDBG
  返回当前 SSN 状态快照：
    gap       是否帧间 (1=帧间)
    ssn       是否选中 (1=帧内)
    no_spi    SPI 无活动累计 us
    low       MISO 低电平累计 us
    enter/exit 帧进入/退出计数
    spi_it/chk/strm  SPI 中断/检查/流使能
    q/tx/ovf/err/stop  队列深度/发送缓冲/溢出/错误/停止标志


三、SPI 读取方式
--------------------------------------------------------------------------------
3.1 硬件配置 (Core/Src/spi.c)
  外设      SPI1
  模式      从机 (SPI_MODE_SLAVE)
  方向      仅接收 (SPI_DIRECTION_2LINES_RXONLY)
  数据位    8 bit，MSB 先发
  时钟      CPOL=0, CPHA=0（Mode 0）
  NSS       软件 NSS (SPI_NSS_SOFT)，CR1.SSI=0（从机始终选中）
  引脚      PA5=SCK, PA7=MOSI（数据输入）

3.2 使能 SPI 调试数据源
  通过 I2C 写 maXTouch T6 对象 Byte6 (DEBUGCTRL2) = 0x80 (DBGOBJMODEEN)：
    MXT_EnableDebugCtrl2()  — 启动 TIM1 SSN 采样
    MXT_DisableDebugCtrl2()   — SPISTOP 时关闭
  由 SPISTART / SPISTART1 / SPISTART3 命令触发。

3.3 接收流程 (mxt_spi_stream.c)

  启动：MXT_SPI_StartIT()
    → HAL_SPI_Receive_DMA() 写入环形缓冲 g_spi_dma_ring[640]（DMA 循环模式）

  DMA 半满/全满回调：HAL_SPI_RxHalfCpltCallback / HAL_SPI_RxCpltCallback
    → MXT_SPI_OnDmaProgress()（mode0 原始流不在 ISR 里逐字节入队）

  SSN 帧内 (PA9 低)：
    → DMA 持续收字节进 ring，记录 g_spi_dma_ssn_pos 为帧起点
    → 主循环 MXT_ProcessSPICheck() 仅做 stall 检测，不发 USB

  SSN 帧间 (PA9 高)：
    → MXT_SPI_OnSsnGap() 置 g_spi_gap_extract_pending
    → MXT_SPI_RawExtractFrameAtGap() 从 ring 拷贝本帧数据到槽缓冲
    → SPIUSB_RawFlushPending() 经 USB CDC 64B 分包发出

  主循环：MXT_ProcessSPICheck() + MXT_USB_ServiceTx()
    USB 发送完成回调 SPIUSB_OnTxComplete() 链式继续 flush

3.4 接收缓冲与队列
  项目                          值/说明
  ----------------------------  ----------------------------------
  DMA 环形 g_spi_dma_ring[]     SPI_DMA_RING_SIZE = 640 字节
  原始帧槽 g_spi_raw_slots[][]  2 槽 × 514B（SPI_RAW_LINE_SLOTS）
  槽长度 g_spi_raw_slot_len[]   每帧实际 SPI 字节数（≤514）
  CDC 发送乒乓 g_spi_cdc_txbuf  2 × 64B（SPI_RAW_TX_CHUNK）
  字节队列 g_spi_rx_queue[]     深度 128（SPISTART1/3 等模式用）
  帧标记 g_spi_rx_mark[]        GAP=1, START=2
  空闲重启                      非流模式 100ms 无活动则 Stop+Start
  文本/SPI1/3 缓冲 g_spi_tx_buf SPI_TX_BUF_SIZE = 1024 字节

3.5 SPI 流模式命令 (mxt_cmd.c)

  命令          mode  输出格式
  ------------  ----  ------------------------------------------------
  SPISTART      0     每 SSN 帧二进制：88 77 66 + LE u16 长度 + payload(≤514B)
  SPISTART1     1     20×16 源矩阵裁剪为 16×16，HEX 文本行输出
  SPISTART3     2     20×16 裁剪为 16×16，打包为 Mode3 二进制包 (AA 10 33)
  SPISTART -no  0     仅 SSN 时序，不读 SPI / 不发 USB
  SPISTOP       -     停止流，关闭 DEBUGCTRL2，收尾 SSN
  SPI           -     切换 SPISTART 原始流开/关
  SPIDBG        -     打印 SSN/SPI 诊断信息

3.6 SPISTART1 / SPISTART3 数据处理

  SPISTART1（640B 裁剪路径）：
    每帧有效载荷 640 字节（20 行 × 40 字节/行，取每行前 33 字节中第 2~33 字节）。
    640 字节收满后自动开始下一帧；32 字节/行 HEX 文本 + 帧号行首标记。

  SPISTART3（514B SSN 间隙提取，与 SPISTART 同路径）：
    byte[0]       帧号 FRAME_ID（本帧 16 行 Mode3 包共用）
    byte[1..512]  512B 有效数据（16 行 × 32B/行，16×16 矩阵）
    byte[513]     帧尾标记（与 byte[0] 成对，不参与数据）

    每 32 字节打包为一个 40 字节 Mode3 包（共 16 包/帧，ROW_ID=0..15）：
    AA 10 33 | LEN(40) | FRAME_ID(=byte[0]) | ROW_ID | DATA[32] | CRC16
    DATA 内每 2 字节高低位交换
    CRC16-CCITT-FALSE，计算范围 packet[0..37]

3.7 SPISTART 原始模式（mode=0）帧边界与 USB 包格式

  帧边界（SSN 驱动，非逐字节队列）：
    SSN EnterActive (PA9↓) → MXT_SPI_OnSsnActive() 记录 DMA 写指针起点
    SSN EndActive   (PA9↑) → MXT_SPI_OnSsnGap() 记录终点，置 gap_extract_pending
    帧间主循环     → MXT_SPI_RawExtractFrameAtGap() 拷贝 [起点,终点) 到槽缓冲
    读到 0 字节    → 不输出；1~514 字节 → 均输出（不强制凑满 514）

  USB 输出格式（小端）：
    偏移  长度  内容
    0     3     88 77 66  帧同步头
    3     2     LE u16    payload 字节数 N（1 ≤ N ≤ 514）
    5     N     SPI 原始数据

  示例：本帧 SPI 100 字节 → USB 发出 105 字节：88 77 66 64 00 [100B data]

  PC 解析：扫描 88 77 66 → 读 2 字节 LE 长度 N → 再读 N 字节 payload

3.8 SPISTART 半流水线（SPI 采集 vs USB 发送）

  层级              机制                          说明
  ----------------  ----------------------------  ---------------------------
  SPI 采集          DMA 循环 ring[640]            硬件持续收，不占 CPU 逐字节
  帧组装            2 槽 g_spi_raw_slots[2][514]  SSN 间隙从 ring 批量提取
  USB 发送          2×64B CDC 乒乓 + TxComplete   流式组帧，每包最多 64B

  时序约束：
    SSN 低（帧内）→ 只收 SPI，主循环 deliberately 不 flush USB
    SSN 高（帧间）→ 提取上一帧入槽 + 排空 USB 队列
  因此 SPI 与 USB 不是同一帧内并行，而是「帧内采集、帧间发送」；
  最多 2 帧在槽中排队（USB 慢则 g_spi_raw_usb_drop++ 丢帧）。

  涉及文件：
    mxt_config.h      SPI_RAW_* 帧头/槽/包长宏
    mxt_spi_stream.c  RawExtract / RawCommit / RawFlushPending
    mxt_cmd.c         SPISTART 命令与 HELP 文案


四、USB 2.0 传输方式
--------------------------------------------------------------------------------
4.1 物理层与协议栈
  MCU         STM32F103（内置 USB Full Speed 设备控制器）
  USB 版本    USB 2.0 Full Speed（12 Mbps）
  设备类      CDC ACM（Communication Device Class，虚拟串口）
  VID/PID     1155 / 22336（usbd_desc.c，可识别为 "STM32 Virtual ComPort"）
  端点        EP0 控制 + EP1 IN/OUT 数据
  最大包长    64 字节 (CDC_DATA_FS_MAX_PACKET_SIZE，USB 2.0 FS 标准上限)

4.2 初始化时序 (main.c)
  1) MX_USB_DEVICE_Init() 初始化 USB 协议栈
  2) USB_EN (PA15) 拉高 100ms 后拉低（硬件 USB 使能时序）
  3) 枚举完成后 CDC_Init_FS() 自动扫描 I2C 并输出欢迎信息

4.3 接收路径（PC → 设备）
  USB OUT 端点 → CDC_Receive_FS()
    mode0 (BRIDGE_MODE_BINARY=0)：ProcessBridgePacket() 二进制桥接
    mode1 (BRIDGE_MODE_STRING=1) ：ProcessStringCommand() 文本命令
  接收缓冲：APP_RX_DATA_SIZE = 256 字节

4.4 发送路径（设备 → PC）— 两条通道

  ┌─────────────────────────────────────────────────────────────────────┐
  │ 通道 A：文本/命令响应                                                │
  │   USB_SendString() / USB_Printf()                                   │
  │     → MSG_BufferWrite() 写入环形缓冲 (2048B)                        │
  │     → MXT_FlushMessageBuffer() → MSG_BufferFlush()                  │
  │     → 每次最多 MSG_FLUSH_CHUNK = 64B                                │
  │     → CDC_Transmit_FS() → USB IN 端点                               │
  │   用途：HELP/INFO/SPIDBG 等文本，SPISTART1 HEX 文本输出             │
  └─────────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────────┐
  │ 通道 B：SPI 二进制流（SPISTART3 等）                                 │
  │   SPI 解析 → g_spi_tx_buf[] (1024B)                                 │
  │     → SPIUSB_TryFlush() → 双缓冲 g_spi_usb_buf[2][64]               │
  │   用途：SPISTART3 Mode3 二进制包 (AA 10 33)                         │
  └─────────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────────┐
  │ 通道 B2：SPISTART 原始二进制流（mode=0）                             │
  │   SSN 间隙 DMA 提取 → g_spi_raw_slots[2][514]                       │
  │     → SPIUSB_RawFlushPending() 流式组帧（88 77 66 + len + data）  │
  │     → 双缓冲 g_spi_cdc_txbuf[2][64] → CDC_Transmit_FS()             │
  │   用途：SPISTART 原始 SPI 帧，PC 须二进制接收                        │
  └─────────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────────┐
  │ 通道 C：阻塞式原始发送                                               │
  │   SendResponse() / USB_SendRaw()                                    │
  │     → 等待 TxState==0 → CDC_Transmit_FS()                           │
  │   用途：mode0 桥接应答、CFGWRITE/CFGREAD 二进制帧                   │
  └─────────────────────────────────────────────────────────────────────┘

4.5 流控与并发规则
  CDC_Transmit_FS() 检查 hcdc->TxState：
    TxState != 0 → 返回 USBD_BUSY，本包不发送（防覆盖）
  文本通道：BUSY 时 MSG_BufferFlush 返回 0，主循环下次重试
  SPI 通道：BUSY 时 SPIUSB_TryFlush 保留 g_spi_tx_buf 待重发
  SPI 发送完成回调 SPIUSB_OnTxComplete() 触发继续 flush
  mode0 二进制模式下 USB_SendString 被静默丢弃，避免干扰二进制流

4.6 主循环 USB 刷新策略
  每轮循环至少调用 2 次 MXT_FlushMessageBuffer()（SPI 处理前后各一次）
  SPI 流模式活跃时主循环 burst 最多 64 轮 MXT_ProcessSPICheck() 降低延迟
  SPISTART 启动前 MXT_WaitUsbIdle(300ms) 等待 USB 发送空闲

4.7 PC 端使用
  1) 连接 USB，等待枚举完成（设备管理器出现 COM 口）
  2) 打开串口终端（115200 或任意波特率，CDC 忽略波特率设置）
  3) 发送文本命令（mode1 默认）：
       SPISTART    开启 SPI 原始二进制流（88 77 66 帧头）
       SPISTART3   开启 SPI Mode3 二进制流
       SPISTOP     停止
       SPIDBG      查看 SSN/SPI 状态
       HELP        完整命令列表
  4) SPISTART / SPISTART3 须用二进制方式接收（勿用纯文本终端看原始数据）
  5) SPISTART1 可直接在终端查看 HEX 文本


五、数据流总览
--------------------------------------------------------------------------------

  maXTouch640                STM32F103                    PC
  ┌──────────┐              ┌──────────────┐           ┌─────────┐
  │ T6       │◄── I2C ─────│ DEBUGCTRL2   │           │         │
  │ DEBUG    │              │ 0x80 使能    │           │  串口   │
  │ SPI Out  │── SCK/MOSI ─►│ SPI1 Slave   │           │  终端   │
  │ (MISO)   │── 监视 ────►│ SSN 解析     │           │         │
  └──────────┘              │ (PA10→PA9)   │           │         │
                            │ 帧内 DMA 收   │── USB CDC►│ COM口   │
                            │ 帧间组包发送  │  64B/包   │(二进制) │
                            └──────────────┘           └─────────┘


六、相关文档
--------------------------------------------------------------------------------
  USB_DEVICE/Mode3_Output_Protocol.md   Mode3 (AA 10 33) 二进制协议详解
  USB_DEVICE/String_Commands.md         字符串命令参考
  ej/doc/                               maXTouch 调试端口官方文档


七、SPISTART 变更记录与问题修复（2026-06）
--------------------------------------------------------------------------------
7.1 变更摘要

  目标：SPISTART (mode=0) 由 HEX 文本行改为 USB 二进制帧，便于 PC 高效解析。

  修改文件：
    USB_DEVICE/App/mxt/mxt_config.h       帧头魔数、长度字段、包长宏
    USB_DEVICE/App/mxt/mxt_spi_stream.c   提取、组帧、CDC 发送逻辑
    USB_DEVICE/App/mxt/mxt_cmd.c          SPISTART 提示信息与 HELP 文案

  协议演进：
    旧：每 SSN 帧一行 HEX 文本（514×3 字符 + CRLF），体积大、需文本解析
    新：88 77 66 + LE u16 长度 + payload，体积约 5+N 字节/帧

7.2 问题：改为二进制后无 USB 输出

  现象：SPISTART 开启后 SPIDBG 显示 SSN 正常，但 PC 收不到数据。

  根因：MXT_SPI_RawExtractFrameAtGap() 仅在拷贝字节数 == 514 时才提交槽位；
        实际每帧 SPI 长度常小于 514，被计入 g_spi_raw_partial_drop 直接丢弃。

  修复：
    1) SSN 间隙结束时，只要 i > 0 即提交（读到多少输出多少，上限 514）
    2) 增加 g_spi_raw_slot_len[] 记录每帧实际长度
    3) USB 发送长度改为 SPI_RAW_FRAME_HDR_LEN(5) + 实际长度，不再固定 517 字节
    4) 提取循环计数 i 由 uint8_t 改为 uint16_t（避免 >255 时溢出）

7.3 问题：PC 无法从流中判断帧长

  需求：帧格式为「88 77 66  长度 | payload」。

  实现：帧头 3 字节固定魔数 + 2 字节小端 u16 表示 payload 长度 N，
        随后 N 字节 SPI 数据（mxt_spi_stream.c SPIUSB_RawEncodedByte）。

  PC 侧伪代码：
    if buf[i:i+3] == [0x88,0x77,0x66]:
        n = buf[i+3] | (buf[i+4] << 8)   # LE u16
        payload = buf[i+5 : i+5+n]
        i += 5 + n

7.4 半流水线架构说明（见 3.8）

  SPI DMA 在 SSN 帧内持续采集；USB 仅在 SSN 帧间发送，避免与采集抢总线。
  2 槽帧队列 + 2×64B CDC 乒乓构成软件流水线；USB 慢于帧率时会丢帧。


================================================================================
  版本: test-V2.7
  生成日期: 2026-06-14
================================================================================
