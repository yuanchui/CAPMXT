
### 3.5 `CHGON`
- **功能**：开启 CHG 处理，并立即执行一次消息读取。
- **副作用**：会将 `g_msg_output_enabled` 置为 1，保证可输出解析后的消息文本。

### 3.6 `CHGOFF`
- **功能**：关闭 CHG 处理（默认状态）。

---

## 4. 单次诊断帧读取（FRAME*）

### 4.1 `FRAME0`
- **模式码**：`0x10`
- **含义**：Mutual Delta（互容变化值，通常按 `int16` 解释）。

### 4.2 `FRAME` / `FRAME1`
- **模式码**：`0x11`
- **含义**：Mutual Reference（互容参考值）。

### 4.3 `FRAME3`
- **模式码**：`0xF7`
- **含义**：Self Delta（自容变化值）。

### 4.4 `FRAME4`
- **模式码**：`0xF8`
- **含义**：Self Reference（自容参考值）。
---

## 5. 定时连续输出（START/STOP）

### 5.1 `START`
- **功能**：启动定时读取 T37 并输出。
- **间隔参数**：`interval_ms`，允许范围 `10 ~ 5000`，否则回退到 `1000ms`。

#### 5.1.1 基本语法
```text
START [interval_ms]
```

#### 5.1.2 MAP16 变体语法（矩阵变换）
```text
START MAP16[ R90 | L90 ][ X ][ Y ] [HEX|CHAR] interval_ms
```
- **说明**：
  - `MAP16` 默认输出（`AA 10 33`）。

#### 5.1.3 CHGNO 扩展（Mode3 + 触点信息）
```text
START CHGNO[X|Y|XY] [MAP16...] interval_ms
START CHGNO [X|Y|XY] interval_ms
```

### 5.2 `STOP`
- **功能**：停止定时输出。

---

## 6. MAP 输出（单次文本/矩阵）

### 6.1 `MAPALL`
- **功能**：读取一帧并输出完整 `X*Y` 矩阵 

### 6.2 `MAP16`
- **功能**：读取一帧并输出 `16x16`。

### 6.3 `MAP16` 组合变体
- **格式**：`MAP16[ R90 | L90 ][ X ][ Y ]`
- **含义**：对 `16x16` 矩阵进行“先旋转再翻转”的变换后输出。

---

## 7. 常见用法示例

### 7.1 互容变化值（默认 0x10）10ms 连续输出 + 触点扩展
```text
START CHGNOXY MAP16L90X 10
```


