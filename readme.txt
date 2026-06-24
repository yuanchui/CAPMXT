================================================================================
  STMUSBATMXT640 工程说明（主工程 Core + USB_DEVICE）
  平台: STM32F103 + maXTouch640U (ATMXT640UD)
  功能: SPI 抓取触摸芯片硬件调试口数据，经 USB CDC 上传 PC；支持 mxt-app 桥接
================================================================================

一、工程概述
--------------------------------------------------------------------------------
本工程基于 STM32CubeMX / Keil MDK 构建，应用层集中在 USB_DEVICE/App/mxt/：

  模块              源文件                          说明
  ----------------  ------------------------------  ---------------------------
  虚拟 SSN          Core/Src/gpio.c                 PA10 监视 MISO → PA9 合成帧边界
                    Core/Src/tim.c                  TIM1 帧内保持 / 帧间轮询
  SPI 接收          Core/Src/spi.c                  SPI1 从机 + DMA 环形 RX
                    USB_DEVICE/App/mxt/mxt_spi_stream.c
  USB 传输          USB_DEVICE/App/usbd_cdc_if.c    CDC 虚拟串口入口
                    USB_DEVICE/App/mxt/mxt_usb_io.c 文本/二进制发送封装
  命令控制          USB_DEVICE/App/mxt/mxt_cmd.c    SPISTART / START / HELP 等
  I2C / 触摸        USB_DEVICE/App/mxt/mxt_touch.c  DEBUGCTRL2、诊断帧、Mode3 输出
                    USB_DEVICE/App/mxt/mxt_i2c.c
  mxt-app 桥接      USB_DEVICE/App/mxt/mxt_bridge.c mode0 二进制 / CFG / ENC
  消息 / CHG        USB_DEVICE/App/mxt/mxt_msg.c    CHG 中断消息、定时诊断

  **速率协调要点**：SPI 由 DMA 环缓全速采集，SSN 划分「帧内采集 / 帧间 USB 发送」，
  双槽帧队列 + 64B CDC 乒乓匹配 USB 包率；详见 **§3.9 半流水线**。

上电初始化 (Core/Src/main.c)：
  MX_GPIO_Init → MX_DMA_Init → MX_TIM1_Init → MX_I2C2_Init → MX_SPI1_Init
  → MX_USB_DEVICE_Init → USB_EN 时序 → RST 脉冲 → MXT_SSN_Init() → MXT_SPI_StartIT()

主循环：
  MXT_ProcessCommand() → MXT_ProcessControlPending()
  若 SPI 流 / g_spi_check_requested / SPISTOP 收尾：
    MXT_SSN_Poll() → MXT_ProcessSPICheck() → [流模式] MXT_USB_ServiceTx()
    → MXT_FlushMessageBuffer()
  否则：
    MXT_SSN_Poll() → MXT_ProcessSPICheck() → MXT_FlushMessageBuffer()
    → MXT_CheckAndProcessMessages() → MXT_TimerDiagnosticRead()

硬件引脚 (Core/Inc/main.h)：
  PA3   CHG_EXTI3     CHG 双边沿中断
  PA4   SSN           Cube 标注（SPI 实际用软件 NSS，不依赖 PA4 片选）
  PA5   SPI1_SCK      SPI 时钟（从机输入）
  PA7   SPI_MOSI      SPI 数据（从机 RX-only）
  PA9   SSN_OUT       虚拟 SSN 输出（MXT_SSN_PA9_GPIO_OUT=1 时推挽）
  PA10  CLK_MON       MISO 信号监视（EXTI15_10 双边沿）
  PA11/12             USB FS CDC
  PA15  USB_EN        USB 使能时序
  PB1   RST           触摸芯片复位
  PB10/11 I2C2        maXTouch 配置 / 桥接
  PB12  ADDSEL        地址选择（高 = 0x4B）
  PB13  IICMODE       I2C 模式选择


二、虚拟 SSN 解析（Core/Src/gpio.c）
--------------------------------------------------------------------------------
2.1 背景
  maXTouch 硬件调试口经 SPI 输出诊断数据，片选 SSN 标识帧起止。
  本板 SPI 从机为软件 NSS（CR1.SSI=0，始终选中），不依赖 PA4 硬件片选。
  固件监视 PA10（接芯片 MISO 线）电平，在 PA9 合成虚拟 SSN，供 DMA 帧提取使用。

2.2 两种时序模式
  g_spi_stream_enabled 或 g_spi_check_requested 为 1 时 → **流模式时序**
  否则 → **轮询时序**（帧间 TIM1 100us  tick）

  查询：MXT_SSN_IsSelected() 返回 1 表示帧内（g_ssn_in_gap=0，SSN 有效）。

2.3 流模式（SPISTART / SPISTART1 / SPISTART3）

  帧间：
    PA10 EXTI 监听 MISO 下降沿，DWT 测低电平宽度
    低电平 ≥ SSN_PA10_LOW_MIN_US (1000us) 后 MISO 上升 → MXT_SSN_StartFrameLow()
    PA9 拉低，启动 TIM1 单次定时 SSN_HOLD_LOW_US (6251us)

  帧内：
    TIM1 到期 → MXT_SSN_EndActive() → PA9 拉高 → MXT_SPI_OnSsnGap()
    mode0/2：置 gap_extract_pending，帧间从 DMA ring 提取本帧
    mode1：MXT_SPI_QueueStartMarker()，字节队列标记帧起点

2.4 轮询时序（非 SPI 流调试）

  帧间 → 帧内：MISO 低 ≥ SSN_GAP_MIN_US (500us) 后置 gap_ready；
            随后 MISO 上升沿 → MXT_SSN_EnterActive()

  帧内 → 帧间（满足任一）：
    A) SPI 无活动 > SSN_SPI_IDLE_US (2500us)
    B) MISO 低电平持续 > SSN_LOW_PULL_US (20us)

2.5 编译选项 MXT_SSN_PA9_GPIO_OUT (main.h)
  0：仅更新 g_ssn_in_gap 等软件状态，PA9 高阻输入
  1（默认）：PA9 推挽，帧间高 / 帧内低（逻辑分析仪可抓波形）

2.6 关键时序参数 (main.h)
  参数                      值        含义
  ------------------------  --------  ----------------------------------
  SSN_HOLD_LOW_US           6251us    流模式帧内 active 保持时间
  SSN_GAP_POLL_US           100us     轮询模式 TIM1 周期
  SSN_GAP_MIN_US            500us     轮询模式帧间 MISO 低最小时长
  SSN_SPI_IDLE_US           2500us    轮询模式 SPI 空闲判帧结束
  SSN_LOW_PULL_US           20us      轮询模式 MISO 低判帧结束
  SSN_STOP_PULLUP_US        250us     SPISTOP 队列排空后再拉高
  SSN_PA10_LOW_MIN_US       1000us    流模式 PA10 低脉宽下限

2.7 SPISTOP 收尾
  g_spi_stream_enabled=0 → MXT_SSN_StopPullup() 等待 RX 队列排空
  → 延迟 SSN_STOP_PULLUP_US → MXT_SSN_EndActive()
  → MXT_DisableDebugCtrl2() → MXT_SSN_TimStop()

2.8 SPIDBG 诊断字段
  gap/ssn/no_spi/low/enter/exit — SSN 状态与计数
  spi_it/chk/strm — SPI DMA / 检查请求 / 流使能
  q/tx/ovf/err/stop — 字节队列深度 / TX 长度 / 溢出 / 错误 / 停止标志
  usb_pend/usb_drop/part_drop — 原始帧槽待发送 / USB 丢帧 / 不完整帧丢弃


三、SPI 读取方式
--------------------------------------------------------------------------------
3.1 硬件配置 (Core/Src/spi.c)
  外设      SPI1
  模式      从机，仅接收 (SPI_DIRECTION_2LINES_RXONLY)
  数据位    8 bit，MSB 先发，Mode 0
  NSS       软件 NSS，SSI=0（从机始终选中）
  DMA       DMA1_Channel2 循环模式 → g_spi_dma_ring[]
  引脚      PA5=SCK, PA7=MOSI

3.2 使能 SPI 调试数据源
  I2C 写 T6 对象 Byte6 (DEBUGCTRL2) = 0x80 (DBGOBJMODEEN)：
    MXT_EnableDebugCtrl2() / MXT_EnableDebugCtrl2Quiet()
    MXT_DisableDebugCtrl2() — SPISTOP 时关闭
  由 SPISTART / SPISTART1 / SPISTART3 命令触发；上电不自动开启。

3.3 接收流程 (mxt_spi_stream.c)

  启动：MXT_SPI_StartIT() → HAL_SPI_Receive_DMA(ring, SPI_DMA_RING_SIZE)

  DMA 半满/全满：HAL_SPI_RxHalfCpltCallback / RxCpltCallback
    → MXT_SPI_OnDmaProgress() 将字节入队（mode1）或仅更新时间戳（mode0/2）

  SSN 帧内 (PA9 低 / IsSelected=1)：
    DMA 持续写入 ring，记录 g_spi_dma_ssn_pos
    主循环不发 USB（mode0 时 MXT_USB_ServiceTx 直接 return）

  SSN 帧间：
    MXT_SPI_OnSsnGap() → gap_extract_pending
    mode0：MXT_SPI_RawExtractFrameAtGap() → 槽缓冲 → SPIUSB_RawFlushPending()
    mode2：MXT_SPI_Start3ExtractFrameAtGap() → 514B → Mode3 包
    mode1：经字节队列 + START/GAP 标记逐字节裁剪

  停滞检测：SPI_GAP_IDLE_STALL_MS=500ms（流模式帧间空闲不重启 DMA）

3.4 缓冲与队列 (mxt_config.h / mxt_state.h)
  项目                          值/说明
  ----------------------------  ----------------------------------
  g_spi_dma_ring[]              SPI_DMA_RING_SIZE = 1024 字节
  g_spi_raw_slots[][]           2 槽 × 514B（SPISTART raw）
  g_spi_rx_queue[]              深度 128（SPISTART1 字节路径）
  g_spi_tx_buf / g_msg_buffer     共用联合体 2048B（互斥使用）
  SPI_USB_PKT_SIZE              64B CDC 分包
  g_spi_usb_buf[2][64]          SPISTART1/3 二进制 TX 乒乓

3.5 SPI 流模式命令 (mxt_cmd.c)

  命令            mode  输出格式
  --------------  ----  ------------------------------------------------
  SPISTART        0     每 SSN 帧：88 77 66 + LE u16 长度 + payload(≤514B)
  SPISTART -no    -     仅 SSN 时序，不读 SPI / 不发 USB
  SPISTART1       1     20×16 裁剪 16×16，HEX 文本行（640B/帧）
  SPISTART3       2     SSN 间隙提取 514B → Mode3 二进制包 (AA 10 33)
  SPISTOP         -     停止流，关闭 DEBUGCTRL2，SSN 收尾
  SPI             -     切换 SPISTART 原始流开/关
  SPIDBG          -     SSN/SPI/USB 诊断快照

3.6 SPISTART1（mode=1，640B 裁剪）

  SSN 进入帧内时 MXT_SPI_QueueStartMarker()，从 DMA  drain 的字节经
  SPIUSB_Start1_ProcessPayloadByte() 处理：
    每行 40B 源数据：跳过行首 1B（常见 0x80），取后续 32B
    640B 收满后自动开始下一帧（SPIUSB_Start1_HandlePageMarker）
  输出：每行前打印帧号 HEX 字节 + 32 字节 HEX 文本 + CRLF

3.7 SPISTART3（mode=2，514B SSN 间隙提取）

  与 SPISTART 共用 DMA + SSN 间隙提取路径，整帧结构：
    byte[0]       帧号 FRAME_ID
    byte[1..512]  512B（16 行 × 32B）
    byte[513]     帧尾标记

  每 32 字节打包为 40 字节 Mode3 包（16 包/帧）：
    AA 10 33 | LEN(40) | FRAME_ID | ROW_ID(0..15) | DATA[32] | CRC16
    DATA 内 16bit 高低字节交换；CRC16-CCITT-FALSE，大端附在帧尾

3.8 SPISTART 原始模式（mode=0）

  帧边界：SSN EnterActive 记 DMA 起点 → EndActive 记终点 → 间隙提取
  读到 0 字节不输出；1~514 字节均输出（不强制凑满 514）

  USB 包格式（小端）：
    88 77 66 | LE u16 N | N 字节 SPI 数据

  详见 **3.9 半流水线：SRAM / DMA / USB 速率协调**。


3.9 半流水线：SRAM / DMA / USB 速率协调（核心设计）
--------------------------------------------------------------------------------
3.9.1 速率矛盾

  触摸芯片 SPI 调试口在帧内以主时钟突发输字节（可达数百 kB/s 量级），
  而 USB FS CDC 每包仅 64B、且受主循环调度与 TxState 限制（约 1ms 级）。
  STM32F103 仅 20KB SRAM，无法在单片缓冲里「整帧 SPI + 整帧 USB」同时展开。

  对策：**时间分工（SSN 帧内/帧间）+ 多级 SRAM 缓冲 + 有界预算 drain + 背压丢帧**，
  让 DMA 全速收、CPU 仅在帧间组包发 USB，避免采集与发送抢同一段内存与总线。

3.9.2 流水线层级（自底向上）

  ┌─────────────────────────────────────────────────────────────────────────┐
  │ L0 硬件采集（最高速率，零 CPU 逐字节）                                    │
  │   SPI1 Slave RX ──DMA1 Ch2 循环──► g_spi_dma_ring[1024]                 │
  │   帧内：DMA 持续写 ring；g_spi_dma_ssn_pos 记录本帧起点                   │
  │   约束：ring ≥ 2×单帧上限（1024 ≥ 2×514），防环绕覆盖未提取数据          │
  └─────────────────────────────────────────────────────────────────────────┘
                                    │ SSN 帧结束（PA9↑）
                                    ▼
  ┌─────────────────────────────────────────────────────────────────────────┐
  │ L1 帧提取（帧间批量，一次 memcpy 量级）                                   │
  │   mode0：ring[start..wr) ──► g_spi_raw_slots[2][514] + slot_len[]       │
  │   mode2：ring ──► g_spi_start3_frame_buf[514] ──► Mode3 包入 TX 缓冲     │
  │   mode1：帧间 MXT_SPI_DrainDmaRingBudget 逐字节 ──► g_spi_rx_queue[128]   │
  │   触发：MXT_SPI_OnSsnGap() 快照起点/写指针 → MXT_SPI_ProcessGapExtract() │
  └─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
  ┌─────────────────────────────────────────────────────────────────────────┐
  │ L2 帧队列 / 组包（解耦 SPI 帧率与 USB 包率）                              │
  │   mode0：双槽 FIFO（r/w 指针），最多 2 帧待发送                           │
  │   mode1/2：g_spi_tx_buf[2048] 累积 HEX 或 AA10 33 二进制                  │
  │   背压：槽满 → g_spi_raw_usb_drop++；队列满 → g_spi_rx_overflow++        │
  └─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
  ┌─────────────────────────────────────────────────────────────────────────┐
  │ L3 USB 发包（匹配 CDC 64B 包率）                                          │
  │   SPIUSB_RawFlushPending() / SPIUSB_TryFlush()                           │
  │   双乒乓：g_spi_cdc_txbuf[2][64] 或 g_spi_usb_buf[2][64]                 │
  │   TxState==0 才 CDC_Transmit_FS；BUSY 则保留缓冲待下轮                   │
  │   MXT_USB_ServiceTx() 每主循环最多 flush 32 次（防卡死）                  │
  └─────────────────────────────────────────────────────────────────────────┘

3.9.3 时间分工：帧内只收、帧间才发（速率协调的关键）

  阶段          SSN 状态    DMA           CPU 主循环              USB
  ------------  ----------  ------------  --------------------  -----------
  帧内(active)  PA9 低      全速写 ring   mode0/2：仅 stall 检测  **不发**
                                          （MXT_USB_ServiceTx 若
                                           IsSelected 则 return）
  帧间(gap)     PA9 高      仍循环写 ring 提取上帧 → 槽/TX 缓冲   64B 分包发送
                                          drain 预算处理 mode1

  这样 SPI 线速由 DMA 承担，USB 低速由帧间窗口消化，避免「边收边发」导致
  g_spi_dma_last_pos 与 USB 组包竞争同一 ring 区域。

3.9.4 有界预算（防止主循环饿死 / 单次占用过长）

  宏 / 常量                    值      作用
  ---------------------------  ------  ----------------------------------
  SPI_DRAIN_BUDGET_ISR         16      DMA 半满/全满回调内最多 drain 字节
  SPI_DRAIN_BUDGET_LOOP        96      主循环 MXT_ProcessSPICheck 每轮 drain
  mode1 字节处理 budget        64      从 g_spi_rx_queue 消费上限/轮
  MXT_USB_ServiceTx flush      32      每轮最多连续 CDC 发送次数
  MSG_FLUSH_CHUNK              64      文本通道与 USB MPS 对齐

  mode0/2 在帧内不走 drain 入队路径，而是帧间 RawDiscardGapDrain 推进读指针，
  丢弃帧间无效间隙字节，仅保留 SSN 快照区间。

3.9.5 SRAM 分区（SPI 路径约 4~5KB，与文本互斥）

  缓冲区                      大小        说明
  --------------------------  ----------  ----------------------------------
  g_spi_dma_ring              1024 B      DMA 循环区，常驻
  g_spi_raw_slots             2×514 B     mode0 帧 FIFO
  g_spi_start3_frame_buf      514 B       mode2 单帧提取暂存
  g_spi_rx_queue + mark       128 B       mode1 字节队列
  g_usb_stream_buf (union)    2048 B      g_spi_tx_buf **或** g_msg_buffer
  g_spi_cdc_txbuf / usb_buf   2×64 B      CDC 发包乒乓，避免异步覆盖
  g_msg_tx_chunk              64 B        文本 flush 暂存

  **互斥规则**：SPI 流活跃时 g_spi_tx_buf 与 g_msg_buffer 共用同一块 2048B；
  mode0 禁止 USB_SendString，避免文本写入污染二进制流。

3.9.6 背压与可观测性

  症状                          计数器 / 条件              含义
  ----------------------------  -------------------------  ---------------------
  USB 慢于帧率，双槽已满          g_spi_raw_usb_drop         丢弃新提取帧
  mode1 队列满                  g_spi_rx_overflow          DMA→队列来不及
  mode2 间隙数据 < 514B         g_spi_raw_partial_drop     不完整帧丢弃
  TX 缓冲满且 USB 一直 BUSY       g_spi_tx_len 停涨          下轮 TryFlush 重试
  SPI 线长时间无字节              SPI_STREAM_STALL_MS 25ms   帧内异常，标记 resync
  帧间正常空闲                    SPI_GAP_IDLE_STALL_MS 500ms  **不**重启 DMA

  SPIDBG 命令可一次查看 gap/ssn/q/tx/usb_pend/usb_drop/part_drop 等字段。

3.9.7 典型一帧时序（mode0 SPISTART）

  1) PA10 低脉宽 ≥1ms → SSN 进帧 → 记 dma_ssn_pos
  2) ~6.25ms 帧内：DMA 将 SPI 字节写入 ring（CPU 几乎不参与）
  3) TIM1 到期 → SSN 出帧 → OnSsnGap 快照 wr → gap_extract_pending
  4) 主循环帧间：RawExtractFrameAtGap → 槽[514] → RawFlushPending
  5) 按 88 77 66 + len + data 流式组包，每 64B CDC 一包，直至本帧发完
  6) 若步骤 4 时槽满而 USB 仍在发上一帧 → usb_drop，本帧丢弃

  **设计取舍**：宁可丢帧也不扩无限队列（SRAM 不够）；ring=1024 + 双槽=2 帧
  是在 20KB SRAM 下可 sustained 运行的平衡点。提高可靠性可增大
  SPI_DMA_RING_SIZE / SPI_RAW_LINE_SLOTS（需重新评估 ZI-data）。


四、USB 2.0 传输
--------------------------------------------------------------------------------
4.1 物理与协议
  USB 2.0 Full Speed CDC ACM 虚拟串口
  端点最大包长 64 字节；接收缓冲 APP_RX_DATA_SIZE = 256

4.2 接收路径（PC → 设备）
  CDC_Receive_FS()：
    mode0 (BRIDGE_MODE_BINARY)：ProcessBridgePacket() 二进制桥接
    mode1 下单字节 0xE0 / 0x82：仍走 ProcessBridgePacket()（mxt-app 兼容）
    其余：ProcessStringCommand() 文本命令

4.3 发送路径（设备 → PC）

  通道 A — 文本响应
    USB_SendString() → MSG_BufferWrite() → MSG_BufferFlush()（≤64B/次）
    缓冲 g_msg_buffer[1024]（与 SPI TX 共用 2048B 联合体，互斥）

  通道 B — SPISTART 原始二进制
    槽缓冲 → SPIUSB_RawFlushPending() → g_spi_cdc_txbuf[2][64]

  通道 C — SPISTART1 HEX / SPISTART3 Mode3
    g_spi_tx_buf[2048] → SPIUSB_TryFlush() → g_spi_usb_buf[2][64]

  通道 D — 阻塞应答
    SendResponse()：等待 TxState 空闲后 CDC_Transmit_FS()
    用于 mode0 桥接、CFGWRITE、SPISTART 启动确认等

4.4 流控
  CDC_Transmit_FS()：TxState!=0 返回 USBD_BUSY
  mode0 下 USB_SendString 静默丢弃，避免污染二进制流
  SPI 发送完成可链式调用 MXT_USB_ServiceTx() / SPIUSB_OnTxComplete()

4.5 I2C 诊断路径（非 SPI 流时）
  START [MAP16*] [HEX|CHAR] interval_ms — I2C 读诊断帧
  START1 — FRAME1 + 每次采集前 CAL
  START CHGNO [X|Y|XY] [MAP16*] ms — Mode3 包附带触点信息
  MXT_SendMode3Packets() 经 SendResponse() 逐行发送 AA 10 33 包


五、mxt-app 桥接与其它协议
--------------------------------------------------------------------------------
  mode0 / BRIDGEBIN：0xE0 找地址、0x82 读脚、0x01 0x51 I2C 读写、0x80 配置
  CFGWRITE (D0/D1/D2)：xcfg 分段写入，CFG_MAX_OBJECTS=128
  ENCWRITE (B0/B1/B2)：Bootloader .enc 流式烧录
  字符串：INFO、FINDIIC、mode1/mode0、HELP 等（见 ej/doc/String_Commands.md）


六、数据流总览（含流水线分工）

  maXTouch640U             STM32F103                         PC
  ┌──────────┐              ┌────────────────────────┐      ┌─────────┐
  │ T6       │◄── I2C ─────│ DEBUGCTRL2 0x80        │      │         │
  │ SPI Out  │── SCK/MOSI ─►│ L0: SPI1+DMA ring 1KB  │      │  COM    │
  │ (MISO)   │── PA10 ────►│     帧内全速写          │      │         │
  └──────────┘              │ L1: SSN 帧间提取→双槽   │      │         │
                            │ L2: 组包 88..66/AA1033  │─USB─►│ 64B/包  │
                            │ L3: CDC 乒乓发送        │ CDC  │         │
                            └────────────────────────┘      └─────────┘

  帧内：DMA 独占 ring，USB 静默 | 帧间：CPU 提取+发送，消化速率差


七、相关文档
--------------------------------------------------------------------------------
  ej/doc/Mode3_Output_Protocol.md       Mode3 (AA 10 33) 协议
  ej/doc/String_Commands.md             字符串命令参考
  ej/doc/STMUSBATMXT640_技术文档.md     综合技术说明
  ej/test-V1.7/                         IT+NSS 变体固件（640U 三次硬件 SSN）


八、变更记录
--------------------------------------------------------------------------------
8.1 SPISTART 二进制帧（2026-06）
  由 HEX 文本改为 88 77 66 + LE u16 + payload；支持 1~514B 变长帧输出。

8.2 虚拟 SSN 流模式（2026-06）
  SPI 流启用 PA10 EXTI + DWT + TIM1 单次保持（6251us），替代纯 2us 轮询进帧。
  轮询时序保留用于非流模式调试。

8.3 应用层模块化
  usbd_cdc_if.c 瘦身为 CDC 入口；逻辑迁至 USB_DEVICE/App/mxt/*。

8.4 SPISTART1 / SPISTART3 路径区分
  START1：640B 字节队列裁剪 + HEX 文本（SSN 帧内 drain）
  START3：514B SSN 间隙 DMA 提取 + Mode3 二进制（与 SPISTART 同提取路径）

8.5 半流水线文档化（2026-06-22）
  readme §3.9 补充 SRAM 分区、DMA ring 约束、帧内/帧间时间分工、
  drain 预算与背压计数器，说明 20KB SRAM 下的速率协调取舍。


================================================================================
  版本: 主工程 Core + USB_DEVICE
  更新日期: 2026-06-22
================================================================================
