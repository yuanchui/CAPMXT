#include "mxt_msg.h"
#include "mxt_state.h"
#include "mxt_work.h"
#include "mxt_config.h"
#include "mxt_i2c.h"
#include "mxt_usb_io.h"
#include "mxt_touch.h"
#include "gpio.h"
#include <stdio.h>
#include "stm32f1xx_hal.h"

int MXT_ParseMessage(char *msg_str, int pos, int max_len, uint8_t report_id, uint8_t *msg_data)
{
  uint8_t status = msg_data[1];
  
  /* T6 命令处理器消息 */
  if (report_id == g_t6_report_id) {
    pos += snprintf(msg_str + pos, max_len - pos, "  [T6 CMD]");
    if (status & 0x80) pos += snprintf(msg_str + pos, max_len - pos, " RESET");
    if (status & 0x40) pos += snprintf(msg_str + pos, max_len - pos, " OFL");
    if (status & 0x20) pos += snprintf(msg_str + pos, max_len - pos, " SIGERR");
    if (status & 0x10) pos += snprintf(msg_str + pos, max_len - pos, " CAL");
    if (status & 0x08) pos += snprintf(msg_str + pos, max_len - pos, " CFGERR");
    if (status & 0x04) pos += snprintf(msg_str + pos, max_len - pos, " COMSERR");
    /* 配置校验和 */
    uint32_t cks = msg_data[2] | (msg_data[3] << 8) | (msg_data[4] << 16);
    pos += snprintf(msg_str + pos, max_len - pos, " CKS=0x%06X", (unsigned int)cks);
  }
  /* T100 多点触控屏幕消息 (18个Report ID: 1屏幕状态 + 1保留 + 16触摸) */
  else if (report_id >= g_t100_report_id && report_id < g_t100_report_id + 18) {
    uint8_t rid_offset = report_id - g_t100_report_id;
    
    /* 第一个Report ID 是屏幕状态消息 */
    if (rid_offset == 0) {
      pos += snprintf(msg_str + pos, max_len - pos, "  [T100 SCR]");
      if (status & 0x80) pos += snprintf(msg_str + pos, max_len - pos, " DETECT");
      if (status & 0x40) pos += snprintf(msg_str + pos, max_len - pos, " SUP");
    }
    /* 第二个Report ID 是保留 */
    else if (rid_offset == 1) {
      pos += snprintf(msg_str + pos, max_len - pos, "  [T100 RSV]");
    }
    /* 后续 Report ID 是触摸状态消息 (0-15) */
    else {
      uint8_t touch_id = rid_offset - 2;  /* 触摸ID 0-15 */
      uint8_t detect = (status >> 7) & 0x01;
      uint8_t type = (status >> 4) & 0x07;
      uint8_t event = status & 0x0F;
      uint16_t x_pos = msg_data[2] | (msg_data[3] << 8);
      uint16_t y_pos = msg_data[4] | (msg_data[5] << 8);
      
      /* 触摸类型字符串 */
      const char *type_str = "???";
      switch (type) {
        case 0: type_str = "RSV"; break;
        case 1: type_str = "FNG"; break;  /* FINGER */
        case 2: type_str = "STY"; break;  /* PASSIVE STYLUS */
        case 5: type_str = "GLV"; break;  /* GLOVE */
        case 6: type_str = "LRG"; break;  /* LARGE TOUCH */
      }
      
      /* 事件字符串 */
      const char *event_str = "???";
      switch (event) {
        case 0: event_str = "NONE"; break;
        case 1: event_str = "MOVE"; break;
        case 2: event_str = "UNSUP"; break;
        case 3: event_str = "SUP"; break;
        case 4: event_str = "DOWN"; break;
        case 5: event_str = "UP"; break;
        case 6: event_str = "UNSUPSUP"; break;
        case 7: event_str = "UNSUPUP"; break;
        case 8: event_str = "DOWNSUP"; break;
        case 9: event_str = "DOWNUP"; break;
      }
      
      /* 输出格式调整为:
       * T100  [TCH1]  X=516 Y=660  STY MOVE
       * 即: "T100  [TCHn]  X=%d Y=%d  <TYPE> <EVENT>"
       */
      pos += snprintf(msg_str + pos, max_len - pos, 
                      "T100  [TCH%d]  X=%d Y=%d  %s %s%s",
                      touch_id, x_pos, y_pos,
                      type_str, event_str,
                      detect ? "" : " (no detect)");
    }
  }
  /* 其他未知消息 - 尝试解析为触摸格式 (根据状态字节特征) */
  else {
    /* 检查是否符合触摸消息特征: EVENT(0-9) 和 TYPE(0,1,2,5,6) */
    uint8_t type = (status >> 4) & 0x07;
    uint8_t event = status & 0x0F;
    
    if (event <= 9 && (type == 0 || type == 1 || type == 2 || type == 5 || type == 6)) {
      /* 可能是触摸消息 */
      uint8_t detect = (status >> 7) & 0x01;
      uint16_t x_pos = msg_data[2] | (msg_data[3] << 8);
      uint16_t y_pos = msg_data[4] | (msg_data[5] << 8);
      
      const char *type_str = "???";
      switch (type) {
        case 0: type_str = "RSV"; break;
        case 1: type_str = "FNG"; break;
        case 2: type_str = "STY"; break;
        case 5: type_str = "GLV"; break;
        case 6: type_str = "LRG"; break;
      }
      
      const char *event_str = "???";
      switch (event) {
        case 0: event_str = "NONE"; break;
        case 1: event_str = "MOVE"; break;
        case 2: event_str = "UNSUP"; break;
        case 3: event_str = "SUP"; break;
        case 4: event_str = "DOWN"; break;
        case 5: event_str = "UP"; break;
        case 6: event_str = "UNSUPSUP"; break;
        case 7: event_str = "UNSUPUP"; break;
        case 8: event_str = "DOWNSUP"; break;
        case 9: event_str = "DOWNUP"; break;
      }
      
      pos += snprintf(msg_str + pos, max_len - pos, 
                      "TCH? X=%d Y=%d  %s %s%s",
                      x_pos, y_pos,
                      type_str, event_str,
                      detect ? "" : " (no detect)");
    } else {
      /* 未知消息 */
      pos += snprintf(msg_str + pos, max_len - pos, "  [RID%d] st=0x%02X", report_id, status);
    }
  }
  
  return pos;
}


void MXT_SetChgPending(void)
{
  g_chg_pending = 1;
}


void MXT_CheckAndProcessMessages(void)
{
  /* SPI 流模式下不处理 CHG，也不触发 I2C 读消息。 */
  if (g_spi_stream_enabled != 0U) {
    g_chg_pending = 0;
    return;
  }

  // 只在模式1工作；消息输出开关仅影响文本输出，不影响 CHGNO 模式下的触点更新
  if (g_bridge_mode != BRIDGE_MODE_STRING) return;
  
  // 未使能 CHG 处理且未处于 CHGNO 流模式时跳过（默认不处理；CHGON 或 START CHGNO 后才会处理）
  if (!g_chg_process_enabled && !g_stream_chgno) return;
  
  // T37读取进行中，跳过CHG消息处理
  if (g_t37_reading) return;
  
  uint32_t current_time = HAL_GetTick();
  // 防抖: 无中断置位时 50ms 内不重复处理；有 g_chg_pending 时立即处理（响应 CHG 拉低）
  if (!g_chg_pending && (current_time - g_last_msg_time) < 50) {
    return;
  }
  
  // 检查CHG引脚状态 (低电平表示有消息)；若有中断置位或引脚为低则处理
  if (g_chg_pending || HAL_GPIO_ReadPin(CHG_EXTI3_GPIO_Port, CHG_EXTI3_Pin) == GPIO_PIN_RESET) {
    g_chg_pending = 0;
    g_last_msg_time = current_time;
    
    // 循环读取所有消息，但限制单批次最多10条
    uint8_t max_msgs = 10;  // 限制为10条，避免缓冲区过载
    uint8_t msg_count_total = 0;
    
    // 获取动态地址
    uint16_t t44_addr = MXT_GetObjectAddr(44);
    uint16_t t5_addr = MXT_GetObjectAddr(5);
    
    // 检查地址是否有效
    if (t44_addr == 0 || t5_addr == 0) {
      // 地址无效，尝试重新读取对象表
      if (g_num_objects > 0) {
        MXT_ReadObjectTable();
        t44_addr = MXT_GetObjectAddr(44);
        t5_addr = MXT_GetObjectAddr(5);
      }
      if (t44_addr == 0 || t5_addr == 0) {
        return;  // 无法获取有效地址
      }
    }
    
    while (max_msgs-- > 0) {
      // 读取消息计数 (T44动态地址)
      uint8_t msg_count = 0;
      if (MXT_I2C_Read(g_mxt_i2c_addr, t44_addr, &msg_count, 1) != 0) {
        break;  // 读取失败
      }
      
      if (msg_count == 0) {
        break;  // 无消息，退出
      }
      
      // 读取一条消息 (T5动态地址)
      uint8_t msg_data[11];
      if (MXT_I2C_Read(g_mxt_i2c_addr, t5_addr, msg_data, 11) == 0) {
        msg_count_total++;
        
        uint8_t report_id = msg_data[0];
        // 跳过无效消息 (Report ID = 0xFF 表示队列已空)
        if (report_id == 0xFF) {
          break;
        }

        /* 若处于 CHGNO 模式，则从 T100 消息中提取触点信息，供 Mode3 包附加使用 */
        if (g_stream_chgno &&
            report_id >= g_t100_report_id &&
            report_id < (uint8_t)(g_t100_report_id + 18)) {
          uint8_t rid_offset = (uint8_t)(report_id - g_t100_report_id);
          if (rid_offset >= 2) {
            uint8_t touch_id = (uint8_t)(rid_offset - 2);  /* 触点号 0-16 */
            uint8_t status = msg_data[1];
            uint8_t event  = (uint8_t)(status & 0x0F);
            uint16_t x_pos = (uint16_t)(msg_data[2] | (msg_data[3] << 8));
            uint16_t y_pos = (uint16_t)(msg_data[4] | (msg_data[5] << 8));

            TouchAction_t action = TOUCH_ACTION_NONE;
            switch (event) {
              case 1: action = TOUCH_ACTION_MOVE;   break; /* MOVE */
              case 4: action = TOUCH_ACTION_DOWN;   break; /* DOWN: 起点 */
              case 5: action = TOUCH_ACTION_UP;     break; /* UP: 抬起/离开 */
              case 9: action = TOUCH_ACTION_DOWNUP; break; /* DOWNUP: 快速点按 */
              default: break;
            }

            /* 更新最新触点信息（用于队列为空时兜底） */
            g_last_touch.id     = touch_id;
            g_last_touch.x      = x_pos;
            g_last_touch.y      = y_pos;
            g_last_touch.action = action;
            g_last_touch_valid  = 1;

            /* 将触点事件压入队列，供 MXT_SendMode3Packets 依次取出上传 */
            MXT_TouchQueuePush(touch_id, x_pos, y_pos, action);
          }
        }

        /* 文本输出仅在消息输出使能时进行，避免在 CHGNO 模式下占用带宽 */
        if (g_msg_output_enabled) {
          // 仅输出解析后的简洁格式，不再打印 MSG[...] / 十六进制原始数据
          int pos = 0;

          pos = MXT_ParseMessage(MXT_WORK_STR, pos, (int)MXT_WORK_BUF_SIZE, report_id, msg_data);
          pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "\r\n");
          MSG_BufferWrite(MXT_WORK_STR);
        }
      }
      

      
      // 再次检查CHG引脚，如果已经拉高则退出
      if (HAL_GPIO_ReadPin(CHG_EXTI3_GPIO_Port, CHG_EXTI3_Pin) == GPIO_PIN_SET) {
        break;
      }
    }
    
    // 取消总数汇总行，只保留每条消息本身
  }
}

