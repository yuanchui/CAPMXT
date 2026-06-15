# mxt-app 诊断数据采集命令速查

## 常用模式切换（直接可用）

### 1) Mutual（互电容）

#### Delta（默认）

```bash
sudo mxt-app --debug-dump -
```

#### Reference（参考值）

```bash
sudo mxt-app --debug-dump - --references
```

### 2) Self-cap（自电容）

#### Signals

```bash
sudo mxt-app --debug-dump - --self-cap-signals
```

#### Deltas

```bash
sudo mxt-app --debug-dump - --self-cap-deltas
```

#### References

```bash
sudo mxt-app --debug-dump - --self-cap-refs
```

### 3) Key array（按键阵列）

#### Deltas

```bash
sudo mxt-app --debug-dump - --key-array-deltas
```

#### References

```bash
sudo mxt-app --debug-dump - --key-array-refs
```

#### Signals

```bash
sudo mxt-app --debug-dump - --key-array-signals
```

### 4) Active stylus（主动笔）

#### Deltas

```bash
sudo mxt-app --debug-dump - --active-stylus-deltas
```

#### References

```bash
sudo mxt-app --debug-dump - --active-stylus-refs
```

## 常用采集参数（一次抓几帧 / 哪个实例 / 格式）

### 抓 N 帧

```bash
sudo mxt-app --debug-dump - --frames 10
```

### 指定实例（例如 T100 instance 0）

```bash
sudo mxt-app --debug-dump - --instance 0
```

### 指定 format（0/1）

```bash
sudo mxt-app --debug-dump - --format 0
```
