# STMUSBMXT 工程技术文档

> **版本**：test-V1.7  
> **日期**：2026-06-24  
> **适用芯片**：Microchip **ATMXT640U**、**ATMXT641**（及同族 640 节点 maXTouch U/T 系列）  
> **固件路径**：`ej/test-V1.7/`  
> **参考协议**：QTAN0050 — *Using the maXTouch Debug Port*（`ej/doc/Level1_QTAN0050_*.md`）  
> **上位机**：Serial Terminal（`ej/serial-app`）

---

## 目录

1. [项目概述](#1-项目概述)
2. [目标芯片与工程选型](#2-目标芯片与工程选型)
3. [系统架构](#3-系统架构)
4. [硬件设计](#4-硬件设计)
5. [SPI 调试口与帧格式](#5-spi-调试口与帧格式)
6. [MCU 固件架构](#6-mcu-固件架构)
7. [通信协议](#7-通信协议)
8. [PC 端软件](#8-pc-端软件)
9. [典型工作流程](#9-典型工作流程)
10. [开发与构建](#10-开发与构建)
11. [故障排查](#11-故障排查)
12. [附录](#12-附录)

---

## 1. 项目概述

### 1.1 工程定位

**STMUSBMXT** 是一套面向 Microchip maXTouch **640 节点级**电容触摸控制器的 **USB 调试桥接方案**，本版固件（**test-V1.7**）专门适配：

- **ATMXT640U**（U 系列，硬件 SPI Debug 口带 SSN）
- **ATMXT641** / **mXT641T**（640 通道 Touch IC，SPI 调试口时序与 640U 同类）

核心能力：

- 经 **I2C** 配置触摸芯片、读取对象表与 T37 诊断数据；
- 经 **SPI 从机 + 硬件 NSS** 抓取芯片 Debug 口高速原始数据；
- 经 **USB CDC 虚拟串口** 上传至 PC，供调参、产测与矩阵可视化。

与仓库 **主工程**（`Core/` + `USB_DEVICE/`，面向 **ATMXT640UD**）的区别见 [§2.2](#22-与主工程640ud-的差异)。

### 1.2 双通道通信模型

| 通道 | 物理接口 | 方向 | 用途 |
|------|----------|------|------|
| 配置/诊断 | I2C2 | MCU ↔ 触摸芯片 | 对象表、T6 命令、T37 诊断、CFG 烧录 |
| 高速原始流 | SPI1（从机，硬件 NSS） | 触摸芯片 → MCU | DEBUGCTRL 使能后的 SPI 调试数据 |
| 主机交互 | USB CDC | MCU ↔ PC | 文本命令、Mode3 二进制、CFG 协议帧 |
| 事件通知 | CHG (PA3) | 触摸芯片 → MCU | 新消息时读 T5 |

![总体数据流](images/arch_dataflow.png)

*图 1-1：PC ↔ STM32 桥接板 ↔ maXTouch 总体数据流*

---

## 2. 目标芯片与工程选型

### 2.1 芯片对照

| 项目 | ATMXT640U | ATMXT641 / mXT641T |
|------|-----------|---------------------|
| 系列 | maXTouch **U** 系列 | maXTouch **T** 系列（640 节点） |
| 节点规模 | 640 级（典型矩阵 **32×20**，以 INFO 为准） | **640 通道**（矩阵尺寸以 INFO 为准） |
| Family ID | 通常 **`0xA6`** | 以 Information Block 读回为准 |
| Variant ID | 常见 **`0x01`** / **`0x02`** | 以 Information Block 读回为准 |
| Debug 使能 | T6 **DEBUGCTRL** Byte4（本固件写 **`0x20` SIGNAL） | 同左（U/T 系列均支持 DEBUGCTRL） |
| SPI Debug SSN | **硬件片选**（本固件必需） | **硬件片选**（本固件适配） |
| 每扫描帧 SPI 载荷 | **640 B**（三次 SSN，见 §5） | 与 640U 同类时序 |

> **重要**：对象地址、矩阵 X/Y、Variant 均须通过 `INFO` 或 Information Block 动态读取，**禁止**在固件或上位机中写死。

### 2.2 与主工程（640UD）的差异

| 对比项 | test-V1.7（640U/641） | 主工程（640UD） |
|--------|----------------------|-----------------|
| 工程路径 | `ej/test-V1.7/` | 仓库根 `Core/` + `USB_DEVICE/` |
| 目标芯片 | ATMXT640U、ATMXT641 | ATMXT640UD-CCUBHA1 等 |
| SPI 片选 | **PA4 硬件 NSS** | PA10 监视 MISO → **PA9 虚拟 SSN** |
| SPI 接收 | **中断 IT 1 字节/次** | DMA 环形缓冲 |
| 调试寄存器 | **DEBUGCTRL** Byte4 | **DEBUGCTRL2** Byte6（DBGOBJMODEEN） |
| 稳定 SPI 帧 | **640 B** | **514 B**（Mode3 展开路径） |
| ENC 固件烧录 | 未启用 | 支持 ENCWRITE |
| 详细文档 | 本文档 | `ej/doc/STMUSBATMXT640_技术文档.md` |

**选型规则**：

- 原理图已将触摸芯片 **Debug SSN 接至 STM32 PA4** → 使用 **test-V1.7** 本固件；
- 640UD 板卡无硬件 SSN、或需 514 B DBGOBJMODEEN 帧 → 使用 **主工程** 固件。

---

## 3. 系统架构

### 3.1 数据流

1. **PC**：Serial Terminal 经 USB CDC 发送文本命令或接收 Mode3 / HEX 流。
2. **STM32F103**：解析命令；I2C 写 T6 DEBUGCTRL；SPI IT 逐字节入队；主循环按 NSS 边沿组帧并经 USB 发送。
3. **触摸芯片**：I2C 接受配置；SPI Master 输出 Debug 数据；CHG 通知消息就绪。

### 3.2 工程目录

![工程目录结构](images/project_structure.png)

*图 3-1：test-V1.7 主要目录*

```text
ej/test-V1.7/
├── Core/                 # main, gpio, i2c, spi
├── USB_DEVICE/App/mxt/   # 应用层
├── MDK-ARM/              # Keil 工程与 hex 输出
└── README.md             # 快速上手
```

---

## 4. 硬件设计

### 4.1 MCU

| 参数 | 值 |
|------|-----|
| 型号 | STM32F103C8（Cortex-M3，72 MHz） |
| Flash / RAM | 64 KB / 20 KB |
| USB | Full Speed Device（CDC ACM） |
| IDE | Keil MDK-ARM |

### 4.2 引脚分配

定义见 `Core/Inc/main.h`：

| 引脚 | 信号 | 方向 | 功能 |
|------|------|------|------|
| PA3 | CHG | 输入/EXTI | 消息就绪中断 |
| **PA4** | **SSN / SPI1_NSS** | 输入 | **硬件片选，帧边界** |
| PA5 | SPI1_SCK | 输入 | SPI 时钟（芯片 Master） |
| PA7 | SPI_MOSI | 输入 | SPI 数据（从机 RX-only） |
| PA11/12 | USB DM/DP | — | USB FS |
| PA15 | USB_EN | 输出 | USB 收发器使能时序 |
| PB1 | RST | 输出 | 芯片复位（低 40 ms） |
| PB10/11 | I2C2 | 开漏 | 配置/诊断 |
| PB12 | ADDSEL | — | I2C 地址（高 = 0x4B） |
| PC13 | LED | 输出 | 状态指示 |

### 4.3 I2C 地址

`USB_DEVICE/App/mxt/mxt_config.h`：

| 模式 | 地址 |
|------|------|
| Application Low | `0x4A` |
| Application High | `0x4B` |
| Bootloader Low/High/Alt | `0x24` / `0x25` / `0x26` |
| Bootloader mXT640 | `0x27` |

### 4.4 SPI 配置

| 参数 | 值 |
|------|-----|
| 外设 | SPI1 |
| 模式 | **从机**，RX-only |
| CPOL/CPHA | Mode 0 |
| NSS | **硬件 NSS 输入**（`SPI_NSS_HARD_INPUT`，PA4） |
| 接收方式 | `HAL_SPI_Receive_IT`，**1 字节/中断** |

### 4.5 上电时序

`Core/Src/main.c`：

1. `HAL_Init()` → `SystemClock_Config()`（72 MHz）
2. `MX_GPIO_Init()` → `MX_I2C2_Init()` → `MX_SPI1_Init()` → `MX_USB_DEVICE_Init()`
3. USB_EN：高 → 延时 100 ms → 低
4. RST：低 40 ms → 高
5. 主循环处理 USB 命令与 SPI 流

---

## 5. SPI 调试口与帧格式

> 参考 QTAN0050 Debug Port Application Note。

### 5.1 使能调试数据源

本固件通过 I2C 写 T6 对象 **Byte 4（DEBUGCTRL）**：

```c
/* mxt_config.h */
#define MXT_T6_DEBUGCTRL_OFFSET  4U
#define MXT_STARTUP_DEBUGCTRL      0x20U   /* SIGNAL */
```

- `SPISTART` / `SPISTART1` / `SPISTART3` 前调用 `MXT_ApplyStartupDebugCtrl()`
- `SPISTOP` 时写 `0x00` 关闭

> **注意**：本固件**不使用** DEBUGCTRL2（Byte6 DBGOBJMODEEN）。640UD 主工程使用 DEBUGCTRL2 且 SPI 帧为 514 B，二者**不可混用**统计口径。

### 5.2 硬件 NSS 帧边界（640U / 641）

实现：`USB_DEVICE/App/mxt/mxt_spi_stream.c`

- 每个 SPI 接收中断采样 **PA4（NSS）** 电平，与数据字节一并入队
- 触摸芯片每扫描周期输出 **三次 SSN 脉冲**
- 固件在 **第一次 NSS 下降沿**（`nss_prev=1 → nss=0`）开始收集本帧
- 收集满 **`SPI_FRAME_PAYLOAD_BYTES`（640 B）** 后结束本帧处理

![SPI 接收流水线](images/spi_pipeline.png)

*图 5-1：IT 逐字节入队 → 主循环按 NSS 组帧 → USB 发送*

### 5.3 640 B 载荷布局

| 层级 | 说明 |
|------|------|
| 芯片 SPI 输出 | 20 行 × **40 B/行** |
| 行内格式 | 第 1 字节常为标记（如 `0x80`），**第 2–33 字节**为 16 点 × 2 B |
| 固件裁剪 | 取每行 32 B × 16 行 → **16×16** 显示矩阵 |
| 整帧 | **640 B** 有效载荷 / 扫描周期 |

### 5.4 SPI 流模式对比

| 命令 | mode | USB 输出 | 说明 |
|------|------|----------|------|
| **SPISTART** | 0 | HEX 文本 | 原始字节 `XX ` 格式 |
| **SPISTART1** | 1 | HEX 文本 + 帧号 | 16×16，行末 `\r\n` |
| **SPISTART3** | 2 | **Mode3 二进制** | 16 包 × 40 B = **640 B/帧** |
| **SPISTOP** | — | — | 停止并关闭 DEBUGCTRL |

#### SPISTART3 Mode3 行包（40 B）

![Mode3 行包](images/mode3_packet.png)

```text
AA 10 33 | LEN(40) | FRAME_ID | ROW_ID(0~15) | DATA[32] | CRC16
```

- DATA：16 个 int16，**字节对交换**（与 I2C T37 路径一致）
- CRC16-CCITT-FALSE，范围 `packet[0..37]`

**稳定态 USB 吞吐**（设扫描频率 f Hz）：

```text
USB ≈ f × 640 B/s    （SPISTART3）
```

### 5.5 缓冲与背压

`mxt_config.h`（**640U 实测值，勿随意减小**）：

| 宏 | 值 | 用途 |
|----|-----|------|
| `SPI_RX_QUEUE_DEPTH` | 2048 | IT 字节队列深度 |
| `SPI_HEX_TX_BUF_SIZE` | 4096 | USB 发送缓冲 |
| `SPI_FRAME_PAYLOAD_BYTES` | 640 | 每帧载荷 |
| `SPI_STREAM_STALL_MS` | 100 | SPI 无中断超时重启 |

队列满时 `g_spi_rx_overflow++`；USB 发送不及 `g_spi_tx_drop++`。`SPIDBG` 可查看计数。

---

## 6. MCU 固件架构

### 6.1 主循环

`Core/Src/main.c`：

```c
while (1) {
    MXT_ProcessCommand();
    MXT_ProcessControlPending();

    MXT_ProcessSPICheck();
    if (g_spi_stream_enabled != 0U) {
        MXT_USB_ServiceTx();          /* SPI 流期间优先 USB TX */
    } else {
        MXT_FlushMessageBuffer();
        MXT_CheckAndProcessMessages();
        MXT_FlushMessageBuffer();
        MXT_TimerDiagnosticRead();
        MXT_FlushMessageBuffer();
    }
}
```

### 6.2 模块划分

| 模块 | 源文件 | 职责 |
|------|--------|------|
| 状态 | `mxt_state.c/h` | 全局变量、SPI 队列、对象地址 |
| I2C | `mxt_i2c.c/h` | 寄存器读写、设备探测 |
| 触摸 | `mxt_touch.c/h` | Info Block、T37、DEBUGCTRL |
| 命令 | `mxt_cmd.c/h` | 文本命令分发 |
| SPI 流 | `mxt_spi_stream.c/h` | IT 接收、NSS 组帧、Mode3 |
| USB I/O | `mxt_usb_io.c/h` | 缓冲与发送 |
| 消息 | `mxt_msg.c/h` | CHG、T5、T100 坐标 |
| 桥接 | `mxt_bridge.c/h` | MODE0、CFGWRITE/CFGREAD |
| 配置 | `mxt_config.h` | 地址、缓冲、协议常量 |

### 6.3 工作模式

| 模式 | 命令 | 说明 |
|------|------|------|
| MODE1 文本 | 默认 | HELP、SPISTART、START 等 |
| MODE0 二进制 | `MODE0` / `BRIDGEBIN` | I2C 透传、CFG 协议 |

切回 MODE1：发送 `MODE1` 或固定序列 `02 01 10 20`。

### 6.4 内存预算（约）

| 缓冲 | 大小 |
|------|------|
| `g_spi_rx_queue` | 2048 × 1 B + NSS |
| `g_spi_hex_tx_buf` | 4096 B |
| `g_msg_buffer` | 1024 B |
| `g_cfg_rx_buf` | ~528 B |

合计占用大部分 20 KB SRAM，修改缓冲须重新评估链接 map。

---

## 7. 通信协议

### 7.1 文本命令（MODE1）

完整列表：`ej/doc/String_Commands.md` 或 `HELP`。

**常用命令**：

| 类别 | 命令 |
|------|------|
| 状态 | `INFO`、`FINDIIC`、`STATUS`、`OBJTBL` |
| I2C 诊断 | `FRAME0`~`FRAME5`、`START MAP16*`、`STOP` |
| SPI 流 | `SPISTART`、`SPISTART1`、`SPISTART3`、`SPISTOP`、`SPIDBG` |
| 模式 | `MODE0`、`MODE1` |
| 配置 | `EXPORTTXT`、`EXPORTBIN`（MODE0 下 CFGWRITE 由上位机发起） |

### 7.2 Mode3 协议

详见 `ej/doc/Mode3_Output_Protocol.md`。

- 标准包 40 B，魔数 `AA 10 33`
- CHGNO 扩展包 46 B（附带触点坐标）

### 7.3 CFGWRITE / CFGREAD（MODE0）

| 命令字节 | 名称 |
|----------|------|
| `0xD0`~`0xD2` | CFGWRITE START / CHUNK / END |
| `0xD3`/`0xD4` | ACK / NACK |
| `0xE1`/`0xE2` | CFGREAD DATA / END |

对象槽上限：`CFG_MAX_OBJECTS = 128`。

### 7.4 USB CDC

| 参数 | 值 |
|------|-----|
| 类 | CDC ACM |
| VID/PID | 1155 / 22336（`0483:5740`） |
| 最大包 | 64 B |

![USB 发送通道](images/usb_tx_channels.png)

---

## 8. PC 端软件

### 8.1 Serial Terminal

路径：`ej/serial-app`

- USB 串口连接、Mode3 解析与热力图
- xcfg 配置编辑与 CFGWRITE 烧录
- 集成 mxt-app CLI

### 8.2 构建

```bash
cd ej/serial-app
npm install
npm run dev
npm run package:user:nsis:fast   # Windows 安装包
```

---

## 9. 典型工作流程

### 9.1 首次连接

1. USB 连接 → 识别 COM 口  
2. 查看上电欢迎信息或发送 `INFO`、`FINDIIC`  
3. 确认 Family/Variant/矩阵与 **640U 或 641** 一致  

### 9.2 SPI 实时矩阵（推荐）

```text
SPISTART3
# PC 端以二进制接收 Mode3，16 包/扫描帧
SPISTOP
```

### 9.3 I2C 诊断对比

```text
START MAP16L90X 10
STOP
```

### 9.4 配置烧录

1. Serial Terminal 加载 `.xcfg`  
2. 自动 MODE0 → CFGWRITE → UNFREEZE  
3. 本变体 **不支持 ENC 固件流式烧录**（`MXT_HAS_ENC=0`）；640UD 主工程支持  

---

## 10. 开发与构建

| 项目 | 说明 |
|------|------|
| IDE | Keil µVision 5 |
| 工程 | `ej/test-V1.7/MDK-ARM/STMUSBATMXT640.uvprojx` |
| 输出 | `STMUSBATMXT640.hex` |
| 关键配置 | `mxt_config.h` — 队列深度、帧长、DEBUGCTRL |

**修改指引**：

| 需求 | 位置 |
|------|------|
| 调整帧长/队列 | `mxt_config.h` |
| NSS 组帧逻辑 | `mxt_spi_stream.c` → `MXT_ProcessSPICheck()` |
| 新增命令 | `mxt_cmd.c` |
| SPI 硬件模式 | `Core/Src/spi.c` |

---

## 11. 故障排查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| USB 无 COM | USB_EN、驱动 | 查 PA15 时序；安装 ST VCP |
| FINDIIC 失败 | 接线/复位 | PB10/11、RST、ADDSEL |
| SPI 无数据 | DEBUGCTRL 未写 | `INFO` 确认 T6；重发 SPISTART |
| 矩阵缺行/花屏 | 队列溢出 | `SPIDBG` 查 ovf/drop；勿减小 2048 队列 |
| 640UD 无 SPI 数据 | 用错固件 | 换主工程 + 虚拟 SSN |
| Mode3 CRC 错 | 算法不一致 | CRC16-CCITT-FALSE |
| CFGWRITE NACK | 对象过多 | total ≤ 128 |

---

## 12. 附录

### 12.1 文档索引

| 文档 | 路径 |
|------|------|
| test-V1.7 快速说明 | `ej/test-V1.7/README.md` |
| 640UD 主工程文档 | `ej/doc/STMUSBATMXT640_技术文档.md` |
| Mode3 协议 | `ej/doc/Mode3_Output_Protocol.md` |
| 命令参考 | `ej/doc/String_Commands.md` |
| Debug Port | `ej/doc/Level1_QTAN0050_*.md` |
| 主工程 readme | `readme.txt` |

### 12.2 术语表

| 术语 | 说明 |
|------|------|
| **DEBUGCTRL** | T6 Byte4，选择 SPI 调试数据类型（SIGNAL/DELTAS 等） |
| **NSS / SSN** | SPI 片选，本设计接 PA4 硬件 NSS |
| **Mode3** | 上位机二进制矩阵行包协议（`AA 10 33`） |
| **640U 三次 SSN** | 每扫描周期三次片选脉冲，第一次下降沿起计 640 B |
| **xcfg** | maXTouch 配置文件格式 |

### 12.3 插图

重新生成插图：

```bash
python ej/doc/gen_stmusbmxt_images.py
python ej/doc/md_to_docx.py ej/doc/STMUSBMXT_技术文档.md ej/doc/STMUSBMXT_技术文档.docx
```

---

*本文档针对 test-V1.7 固件与 ATMXT640U/ATMXT641 硬件编写；640UD 详见 STMUSBATMXT640 技术文档。*
