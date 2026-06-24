# STMUSBATMXT640 — test-V1.7 固件

> **适用芯片**：Microchip **ATMXT640U** / **ATMXT641**（maXTouch U 系列，640 节点级）  
> **MCU**：STM32F103C8  
> **完整技术说明**：[`ej/doc/STMUSBMXT_技术文档.md`](../doc/STMUSBMXT_技术文档.md)（含 Word 版 `STMUSBMXT_技术文档.docx`）

---

## 工程定位

本目录为 **test-V1.7** 固件变体，面向触摸芯片 **硬件 SPI 调试口带 SSN 片选** 的板卡（PA4 接芯片 Debug SSN）。与仓库主工程（`Core/` + `USB_DEVICE/`）相比，本变体：

| 项目 | test-V1.7（本目录） | 主工程 |
|------|---------------------|--------|
| 目标芯片 | **ATMXT640U**、**ATMXT641** | **ATMXT640UD** 等 |
| SPI 帧边界 | **硬件 NSS（PA4）**，三次 SSN/扫描帧 | 虚拟 SSN（PA10 监视 MISO → PA9 合成） |
| SPI 接收 | **中断 IT 逐字节** + 2048 深队列 | DMA 环形缓冲 + 帧间提取 |
| 调试使能 | T6 **DEBUGCTRL** Byte4 = `0x20`（SIGNAL） | T6 **DEBUGCTRL2** Byte6 = `0x80`（DBGOBJMODEEN） |
| SPI 稳定帧长 | **640 B**（20 行 × 40 B，取每行 32 B 有效数据） | 514 B（DBGOBJMODEEN 固定帧） |
| ENC 烧录 | 关闭（`MXT_HAS_ENC=0`） | 支持 ENCWRITE |

**选用原则**：板卡已将触摸芯片 Debug SSN 接至 STM32 **PA4（SPI1_NSS）** 时使用本固件；640UD 无硬件 SSN、需虚拟片选时使用主工程。

---

## 目录结构

```text
test-V1.7/
├── Core/                    # STM32 外设：GPIO、I2C2、SPI1、main
├── USB_DEVICE/App/mxt/      # maXTouch 应用层（命令、SPI 流、I2C、桥接）
├── Middlewares/             # ST USB Device Library
├── Drivers/                 # HAL / CMSIS
└── MDK-ARM/
    ├── STMUSBATMXT640.uvprojx
    └── STMUSBATMXT640/STMUSBATMXT640.hex   # 编译输出
```

核心应用模块：

| 模块 | 文件 | 说明 |
|------|------|------|
| 配置常量 | `mxt_config.h` | 640U 帧长、队列深度、I2C 地址 |
| SPI 流 | `mxt_spi_stream.c` | IT 接收、NSS 边沿判帧、Mode3 打包 |
| 命令 | `mxt_cmd.c` | SPISTART / START / HELP 等 |
| 触摸/I2C | `mxt_touch.c`, `mxt_i2c.c` | Info Block、T37 诊断、DEBUGCTRL |
| USB | `mxt_usb_io.c`, `usbd_cdc_if.c` | CDC 文本与二进制发送 |
| 桥接 | `mxt_bridge.c` | MODE0 二进制、CFGWRITE/CFGREAD |

---

## 硬件连接

引脚定义见 `Core/Inc/main.h`：

| 引脚 | 信号 | 说明 |
|------|------|------|
| PA3 | CHG | 触摸消息就绪，EXTI 中断 |
| **PA4** | **SSN** | **SPI1 硬件 NSS，接芯片 Debug 片选** |
| PA5 | SCK | SPI 从机时钟（芯片为 Master） |
| PA7 | MOSI | SPI 数据（从机 RX-only） |
| PA11/12 | USB | Full Speed CDC |
| PA15 | USB_EN | 上电拉高 100 ms 后拉低 |
| PB1 | RST | 触摸芯片复位（低 40 ms 脉冲） |
| PB10/11 | I2C2 | 配置与诊断 |
| PB12 | ADDSEL | I2C 地址选择（高 = 0x4B） |

SPI 配置（`Core/Src/spi.c`）：从机、Mode 0、8 bit、**硬件 NSS 输入**。

---

## 快速开始

### 1. 编译与烧录

1. 安装 Keil MDK-ARM v5 + STM32F1 器件包  
2. 打开 `MDK-ARM/STMUSBATMXT640.uvprojx`  
3. Build → Download  
4. 输出：`MDK-ARM/STMUSBATMXT640/STMUSBATMXT640.hex`

### 2. 连接 PC

1. USB 连接桥接板，等待虚拟 COM 口（VID `0483`，PID `5740`）  
2. 打开 [`ej/serial-app`](../serial-app) 或任意串口工具（波特率对 CDC 无实质影响）  
3. 上电后自动输出 Info Block，或发送：

```text
INFO
FINDIIC
```

确认 Family / Variant / 矩阵尺寸与目标芯片一致。

### 3. 实时矩阵采集（推荐）

```text
SPISTART3          # SPI → Mode3 二进制（16×16，640 B/扫描帧）
SPISTOP            # 停止
SPIDBG             # 查看队列溢出、TX 丢弃等
```

I2C 诊断路径（不依赖 SPI 调试口）：

```text
START MAP16L90X 10
STOP
```

---

## SPI 数据流要点（640U / 641）

触摸芯片每个扫描周期通过 SPI Master 输出调试数据，**每帧含三次 SSN 脉冲**；固件在**第一次 SSN 下降沿**起收集 **640 字节**有效载荷：

- 源格式：20 行 × 40 B/行  
- 每行跳过首字节（常见 `0x80`），取后续 **32 B** → 共 16 行 × 32 B 显示为 **16×16** 矩阵  
- `SPISTART3`：每行打包为 40 B Mode3 包（`AA 10 33`），整帧 **16 包 × 40 B = 640 B** USB 输出  

缓冲参数（`mxt_config.h`，**勿随意缩小**，否则 SPI 丢包）：

```c
#define SPI_RX_QUEUE_DEPTH        2048U
#define SPI_HEX_TX_BUF_SIZE       4096U
#define SPI_FRAME_PAYLOAD_BYTES   640U
```

---

## 文本命令摘要

完整列表见 [`ej/doc/String_Commands.md`](../doc/String_Commands.md) 或固件内 `HELP`。

| 命令 | 功能 |
|------|------|
| `MODE0` / `MODE1` | 二进制桥接 / 文本模式 |
| `SPISTART` | 原始 HEX 文本流 |
| `SPISTART1` | 16×16 HEX 文本（带帧号） |
| `SPISTART3` | Mode3 二进制（推荐） |
| `SPISTOP` | 停止 SPI 流 |
| `START MAP16*` | I2C T37 定时 Mode3 输出 |
| `CFGWRITE` / 上位机 xcfg | MODE0 下写入配置（ENC 本变体未启用） |

---

## 芯片识别

上电 `INFO` 或读 Information Block 确认型号，**勿硬编码对象地址**。

| 芯片 | Family | Variant（常见） | 矩阵 | 本固件 |
|------|--------|-----------------|------|--------|
| ATMXT640U | `0xA6` | `0x01` / `0x02` | 以 INFO 为准（典型 32×20） | **主要适配** |
| ATMXT641 / mXT641T | 以 INFO 为准 | 以 INFO 为准 | 640 节点级 | **协议兼容，须 INFO 校验** |
| ATMXT640UD | `0xA6` | `0x15` / `0x17` | 32×20 | 请用**主工程**（虚拟 SSN + DEBUGCTRL2） |

---

## 故障排查

| 现象 | 处理 |
|------|------|
| FINDIIC 失败 | 查 I2C 接线、RST 脉冲、ADDSEL |
| SPISTART 无数据 | 确认 T6 地址有效；`INFO` 后重试；查 DEBUGCTRL 是否写入 |
| 矩阵花屏/丢行 | 查 `SPIDBG` 中 `rx_ovf` / `tx_drop`；勿减小 `SPI_RX_QUEUE_DEPTH` |
| 640UD 板卡无数据 | 换用主工程固件（需虚拟 SSN 路径） |
| USB 无 COM 口 | 检查 PA15 USB_EN 时序与驱动 |

---

## 相关文档

| 文档 | 路径 |
|------|------|
| 640U/641 技术文档 | [`ej/doc/STMUSBMXT_技术文档.md`](../doc/STMUSBMXT_技术文档.md) |
| 640UD 主工程文档 | [`ej/doc/STMUSBATMXT640_技术文档.md`](../doc/STMUSBATMXT640_技术文档.md) |
| Mode3 协议 | [`ej/doc/Mode3_Output_Protocol.md`](../doc/Mode3_Output_Protocol.md) |
| 命令参考 | [`ej/doc/String_Commands.md`](../doc/String_Commands.md) |
| 主工程说明 | [`readme.txt`](../../readme.txt) |
| 上位机 | [`ej/serial-app/`](../serial-app/) |

---

*版本：test-V1.7 · 更新：2026-06-24*
