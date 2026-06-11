## Mode3 输出协议说明（AA 10 33）

本文件描述 `usbd_cdc_if.c` 中实现的 **Mode3 风格二进制输出协议**，即以固定报文头 `AA 10 33` 开头的帧格式，以及在 `START CHGNO` 模式下新增的触点扩展字段。

---

## 1. 基本帧格式（兼容原有 START / MAP16* 模式）

在普通 `START` 或 `START MAP16*` 串行诊断输出模式下，每一帧按行拆分为 16 行，每行发送一个 Mode3 包：

- **帧头**：`AA 10 33`
- **通用字段顺序**：

```text
AA 10 33 | LEN | FRAME_ID | LINE_ID | DATA[16 * 2] | CRC16
```

- **字段说明**

- **AA 10 33**：固定包头（3 字节）
- **LEN**：整包长度（含头、LEN、本行数据、CRC16），1 字节  
  - 计算方式：`LEN = 3 (头) + 1 (LEN) + 1 (FRAME_ID) + 1 (LINE_ID) + 32 (DATA) + 2 (CRC16) = 40 (0x28)`
- **FRAME_ID**：帧号，1 字节，启动输出时从 0 自增（溢出后回绕）
- **LINE_ID**：行号，1 字节，范围 `0 ~ 15`，每个完整帧共 16 行
- **DATA[16 * 2]**：本行 16 个采样点，每个点 2 字节（大端，高字节在前）
  - 索引顺序：`out_x = 0..15`
  - 考虑旋转 / 翻转后从 `g_diag_buffer` 取值：
    - 先按 `g_stream_flip` 做 X/Y 翻转（在 0~15 内）
    - 再按 `g_stream_rot` 做 90° 旋转
    - 最终索引：`g_diag_buffer[src_y + src_x * g_matrix_y_size]`
- **CRC16**：2 字节（大端，高字节在前）
  - 多项式：`0xA001`
  - 初值：`0xFFFF`
  - 计算范围：**从包头 `AA` 开始，到 CRC16 之前的所有字节**（即不含 CRC 自身）

> 说明：在非 `CHGNO` 模式下，协议与原 Mode3 输出行为完全等价，未增加任何附加字段，确保兼容现有 `START MAP16L90X 10` 等旧协议。

---

## 2. START CHGNO 模式下的扩展帧格式

### 2.1 启动指令

新增指令：

```text
START CHGNO [X|Y|XY] interval_ms
```

- **CHGNO**：启用 “CHG+触点号” 扩展模式
- **[X|Y|XY]**（可选）：
  - `X`：对 X 坐标做翻转：`x' = 830 - x`
  - `Y`：对 Y 坐标做翻转：`y' = 940 - y`
  - `XY`：对 X、Y 都做翻转
- **interval_ms**：诊断输出间隔（单位 ms）
  - 允许范围：`10 ~ 5000`，超出则回退到 `1000ms`

此模式下仍旧输出 Mode3 包，但在 16×2 原始数据与 CRC16 之间插入触点信息字段。

### 2.2 CHGNO 模式帧结构

在 `START CHGNO` 模式下，每行的包格式为：

```text
AA 10 33 | LEN | FRAME_ID | LINE_ID | DATA[16 * 2] | TOUCH_ID | X | Y | ACTION | CRC16
```

- **LEN**：整包总长度（含扩展字段），1 字节
  - 计算方式：

```text
LEN = 3 (头) + 1 (LEN) + 1 (FRAME_ID) + 1 (LINE_ID)
    + 32 (DATA) + 6 (TOUCH_ID + X + Y + ACTION) + 2 (CRC16)
    = 46 (0x2E)
```

- **TOUCH_ID**：触点号，1 字节
  - 范围：`0 ~ 16`（对应 maXTouch T100 报告 ID 的触点索引）
  - 若当前没有有效触点，则发送 `0xFF`

- **X**：X 坐标，2 字节（小端，低字节在前）
  - 源自 maXTouch T100 消息中的 `x_pos`
  - 若开启翻转：
    - 当 `flip_mask & 0x01`（X 翻转）且 `x_pos <= 830` 时：
      - `x' = 830 - x_pos`
  - 最后做范围裁剪：
    - `0 <= X <= 830`

- **Y**：Y 坐标，2 字节（小端，低字节在前）
  - 源自 maXTouch T100 消息中的 `y_pos`
  - 若开启翻转：
    - 当 `flip_mask & 0x02`（Y 翻转）且 `y_pos <= 940` 时：
      - `y' = 940 - y_pos`
  - 最后做范围裁剪：
    - `0 <= Y <= 940`

- **ACTION**：动作类型，1 字节

| ACTION 值 | 含义        | 对应 T100 event |
|----------|-------------|------------------|
| 0        | NONE        | 其他 / 未识别   |
| 1        | DOWN        | 4 (DOWN)        |
| 2        | MOVE        | 1 (MOVE)        |
| 3        | UP          | 5 (UP)          |
| 4        | DOWNUP      | 9 (DOWNUP)      |

- **CRC16**：2 字节（大端）
  - 计算方式与普通 Mode3 包相同：  
    从 `AA` 开始到 `ACTION` 字节结束（即**包含触点扩展字段**），再计算 CRC16，结果附在末尾。

---

## 3. 触点信息来源（CHG 队列）

在 `START CHGNO` 模式下：

- 固件会强制开启：
  - `g_chg_process_enabled = 1`  
    使能基于 CHG 引脚的消息读取
  - `g_msg_output_enabled = 0`  
    关闭文本 T100 输出，只用于内部提取触点数据，节省带宽

- 在 `MXT_CheckAndProcessMessages()` 中：
  - 从 T44（message count）获取消息数量
  - 从 T5 读取一条 11 字节消息，识别为 T100 报告：
    - `report_id` 范围：`[g_t100_report_id, g_t100_report_id + 18)`
    - `rid_offset = report_id - g_t100_report_id`
    - 当 `rid_offset >= 2` 时，该消息对应触点 `TOUCH_ID = rid_offset - 2`
  - 提取字段：
    - `status = msg_data[1]`
    - `event = status & 0x0F`
    - `x_pos = msg_data[2] | (msg_data[3] << 8)`
    - `y_pos = msg_data[4] | (msg_data[5] << 8)`
  - 将事件映射到 `ACTION`：
    - `event == 4` → DOWN
    - `event == 1` → MOVE
    - `event == 5` → UP
    - `event == 9` → DOWNUP
    - 其余 → NONE
  - 更新全局触点信息缓存：

```c
g_last_touch.id     = touch_id;
g_last_touch.x      = x_pos;
g_last_touch.y      = y_pos;
g_last_touch.action = action;
g_last_touch_valid  = 1;
```

- 在定时诊断输出函数 `MXT_TimerDiagnosticRead()` 中：
  - 每到达设定间隔，读取一次完整 T37 诊断帧到 `g_diag_buffer`
  - 若 `g_stream_map16_hex == 1`，调用：

```c
MXT_SendMode3Packets(g_stream_rot, g_stream_flip, g_stream_frame_id++);
```

  - 在 `MXT_SendMode3Packets()` 内部，就会在每行的数据后追加从 `g_last_touch` 获取的触点信息。

---

## 4. PC 端解析建议

### 4.1 普通 Mode3（兼容原有）

- 按旧逻辑解析：
  - 检查头：`AA 10 33`
  - 根据 `LEN`（40/0x28）判定包长
  - 提取 `FRAME_ID`、`LINE_ID`、32 字节数据、CRC16
  - 对 `[AA..最后一个数据字节]` 做 CRC16 校验

### 4.2 CHGNO Mode3（扩展包）

- 识别方式：
  - 同样检查头 `AA 10 33`
  - 若 `LEN == 46 (0x2E)`，则可以识别为 **带触点扩展的 CHGNO 包**
    - 如果将来需要兼容更复杂扩展，可再根据上位机的配置或额外指示位做区分

- 解析顺序：

```text
offset 0  : 0xAA
offset 1  : 0x10
offset 2  : 0x33
offset 3  : LEN
offset 4  : FRAME_ID
offset 5  : LINE_ID
offset 6  : DATA[0] (高字节)
...
offset 6+31: DATA[15] 低字节
offset 38  : TOUCH_ID
offset 39  : X_L
offset 40  : X_H
offset 41  : Y_L
offset 42  : Y_H
offset 43  : ACTION
offset 44  : CRC_H
offset 45  : CRC_L
```

- PC 侧坐标和动作解释：
  - `TOUCH_ID`：
    - `0xFF`：当前无有效触点，可忽略坐标与动作
    - 其他值：0~16，对应 maXTouch 的触点 ID
  - `X / Y`：
    - 坐标范围：
      - `0 ~ 830`（X）
      - `0 ~ 940`（Y）
    - 已经按照 `START CHGNO [X|Y|XY]` 的翻转设置在固件侧处理完毕，PC 无需再二次翻转
  - `ACTION`：
    - `1`：DOWN（起点）
    - `2`：MOVE（移动）
    - `3`：UP（抬起/离开）
    - `4`：DOWNUP（快速点按）
    - `0`：可视为“无事件/无效”

---

## 5. 向后兼容性

- 不使用 `START CHGNO` 的场景：
  - Mode3 输出帧保持原有格式不变：`AA 10 33 | LEN(40) | frame | line | 32-byte data | CRC16`
  - 既有 PC 工具（如旧版本的 mxt-app / xcfg-viewer）可无改动继续使用

- 使用 `START CHGNO` 的场景：
  - 只需要在 PC 端根据 `LEN` 或配置判断是否有扩展字段；
  - 若识别到扩展版本，则按本文件第 2、4 章的定义解析触点信息。  
