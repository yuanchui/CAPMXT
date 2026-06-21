/** 与 mxt-app utilfuncs.c mxt_get_object_name 对齐的对象类型名称 */
const OBJECT_NAMES: Record<number, string> = {
  0: 'T0-RESERVED_T0',
  1: 'T1-RESERVED_T1',
  2: 'T2-加密状态',
  3: 'T3-调试参考值',
  4: 'T4-调试信号',
  5: 'T5-消息处理器',
  6: 'T6-命令处理器',
  7: 'T7-功耗配置',
  8: 'T8-采集配置',
  9: 'T9-多点触摸屏',
  10: 'T10-自检控制',
  11: 'T11-自检引脚故障',
  12: 'T12-自检信号限制',
  15: 'T15-触摸按键阵列',
  18: 'T18-通信配置',
  22: 'T22-噪声抑制',
  37: 'T37-诊断数据',
  38: 'T38-用户数据',
  44: 'T44-消息计数',
  53: 'T53-数据源',
  100: 'T100-多点触摸屏',
  107: 'T107-主动笔',
  108: 'T108-自电容噪声抑制',
  109: 'T109-自电容全局配置',
  110: 'T110-自电容调参',
  111: 'T111-自电容配置',
  112: 'T112-自电容握持抑制',
  132: 'T132-消息过滤',
  141: 'T141-忽略节点',
  145: 'T145-忽略节点控制',
  148: 'T148-噪声均衡数据',
  160: 'T160-MC通信配置',
  170: 'T170-事件计数'
};

export function getMxtObjectName(type: number): string | null {
  return OBJECT_NAMES[type] || null;
}

export function isVolatileMxtObject(type: number): boolean {
  return [3, 4, 5, 6, 37, 44, 53, 160].includes(type);
}
