# serial-app 原有逻辑与 ENC 新增功能说明

**工程路径：** `ej/serial-app` + `USB_DEVICE/App/mxt/`  
**相关文档：**  
- XCFG 配置上传：`ej/doc/xcfg_upload_analysis.md`  
- ENC 固件与 I2C 抓包：`ej/doc/enc_upload_analysis.md`、`ej/bit/enc.txt`  
- Bootloader 协议：QTAN0051（`ej/doc/使用Host烧录FW文档Level2_QTAN0051_BX-Bootloader-20260514012450.md`）

---

## 1. 总体架构

```text
PC (Electron serial-app)
    │  USB CDC / WinUSB
    ▼
STM32 桥接固件 (test-V1)
    │  I2C
    ▼
ATMXT640UD 触摸芯片
    · 应用模式 @0x4B — 配置 RAM / 正常运行
    · Bootloader @0x27 — 固件更新（.enc）
```

上位机不直接操作 I2C，而是通过 **二进制桥协议** 或 **字符串命令** 让 STM32 转发。两种业务：

| 业务 | 文件类型 | 芯片模式 | 持久化位置 |
|------|----------|----------|------------|
| 配置升级 | `.xcfg` / prepared `.bin` | 应用模式 0x4B | T6 RAM + BACKUPNV → NVM |
| 固件烧录 | `.enc` | Bootloader 0x27 | 芯片内部 Flash |

---

## 2. 原有逻辑（ENC 新增之前）

### 2.1 桥接模式

STM32 `mxt_bridge.c` 维护两种模式：

| 模式 | 进入方式 | 用途 |
|------|----------|------|
| **字符串模式** | 默认 / `02 01 10 20` / `CMD_CONFIG` bit7=1 | `help`、`T100CFG`、矩阵数据输出等 |
| **二进制桥模式 (mode0)** | `0x80 0x00 0x00` 或 `MODE0` | CFGWRITE、I2C 桥 `0x51`、ENCWRITE（新增） |

上位机在传配置前会发 `0x80 0x00 0x00` 切到 mode0；传输结束后可用 `0x80 0x00 0x80` + `02 01 10 20` 回到字符串模式。

### 2.2 XCFG 配置升级（CFGWRITE）

**目的：** 将 Studio 导出的触摸 **参数对象表** 写入芯片 RAM，并备份到 NVM。

**上位机：**

1. `xcfg_codec.ts`：解析 `.xcfg` 明文 → 按对象编码为二进制 → 分包 + CRC16（Modbus IBM）。
2. `prepareXcfgBinary`：可落盘 `config1-prepared.bin`，并在内存中保留 **分包计划 token**（流式发送，避免整包缓存）。
3. `writeXcfgAndExportFromMcu` / `writeBinAndBackupFromMcu`：经 USB 发送 CFGWRITE 帧序列。

**USB 协议（`cfg_protocol.ts` ↔ `mxt_config.h`）：**

| 命令 | 值 | 含义 |
|------|-----|------|
| START | `0xD0` | 对象元数据 + MCU 侧 FREEZE |
| CHUNK | `0xD1` | 单包数据写 I2C |
| END | `0xD2` | 传输结束确认 |
| ACK/NACK | `0xD3` / `0xD4` | 6 字节应答 |
| FREEZE / UNFREEZE / BACKUPNV | `0x22` / `0x11` / `0x55` | T6 控制 |

**MCU：**

- `g_cfg_rx_buf` 流式重组 USB 拆包（START 最大约 12+128×4 字节）。
- 收到 `D0`：解析对象表 → T6 FREEZE → ACK。
- 收到 `D1`：按 seq 写 I2C → ACK（对象最后一包可带 `STATUS_OBJ_DONE`）。
- 收到 `D2`：ACK，结束 CFG 会话。
- 上位机再发 `BACKUPNV`、可选 `UNFREEZE`。

**对象数：** MCU 固定预留 **128** 槽位（`CFG_MAX_OBJECTS`）；实际上传对象数由 START 帧 `total_objects` 声明（如 PICO 640UD 约 125）。

**UI：** XCFG 弹窗 → 选择 XCFG/BIN → 预处理 →「写入 test-V1 并自动备份」。

### 2.3 BIN 直传

已预处理的 `config1-prepared.bin`（内含 D0/D1/D2 帧序列）可跳过 xcfg 解析，直接 `writeBinAndBackupFromMcu` 上传，逻辑与 xcfg 传输阶段相同。

### 2.4 原有 I2C 桥命令（仍保留）

`mxt_bridge.c` 在二进制模式下还支持 mxt-app 风格命令，例如：

- `0x51`：带/不带寄存器地址的 I2C 读写（Bootloader 无寄存器写用于 MTU 工具直连）。
- `0x82`：读 CHG 引脚。
- `0xE0`：扫描 I2C 地址。

**原有逻辑不包含：** 上位机解析 `.enc`、专用 ENC USB 协议、UI 一键烧录固件。

---

## 3. 新增功能：ENC 流式固件烧录

### 3.1 要解决什么问题

- `.enc` 为 Atmel 工具生成的 **十六进制 ASCII 加密帧流**（示例约 185KB 二进制、1186 帧），Host **不解密**，只负责可靠传输。
- 不宜在 PC 或 MCU 上 **整文件缓存**；需 **边读文件、边切帧、边 USB 下发、边 I2C 写 Bootloader**。
- 芯片侧 Bootloader 按 QTAN0051 状态机：`A0 → 写帧 → 02 → 04 → A0`。

### 3.2 数据流

```text
.enc 文件 (hex ASCII)
    → enc_codec.ts 流式 hex 解码 + 大端 L 切帧（单帧在内存 ≤276B）
    → USB ENCWRITE (B0/B1/B2)
    → MCU g_enc_rx_buf 重组单帧（≤292B）
    → mxt_enc.c：Bootloader 状态机 + I2C WriteNoReg @0x27
    → 芯片内解密、CRC、写 Flash
```

与 MTU + SAMD21 抓包（`enc.txt`）一致：**I2C 线上字节 = .enc 解码后的帧原样**，不在桥接层二次加密。

### 3.3 新增 USB 协议（ENCWRITE）

常量定义：`mxt_config.h`、`ej/serial-app/src/main/enc_protocol.ts`（须保持一致）。

| 命令 | 值 | 帧格式（小端字段 + CRC16） | MCU 行为 |
|------|-----|---------------------------|----------|
| ENC_START | `0xB0` | ver, bl_addr, flags, total_frames(u16), crc | 进 BL、DC AA 解锁、等 A0 |
| ENC_FRAME | `0xB1` | seq(u16), len(u16), enc帧字节[len], crc | 等 A0 → 写 I2C → 02→04→A0 |
| ENC_END | `0xB2` | end_seq(u16), reserved(u16), crc | 结束会话、延时、回字符串模式倾向 |
| ENC_ACK | `0xB3` | seq(u16), status, crc | 与 CFG ACK 同结构 |
| ENC_NACK | `0xB4` | 同上 | 失败原因 status |

**ENC_START 参数：**

- `bl_addr`：Bootloader 7-bit I2C 地址；`0` 表示 MCU 自动探测（优先 **0x27**，再 0x25/0x24/0x26）。
- `flags`：bit0 = `SKIP_ENTER_BOOTLOADER`（已在 BL 时可跳过 T6 0xA5）。

**单帧大小上限：** `ENC_MAX_FRAME_BYTES = 276`（2 + L_max 274），MCU 缓冲 `ENC_RX_BUF_SIZE ≈ 292` 字节。

### 3.4 MCU 新增代码

| 文件 | 说明 |
|------|------|
| `mxt_enc.c` / `mxt_enc.h` | 进 Bootloader（T6 RESET `0xA5`）、解锁 `DC AA`、逐帧状态机、I2C 写 |
| `mxt_bridge.c` | ENC 流式重组 + B0/B1/B2 处理 |
| `mxt_config.h` | ENC 命令字与缓冲常量 |
| `mxt_state.c/h` | `g_encwrite_active`、`g_enc_rx_buf`、`g_enc_next_seq` 等 |
| `mxt_i2c.c` | `MXT_FindI2CAddress` 增加 **0x27** 探测 |

### 3.5 上位机新增代码

| 文件 | 说明 |
|------|------|
| `enc_codec.ts` | `scanEncFile`、`iterateEncFramesFromFile` 流式解析 |
| `enc_protocol.ts` | 协议常量、`ENC_DEFAULT_BL_ADDR = 0x27` |
| `main/index.ts` | IPC `flash-enc-from-mcu` |
| `preload/index.ts` | `flashEncFromMcu` |
| `renderer/index.html` | 选择 ENC、按钮「上传 ENC 固件」 |

**IPC `flash-enc-from-mcu` 参数：**

```typescript
{
  portPath: string;           // 必填
  encFilePath: string;        // 必填，本地 .enc 路径（不读入 base64）
  fileName?: string;
  bootloaderAddr?: number;    // 默认 0x27
  skipEnterBootloader?: boolean;
}
```

**传输步骤（主进程）：**

1. 流式 `scanEncFile` 统计帧数（不占大内存）。
2. mode0：`0x80 0x00 0x00`。
3. 发 ENC_START，等 ENC_ACK seq=0。
4. `for await` 逐帧发 ENC_FRAME，每帧等 ACK（WinUSB 期间后台 IN pump）。
5. 发 ENC_END，等 ACK。
6. 进度事件复用 `xcfg-transfer-progress`；取消复用 `cancel-xcfg-transfer`。

### 3.6 UI 变化

XCFG 弹窗（`xcfgModal`）：

- 按钮文案：**选择 XCFG/BIN/ENC**。
- 选 `.enc` 后显示 **上传 ENC 固件**（隐藏「写入 test-V1」）。
- 选 `.xcfg` / `.bin` 行为与原来相同。
- 进度同步到接收区，前缀 `[XCFG]`（与 xcfg 共用通道）。

---

## 4. 原有 vs 新增对比

| 维度 | XCFG / BIN（原有） | ENC（新增） |
|------|-------------------|-------------|
| 文件 | `.xcfg`、prepared `.bin` | `.enc`（hex 文本） |
| USB 协议 | CFGWRITE `D0/D1/D2` | ENCWRITE `B0/B1/B2` |
| 芯片模式 | 应用 0x4B | Bootloader 0x27 |
| 进入方式 | START 内 FREEZE | T6 RESET 0xA5 + DC AA |
| 结束动作 | BACKUPNV / UNFREEZE | 芯片自复位回应用模式 |
| Host 解码 | xcfg 明文编码 | hex→binary→切帧，**不解密** |
| MCU 缓冲 | START 最大约 528B 对象表 + 重组 | 单帧约 **292B** |
| 上位机内存 | 流式分包 token，可选落盘 bin | 流式读文件，**仅保留当前帧** |
| 典型帧数 | 约 125 包（配置） | 约 1186 帧（示例固件） |

---

## 5. 代码路径速查

```text
ej/serial-app/
  src/main/index.ts          # IPC：prepare/write xcfg/bin、flash-enc-from-mcu
  src/main/cfg_protocol.ts   # CFGWRITE 常量
  src/main/enc_protocol.ts   # ENCWRITE 常量（新增）
  src/main/enc_codec.ts      # .enc 流式解析（新增）
  src/main/xcfg_codec.ts     # .xcfg 编解码
  src/preload/index.ts       # electronAPI 暴露
  src/renderer/index.html    # XCFG/ENC 弹窗 UI

USB_DEVICE/App/mxt/
  mxt_bridge.c               # 桥接 + CFGWRITE + ENCWRITE
  mxt_enc.c                  # Bootloader 烧录逻辑（新增）
  mxt_config.h               # 协议与缓冲上限
  mxt_state.c/h              # 全局状态
  mxt_i2c.c                  # I2C 地址探测（含 0x27）
```

---

## 6. 使用步骤

### 6.1 配置升级（原有）

1. 编译并烧录 STM32 固件（含 CFG_MAX_OBJECTS=128 等）。
2. 构建 serial-app，连接设备。
3. XCFG 弹窗 → 选 `.xcfg` → 自动预处理 → **写入 test-V1 并自动备份**。

### 6.2 ENC 固件烧录（新增）

1. **必须**使用含 `mxt_enc.c` 的 STM32 固件。
2. 构建 serial-app。
3. XCFG 弹窗 → 选 `.enc` → 连接设备 → **上传 ENC 固件**。
4. 烧录完成后按 QTAN0051 **重新写入 xcfg 配置/校准**。

### 6.3 停止传输

传输中点击 **停止传输**（xcfg / bin / enc 共用 `cancel-xcfg-transfer`）。

---

## 7. 注意事项

1. **固件与上位机需配套**：ENC 功能依赖 MCU 侧 B0/B1/B2；仅更新 serial-app 而无新固件时，ENC 烧录不可用。
2. **地址**：mXT640UD ADDR_SEL=High 时 Bootloader 为 **0x27**（与 `enc.txt` 一致）；与部分文档中的 0x25 表项不同，以本工程探测顺序为准。
3. **耗时**：1186 帧 × 每帧 Bootloader 握手，整包烧录时间较长；USB 回调内会轮询 I2C 状态，期间避免并发其它桥操作。
4. **与 xcfg 互斥**：MCU 在 `g_encwrite_active` 或 `g_cfgwrite_active` 期间不应混用两套协议。

---

## 8. 版本说明

| 项目 | 说明 |
|------|------|
| CFG 对象上限 | 96 → **128**（修复 PICO 125 对象 START 被拒/缓冲溢出） |
| ENC 烧录 | **本次新增** ENCWRITE + 流式 `.enc` + UI 按钮 |
| 文档快照 | `ej/doc/src修改版本` 仅为部分 UI/preload 片段，**不含** `main/index.ts` 与 ENC 实现；以 `ej/serial-app` 为完整工程 |

---

*文档生成对应工程状态：ENC 流式烧录功能合入后。*
