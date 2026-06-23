#include "mxt_touch.h"
#include "gpio.h"
#include "mxt_state.h"
#include "mxt_work.h"
#include "mxt_config.h"
#include "mxt_i2c.h"
#include "mxt_usb_io.h"
#include "mxt_msg.h"
#include "mxt_spi_stream.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>
#include "stm32f1xx_hal.h"

int MXT_FormatInfoBlockLine(const uint8_t id_info[7], char *buf, size_t buf_size)
{
  if (id_info == NULL || buf == NULL || buf_size == 0U) {
    return 0;
  }
  /* mXT640：byte2=版本(如 0x30=48)，byte3=build；与 xcfg VERSION/BUILD 字段一致 */
  return snprintf(buf, buf_size,
                  "Info Block: Family=0x%02X Variant=0x%02X Version=%u.0 Build=%u Matrix X=%u Y=%u NumObj=%u\r\n",
                  id_info[0], id_info[1],
                  (unsigned)id_info[2], (unsigned)id_info[3],
                  (unsigned)id_info[4], (unsigned)id_info[5], (unsigned)id_info[6]);
}

uint16_t MXT_GetObjectAddr(uint8_t obj_type)
{
    /* 如果地址为0，尝试重新读取对象表 */
    uint16_t addr = 0;
    switch (obj_type) {
        case 5:  addr = g_t5_addr; break;
        case 6:  addr = g_t6_addr; break;
        case 44: addr = g_t44_addr; break;
        case 37: addr = g_t37_addr; break;
        case 100: addr = g_t100_addr; break;
        default: addr = 0; break;
    }
    
    /* 如果地址为0且对象表已读取过，尝试重新读取对象表 */
    if (addr == 0 && g_num_objects > 0) {
        MXT_ReadObjectTable();
        switch (obj_type) {
            case 5:  addr = g_t5_addr; break;
            case 6:  addr = g_t6_addr; break;
            case 44: addr = g_t44_addr; break;
            case 37: addr = g_t37_addr; break;
            case 100: addr = g_t100_addr; break;
            default: addr = 0; break;
        }
    }
    
    return addr;
}


uint8_t MXT_ObjectTableReady(void)
{
    return (g_t44_addr != 0 && g_t5_addr != 0) ? 1 : 0;
}


static uint8_t MXT_ReadInfoBlock(void)
{
  uint8_t id_info[7];
  uint8_t result = MXT_I2C_Read(g_mxt_i2c_addr, 0x0000, id_info, 7);
  
  if(result == 0) {
    g_matrix_x_size = id_info[4];  // Matrix X通道数
    g_matrix_y_size = id_info[5];  // Matrix Y通道数
    g_num_objects = id_info[6];    // Object table elements
    
    MXT_FormatInfoBlockLine(id_info, MXT_WORK_STR, MXT_WORK_BUF_SIZE);
    MSG_BufferWrite(MXT_WORK_STR);
  }
  
  return result;
}


uint8_t MXT_ReadObjectTable(void)
{
  uint8_t obj_entry[6];  // 每个对象表项为6字节
  uint16_t obj_table_start = 0x0007;  // 对象表起始地址
  uint8_t result;
  uint8_t current_report_id = 1;  // Report ID从1开始分配
  
  // 遍历所有对象
  uint8_t max_objs = g_num_objects;
  if (max_objs == 0) max_objs = 64; /* 默认值 */
  for (uint8_t i = 0; i < max_objs; i++) {
    result = MXT_I2C_Read(g_mxt_i2c_addr, obj_table_start + i * 6, obj_entry, 6);
    if (result != 0) return result;
    
    uint8_t obj_type = obj_entry[0];
    uint16_t obj_addr = obj_entry[1] | (obj_entry[2] << 8);
    uint8_t obj_size = obj_entry[3] + 1;  // size = (size_minus_one + 1)
    uint8_t instances = obj_entry[4] + 1; // instances = (instances_minus_one + 1)
    uint8_t num_report_ids = obj_entry[5]; // 每个实例的Report ID数量
    
    // T5 Message Processor
    if (obj_type == 5) {
      g_t5_addr = obj_addr;
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Found T5 at addr=0x%04X, size=%d\r\n", g_t5_addr, obj_size);
      USB_SendString(MXT_WORK_STR);
    }
    
    // T6 Command Processor
    if (obj_type == 6) {
      g_t6_addr = obj_addr;
      g_t6_report_id = current_report_id;
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Found T6 at addr=0x%04X, RID=%d\r\n", g_t6_addr, g_t6_report_id);
      USB_SendString(MXT_WORK_STR);
    }
    
    // T44 Message Count
    if (obj_type == 44) {
      g_t44_addr = obj_addr;
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Found T44 at addr=0x%04X, size=%d\r\n", g_t44_addr, obj_size);
      USB_SendString(MXT_WORK_STR);
    }
    
    // T37 Debug Diagnostic
    if (obj_type == 37) {
      g_t37_addr = obj_addr;
      g_t37_size = obj_size;
      g_page_size = (g_t37_size >= 3) ? (g_t37_size - 2) : 1;  // 去掉 mode+page，至少 1 避免除零
      
      // 计算 pages_per_pass，确保始终设置（避免导出全 0）
      if (g_matrix_x_size > 0 && g_matrix_y_size > 0 && g_page_size > 0) {
        uint16_t data_values = g_matrix_x_size * g_matrix_y_size;
        g_pages_per_pass = (data_values * 2 + g_page_size - 1) / g_page_size;
      } else {
        g_pages_per_pass = 1;  // 至少读 1 页，避免 g_pages_per_pass==0 导致全零
      }
      
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Found T37 at addr=0x%04X, size=%d, pages=%d\r\n", 
               g_t37_addr, g_t37_size, g_pages_per_pass);
      USB_SendString(MXT_WORK_STR);
    }
    
    // T100 Multiple Touch Touchscreen
    if (obj_type == 100) {
      g_t100_addr = obj_addr;
      g_t100_size = obj_size;
      g_t100_report_id = current_report_id;
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Found T100 at addr=0x%04X, size=%d, RID=%d-%d\r\n",
               g_t100_addr, g_t100_size,
               g_t100_report_id, g_t100_report_id + instances * num_report_ids - 1);
      USB_SendString(MXT_WORK_STR);
    }
    
    // 累加Report ID
    current_report_id += instances * num_report_ids;
  }
  
  return 0;
}


void MXT_ReadT100UnknownFields(void)
{
  uint16_t t100_addr = MXT_GetObjectAddr(100);
  uint8_t t100_size = g_t100_size;

  if (t100_addr == 0) {
    USB_SendString("ERR: T100 not found in object table\r\n");
    return;
  }
  if (t100_size < 21) {
        snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: T100 size too small (%d), need >= 21\r\n", t100_size);
    USB_SendString(MXT_WORK_STR);
    return;
  }
  if (t100_size > 128) {
    USB_SendString("ERR: T100 size too large to read\r\n");
    return;
  }

  if (MXT_I2C_Read(g_mxt_i2c_addr, t100_addr, MXT_WORK_U8, t100_size) != 0) {
    USB_SendString("ERR: Read T100 data failed\r\n");
    return;
  }

    snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE,
           "T100: OBJECT_ADDRESS=%u OBJECT_SIZE=%u UNKNOWN[9]=%u UNKNOWN[20]=%u\r\n",
           t100_addr, t100_size, MXT_WORK_U8[9], MXT_WORK_U8[20]);
  USB_SendString(MXT_WORK_STR);
}


void MXT_EnableOutput(uint8_t enable)
{
  g_output_enabled = enable;
}


uint8_t MXT_IsOutputEnabled(void)
{
  return g_output_enabled;
}


void MXT_InitTouchScreen(void)
{
  // 读取ID信息块以获取X,Y尺寸
  if (MXT_ReadInfoBlock() != 0) return;
  
  // 读取对象表
  if (MXT_ReadObjectTable() != 0) return;
  
  // 诊断缓冲区使用静态内存，无需分配，直接标记初始化完成
  g_touch_inited = 1;
  g_debugctrl_applied = 0;
  (void)MXT_ApplyStartupDebugCtrl();
}

uint8_t MXT_ApplyStartupDebugCtrl(void)
{
  uint8_t debugctrl = MXT_STARTUP_DEBUGCTRL;

  if (!g_touch_inited || g_t6_addr == 0) {
    return 1;
  }

  if (g_debugctrl_applied) {
    return 0;
  }

  if (MXT_I2C_Write(g_mxt_i2c_addr, (uint16_t)(g_t6_addr + MXT_T6_DEBUGCTRL_OFFSET), &debugctrl, 1) != 0) {
    USB_SendString("ERR: Write DEBUGCTRL failed\r\n");
    return 2;
  }

  g_debugctrl_applied = 1;
  g_spi_check_requested = 1;
  USB_SendString("INFO: DEBUGCTRL enabled (Byte4=0x20 SIGNAL)\r\n");
  return 0;
}

uint8_t MXT_EnableDebugCtrl2(void)
{
  return MXT_ApplyStartupDebugCtrl();
}

uint8_t MXT_EnableDebugCtrl2Quiet(void)
{
  uint8_t debugctrl = MXT_STARTUP_DEBUGCTRL;

  if (!g_touch_inited || g_t6_addr == 0) {
    return 1;
  }

  if (MXT_I2C_Write(g_mxt_i2c_addr, (uint16_t)(g_t6_addr + MXT_T6_DEBUGCTRL_OFFSET), &debugctrl, 1) != 0) {
    return 2;
  }

  g_debugctrl_applied = 1;
  g_spi_check_requested = 1;
  return 0;
}

uint8_t MXT_DisableDebugCtrl2(void)
{
  uint8_t debugctrl = 0x00U;

  if (!g_touch_inited || g_t6_addr == 0) {
    return 1;
  }

  if (MXT_I2C_Write(g_mxt_i2c_addr, (uint16_t)(g_t6_addr + MXT_T6_DEBUGCTRL_OFFSET), &debugctrl, 1) != 0) {
    USB_SendString("ERR: Write DEBUGCTRL disable failed\r\n");
    return 2;
  }

  g_debugctrl_applied = 0;
  USB_SendString("INFO: DEBUGCTRL disabled\r\n");
  return 0;
}

uint8_t MXT_DisableDebugCtrl2Quiet(void)
{
  uint8_t debugctrl = 0x00U;

  if (!g_touch_inited || g_t6_addr == 0) {
    return 1;
  }

  if (MXT_I2C_Write(g_mxt_i2c_addr, (uint16_t)(g_t6_addr + MXT_T6_DEBUGCTRL_OFFSET), &debugctrl, 1) != 0) {
    return 2;
  }

  g_debugctrl_applied = 0;
  return 0;
}


static uint8_t MXT_ReadT37Page(uint8_t mode, uint8_t page, uint8_t quiet)
{
  uint8_t cmd;
  uint8_t read_cmd;
  uint8_t result;
  uint16_t timeout;
  
  // Step 1: 写入命令到 T6+5（增加重试和小延时，避免瞬间 NACK）
  if (page == 0) {
    cmd = mode;  // 第一页: 写入诊断模式
  } else {
    cmd = 0x01;  // 后续页: PAGE_UP
  }
  
  for (uint8_t tries = 0; tries < 10; tries++) {
    result = MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 5, &cmd, 1);
    if (result == STATUS_ADDR_NACK) {
      MXT_DelayUs(100);; /* 1ms 再试 */
      continue;
    }
    break;
  }
  if (result != 0) {
    if (quiet == 0U) {
      USB_SendString("ERR: T37 write cmd failed\r\n");
    }
    return result;
  }
  
  // Step 2: 轮询等待命令完成 (延长至 1s，1ms 步进)
  timeout = 0;
  /* 写命令后先等待 1ms 再开始轮询，避免立即读返回 NACK */
  while (timeout < 2000) { /* 1000 * 1ms = 1s */
    MXT_DelayUs(100);
    result = MXT_I2C_Read(g_mxt_i2c_addr, g_t6_addr + 5, &read_cmd, 1);
    /* 如果是 NACK，认为设备还没准备好，继续轮询 */
    if (result == STATUS_ADDR_NACK) {
      timeout++;
      continue;
    } else if (result != 0) {
      if (quiet == 0U) {
        USB_SendString("ERR: T37 read cmd status failed\r\n");
      }
      return result;
    }
    
    if (read_cmd == 0) break;  // 命令已执行完成
    timeout++;
  }
  if (timeout >= 1000) {
    if (quiet == 0U) {
      USB_SendString("ERR: T37 cmd timeout\r\n");
    }
    return 0xFF;  // 超时
  }
  
  
  // Step 3: 读取T37数据并等待mode/page匹配
  /* 命令清零后T37数据可能还未更新，需要轮询等待mode和page字段匹配 */
  for (uint16_t tries = 0; tries < 500; tries++) { /* 最长 500ms */
    result = MXT_I2C_Read(g_mxt_i2c_addr, g_t37_addr, g_t37_data, g_t37_size);
    if (result == STATUS_ADDR_NACK) {
      MXT_DelayUs(100); /* 1ms */
      continue;
    }
    if (result != 0) {
      if (quiet == 0U) {
        USB_SendString("ERR: T37 read page failed\r\n");
      }
      return result;
    }
    
    // 检查mode和page是否匹配
    if (g_t37_data[0] == mode && g_t37_data[1] == page) {
      return 0;  // 成功
    }   
      // 数据还未准备好，等待1ms后重试
      HAL_Delay(1);
  }
  
  // 超时，输出调试信息
  if (quiet == 0U) {
    snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: T37 mismatch (exp=0x%02X/%d, got=0x%02X/%d)\r\n", 
           mode, page, g_t37_data[0], g_t37_data[1]);
    USB_SendString(MXT_WORK_STR);
  }
  return 0xFE;  // 模式不匹配
}


static uint8_t MXT_ReadCompleteDiagnosticFrameInternal(uint8_t quiet)
{
  uint8_t result;
  uint16_t data_offset = 0;
  
  if (g_diag_mode == DIAG_MODE_NONE || g_diag_buffer == NULL) {
    return 0xFF;
  }
  
  /* 若 T37 未配置（从未找到或 pages=0），先刷新对象表再检查，避免导出全 0 */
  if (g_pages_per_pass == 0 || g_t37_addr == 0) {
    if (g_num_objects > 0) {
      MXT_ReadObjectTable();
    }
    if (g_pages_per_pass == 0 || g_t37_addr == 0) {
      if (quiet == 0U) {
        USB_SendString("ERR: T37 not configured (run INFO/OBJTBL first or check device)\r\n");
      }
      return 0xFD;
    }
  }
  
  // 暂停CHG消息处理，防止I2C冲突
  g_t37_reading = 1;
  
  // 清空缓冲区
  memset(g_diag_buffer, 0, g_matrix_x_size * g_matrix_y_size * sizeof(uint16_t));
  
  // 读取所有页
  for (uint8_t page = 0; page < g_pages_per_pass; page++) {
    result = MXT_ReadT37Page((uint8_t)g_diag_mode, page, quiet);
    if (result != 0) {
      if (quiet == 0U) {
        snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: T37 read failed page %d, code 0x%02X\r\n", page, result);
        USB_SendString(MXT_WORK_STR);
      }
      g_t37_reading = 0;
      return result;
    }
    
    // 提取数据 (跳过前2字节: mode和page)
    uint8_t *data = &g_t37_data[2];
    uint8_t data_len = g_page_size;
    
    // 复制到缓冲区
    for (uint8_t i = 0; i < data_len && data_offset < (g_matrix_x_size * g_matrix_y_size); i += 2) {
      g_diag_buffer[data_offset] = (uint16_t)(data[i]) | ((uint16_t)(data[i+1]) << 8);
      data_offset++;
    }
  }
  
  g_t37_reading = 0;
  
  if (quiet == 0U) {
    if (HAL_GPIO_ReadPin(CHG_EXTI3_GPIO_Port, CHG_EXTI3_Pin) == GPIO_PIN_RESET) {
      MXT_CheckAndProcessMessages();
    }
  }
  
  return 0;
}


uint8_t MXT_ReadCompleteDiagnosticFrame(void)
{
  return MXT_ReadCompleteDiagnosticFrameInternal(0U);
}


void MXT_OutputDiagnosticData(void)
{
  if (g_diag_buffer == NULL || g_matrix_x_size == 0 || g_matrix_y_size == 0) {
    return;
  }

  /*
   * 自电容输出格式需求：
   * - 数据顺序：前 20 个为 Y0-Y19，后 32 个为 X0-X31（线性列表）
   * - 输出格式：
   *   Y: v0,v1,...,v19\r\n
   *   X: v0,v1,...,v31\r\n
   * 其他模式保持原矩阵输出。
   */

  if (g_diag_mode == DIAG_MODE_SELF_DELTA || g_diag_mode == DIAG_MODE_SELF_REF || g_diag_mode == DIAG_MODE_SELF_SIGNAL) {
    int pos = 0;

    /* Y: 取前 20 个 */
    USB_SendString("Y:");
    pos = 0;
    for (uint8_t i = 0; i < 20; i++) {
      int16_t v = (int16_t)g_diag_buffer[i];
      pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "%d", (int)v);
      if (i < 19) pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, ",");
    }
    pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "\r\n");
    USB_SendString(MXT_WORK_STR);

    /* X: 紧接 32 个 */
    USB_SendString("X:");
    pos = 0;
    for (uint8_t i = 0; i < 32; i++) {
      int16_t v = (int16_t)g_diag_buffer[20 + i];
      pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "%d", (int)v);
      if (i < 31) pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, ",");
    }
    pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "\r\n\r\n");
    USB_SendString(MXT_WORK_STR);
    return;
  }

  /* 默认：互容等仍输出原始矩阵 */
  USB_SendString("=== RAW 32x20 ===\r\n");
  for (uint8_t y = 0; y < g_matrix_y_size; y++) {
    int pos = 0;

    for (uint8_t x = 0; x < g_matrix_x_size; x++) {
      uint16_t offset = y + x * g_matrix_y_size;
      int16_t value = (int16_t)g_diag_buffer[offset];

      pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "%6d", value);

      if (x < g_matrix_x_size - 1) {
        pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, ",");
      }
    }

    pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "\r\n");
    USB_SendString(MXT_WORK_STR);
  }

  USB_SendString("\r\n");
}


void MXT_OutputMapAll(void)
{
  if (g_diag_buffer == NULL || g_matrix_x_size == 0 || g_matrix_y_size == 0) {
    return;
  }
  MSG_BufferWrite("=== MAPALL ===\r\n");
  for (uint8_t y = 0; y < g_matrix_y_size; y++) {
    int pos = 0;
    for (uint8_t x = 0; x < g_matrix_x_size; x++) {
      uint16_t offset = y + x * g_matrix_y_size;
      int16_t value = (int16_t)g_diag_buffer[offset];
      pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "%6d", value);
      if (x < g_matrix_x_size - 1) {
        pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, ",");
      }
    }
    pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "\r\n");
    MSG_BufferWrite(MXT_WORK_STR);
  }
  MSG_BufferWrite("=== FRAME_END ===\r\n\r\n");
  /* 尽量排空：确保这一帧完整发出 */
  MXT_FlushMessageBuffer();
}


void MXT_OutputMap16(void)
{
  if (g_diag_buffer == NULL || g_matrix_x_size == 0 || g_matrix_y_size == 0) {
    return;
  }
  /* 取前 16 列、前 16 行，超出则按实际范围 */
  uint8_t cols = (g_matrix_x_size >= 16) ? 16 : g_matrix_x_size;
  uint8_t rows = (g_matrix_y_size >= 16) ? 16 : g_matrix_y_size;
  MSG_BufferWrite("=== MAP16 (16x16, index 0-15) ===\r\n");
  for (uint8_t out_y = 0; out_y < rows; out_y++) {
    int pos = 0;
    for (uint8_t out_x = 0; out_x < cols; out_x++) {
      uint16_t offset = out_y + out_x * g_matrix_y_size;
      int16_t value = (int16_t)g_diag_buffer[offset];
      pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "%6d", value);
      if (out_x < cols - 1) {
        pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, ",");
      }
    }
    pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "\r\n");
    MSG_BufferWrite(MXT_WORK_STR);
  }
  MSG_BufferWrite("=== FRAME_END ===\r\n\r\n");
  /* 尽量排空：确保这一帧完整发出 */
  MXT_FlushMessageBuffer();
}


void MXT_OutputMap16Transformed(uint8_t rot, uint8_t flip_mask)
{
  if (g_diag_buffer == NULL || g_matrix_x_size < 16 || g_matrix_y_size < 16) {
    return;
  }

    const char *rot_str = (rot == 1) ? "R90" : (rot == 2) ? "L90" : "";
  const char *fx = (flip_mask & 0x01) ? "X" : "";
  const char *fy = (flip_mask & 0x02) ? "Y" : "";
  if (rot_str[0] || fx[0] || fy[0]) {
    snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "=== MAP16%s%s%s (rotate then flip) ===", rot_str, fx, fy);
  } else {
    snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "=== MAP16 (rotate then flip) ===");
  }
  MSG_BufferWrite(MXT_WORK_STR);
  MSG_BufferWrite("\r\n");

  for (uint8_t out_y = 0; out_y < 16; out_y++) {
    int pos = 0;
    for (uint8_t out_x = 0; out_x < 16; out_x++) {
      /* output coords */
      uint8_t x = out_x;
      uint8_t y = out_y;

      /* 先翻转（因为我们做的是逆向映射：output -> source；翻转是自逆） */
      if (flip_mask & 0x01) x = (uint8_t)(15 - x); /* flip X: 左右镜像 */
      if (flip_mask & 0x02) y = (uint8_t)(15 - y); /* flip Y: 上下镜像 */

      /* 再反旋转（因为 forward: rotate then flip；inverse: unflip then unrotate） */
      uint8_t src_x = x;
      uint8_t src_y = y;
      if (rot == 1) {
        /* forward CW90: new(x,y)=old(y,15-x) -> inverse: old_x=y, old_y=15-x */
        src_x = y;
        src_y = (uint8_t)(15 - x);
      } else if (rot == 2) {
        /* forward CCW90: new(x,y)=old(15-y,x) -> inverse: old_x=15-y, old_y=x */
        src_x = (uint8_t)(15 - y);
        src_y = x;
      }

      uint16_t offset = src_y + src_x * g_matrix_y_size;
      int16_t value = (int16_t)g_diag_buffer[offset];
      pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "%6d", value);
      if (out_x < 15) {
        pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, ",");
      }
    }
    pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "\r\n");
    MSG_BufferWrite(MXT_WORK_STR);
  }
  MSG_BufferWrite("=== FRAME_END ===\r\n\r\n");
  /* 尽量排空：确保这一帧完整发出 */
  MXT_FlushMessageBuffer();
}


void MXT_TouchQueuePush(uint8_t id, uint16_t x, uint16_t y, TouchAction_t action)
{
  uint8_t head = g_touch_q_head;
  uint8_t tail = g_touch_q_tail;
  uint8_t next_tail = (uint8_t)((tail + 1) % TOUCH_QUEUE_SIZE);

  /* 队列满：丢弃最旧一条（head 向前移动一位），避免覆盖未定义状态 */
  if (next_tail == head) {
    g_touch_q_head = (uint8_t)((head + 1) % TOUCH_QUEUE_SIZE);
  }

  g_touch_queue[tail].id     = id;
  g_touch_queue[tail].x      = x;
  g_touch_queue[tail].y      = y;
  g_touch_queue[tail].action = action;
  g_touch_q_tail = next_tail;
}


uint8_t MXT_TouchQueuePop(TouchInfo_t *out)
{
  if (out == NULL) return 0;

  uint8_t head = g_touch_q_head;
  uint8_t tail = g_touch_q_tail;
  if (head == tail) {
    return 0; /* 队列空 */
  }

  *out = g_touch_queue[head];
  g_touch_q_head = (uint8_t)((head + 1) % TOUCH_QUEUE_SIZE);
  return 1;
}


uint16_t Map16_CalcCRC16(const uint8_t *data, uint16_t length)
{
  if (data == NULL || length == 0) return 0;
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc;
}


uint16_t CRC16_CCITT_FALSE(const uint8_t *data, uint16_t length)
{
  uint16_t wCRCin = 0xFFFFU;
  const uint16_t wCPoly = 0x1021U;

  if (data == NULL || length == 0U) {
    return 0U;
  }

  while (length--) {
    wCRCin ^= (uint16_t)(*data++) << 8;
    for (uint8_t i = 0U; i < 8U; i++) {
      if (wCRCin & 0x8000U) {
        wCRCin = (uint16_t)((wCRCin << 1) ^ wCPoly);
      } else {
        wCRCin <<= 1;
      }
    }
  }
  return wCRCin;
}


void MXT_SendMode3Packets(uint8_t rot, uint8_t flip_mask, uint8_t frame_id)
{
  if (g_diag_buffer == NULL) return;

  /* 自动适应矩阵大小，最多取 16x16 */
  uint8_t max_x = (g_matrix_x_size > 16) ? 16 : g_matrix_x_size;
  uint8_t max_y = (g_matrix_y_size > 16) ? 16 : g_matrix_y_size;

  const uint8_t header_size = 3; /* AA 10 33 */
  const uint8_t data_count = 16;
  const uint8_t base_data_bytes = data_count * 2;
  /* CHGNO 模式: 在 16*2 字节后增加 [触点号(1) | x(2) | y(2) | 动作(1)] 共 6 字节 */
  const uint8_t extra_touch_bytes = g_stream_chgno ? 6 : 0;
  const uint8_t packet_len = header_size + 1 /*len*/ + 1 /*frame*/ + 1 /*line*/
                             + base_data_bytes + extra_touch_bytes + 2 /*crc*/;
  uint8_t *buffer = MXT_WORK_U8;

  buffer[0] = 0xAA;
  buffer[1] = 0x10;
  buffer[2] = 0x33;
  buffer[3] = packet_len;
  buffer[4] = frame_id;

  for (uint8_t out_y = 0; out_y < max_y; out_y++) {
    /* 先准备一行 16 个值（变换后） */
    uint16_t vals[16];
    memset(vals, 0, sizeof(vals));

    for (uint8_t out_x = 0; out_x < max_x; out_x++) {
      uint8_t x = out_x;
      uint8_t y = out_y;
      if (flip_mask & 0x01) x = (uint8_t)(max_x - 1 - x); 
      if (flip_mask & 0x02) y = (uint8_t)(max_y - 1 - y); 
      
      uint8_t src_x = x, src_y = y;
      if (rot == 1) { 
        src_x = y;
        src_y = (uint8_t)(max_x - 1 - x);
      } else if (rot == 2) {
        src_x = (uint8_t)(max_y - 1 - y);
        src_y = x;
      }

      if (src_x < g_matrix_x_size && src_y < g_matrix_y_size) {
        vals[out_x] = (uint16_t)g_diag_buffer[src_y + src_x * g_matrix_y_size];
      }
    }

    buffer[5] = out_y; /* line id */
    for (uint8_t i = 0; i < data_count; i++) {
      buffer[6 + i * 2] = (uint8_t)((vals[i] >> 8) & 0xFF);
      buffer[7 + i * 2] = (uint8_t)(vals[i] & 0xFF);
    }

        /* CHGNO 模式: 在 CRC16 之前增加 [触点号 | x | y | 动作类型]
         * 注意：触点坐标翻转仅受 g_stream_touch_flip 控制，与 MAP16 矩阵翻转 (flip_mask) 解耦，
         * 这样 CHGNOX / CHGNOY / CHGNOXY 与 MAP16L90X 等组合时才会有可见差异。
         */
    if (g_stream_chgno) {
      uint8_t  touch_id = 0xFF;
      uint16_t tx = 0;
      uint16_t ty = 0;
      uint8_t  action = TOUCH_ACTION_NONE;

      /* 优先从队列中取出最早的一条触点事件，确保多点触摸 (TCH0/TCH1/...) 按顺序上传。
       * 若队列为空，则退回到最后一条(g_last_touch)以保持兼容旧行为。
       */
      TouchInfo_t queued;
      if (MXT_TouchQueuePop(&queued)) {
        touch_id = queued.id;
        tx = queued.x;
        ty = queued.y;
        action = (uint8_t)queued.action;
      } else if (g_last_touch_valid) {
        touch_id = g_last_touch.id;
        tx = g_last_touch.x;
        ty = g_last_touch.y;
        action = (uint8_t)g_last_touch.action;
      }

      if (touch_id != 0xFF) {
        /* 坐标翻转:
         * X翻转: x' = 830 - x
         * Y翻转: y' = 940 - y
         * 仅由 g_stream_touch_flip 控制，与 MAP16 矩阵翻转解耦。
         */
        if (g_stream_touch_flip & 0x01) {
          if (tx <= TOUCH_MAX_X) tx = (uint16_t)(TOUCH_MAX_X - tx);
        }
        if (g_stream_touch_flip & 0x02) {
          if (ty <= TOUCH_MAX_Y) ty = (uint16_t)(TOUCH_MAX_Y - ty);
        }

        /* 防止越界，限定在 [0, MAX] */
        if (tx > TOUCH_MAX_X) tx = TOUCH_MAX_X;
        if (ty > TOUCH_MAX_Y) ty = TOUCH_MAX_Y;
      }

      uint8_t idx = 6 + base_data_bytes;
      buffer[idx++] = touch_id;
      buffer[idx++] = (uint8_t)(tx & 0xFF);
      buffer[idx++] = (uint8_t)((tx >> 8) & 0xFF);
      buffer[idx++] = (uint8_t)(ty & 0xFF);
      buffer[idx++] = (uint8_t)((ty >> 8) & 0xFF);
      buffer[idx++] = action;
    }

    uint16_t crc = CRC16_CCITT_FALSE(buffer, (uint16_t)(packet_len - 2U));
    buffer[packet_len - 2] = (uint8_t)((crc >> 8) & 0xFF);
    buffer[packet_len - 1] = (uint8_t)(crc & 0xFF);

    SendResponse(buffer, packet_len);
  }
}


void MXT_SendSelfCapMode3Packets(uint8_t use_map16, uint8_t frame_id)
{
  if (g_diag_buffer == NULL) return;

  const uint8_t header_size = 3; /* AA 10 33 */
  const uint8_t data_count = 16;
  const uint8_t payload_bytes = data_count * 2;
  const uint8_t packet_len = header_size + 1 /*len*/ + 1 /*frame*/ + 1 /*line*/ + payload_bytes + 2 /*crc*/;

  uint8_t *buffer = MXT_WORK_U8;
  buffer[0] = 0xAA;
  buffer[1] = 0x10;
  buffer[2] = 0x33;
  buffer[3] = packet_len;
  buffer[4] = frame_id;

  /* 组装一个线性数组（最多 52；裁剪时最多 32） */
  int16_t linear[52];
  uint8_t linear_len = 0;

  if (use_map16) {
    /* Y[0..15] */
    for (uint8_t i = 0; i < 16; i++) {
      linear[linear_len++] = (int16_t)g_diag_buffer[i];
    }
    /* X[0..15] 位于原始 [20..35] */
    for (uint8_t i = 0; i < 16; i++) {
      linear[linear_len++] = (int16_t)g_diag_buffer[20 + i];
    }
  } else {
    /* Y[0..19] */
    for (uint8_t i = 0; i < 20; i++) {
      linear[linear_len++] = (int16_t)g_diag_buffer[i];
    }
    /* X[0..31] */
    for (uint8_t i = 0; i < 32; i++) {
      linear[linear_len++] = (int16_t)g_diag_buffer[20 + i];
    }
  }

  uint8_t total_packets = use_map16 ? 2 : 4;

  for (uint8_t pkt = 0; pkt < total_packets; pkt++) {
    buffer[5] = pkt; /* line 作为发送计数 */

    for (uint8_t i = 0; i < data_count; i++) {
      uint8_t idx = (uint8_t)(pkt * data_count + i);
      int16_t v = 0;
      if (idx < linear_len) v = linear[idx];
      /* 按 Mode3 现有约定：高字节在前 */
      buffer[6 + i * 2] = (uint8_t)(((uint16_t)v >> 8) & 0xFF);
      buffer[7 + i * 2] = (uint8_t)((uint16_t)v & 0xFF);
    }

    uint16_t crc = CRC16_CCITT_FALSE(buffer, (uint16_t)(packet_len - 2U));
    buffer[packet_len - 2] = (uint8_t)((crc >> 8) & 0xFF);
    buffer[packet_len - 1] = (uint8_t)(crc & 0xFF);

    SendResponse(buffer, packet_len);
  }
}


void MXT_TimerDiagnosticRead(void)
{
  /* SPI 流模式下让出总线，不做 I2C 诊断读取。 */
  if (g_spi_stream_enabled != 0U) {
    return;
  }

  // 只在模式1且输出使能时工作
  if (g_bridge_mode != BRIDGE_MODE_STRING || !MXT_IsOutputEnabled()) {
    return;
  }
  
  // 检查是否到达设定间隔
  uint32_t current_time = HAL_GetTick();
  if ((current_time - g_last_diag_time) >= g_diag_interval_ms) {
    g_last_diag_time = current_time;

    /* START1 模式：每次采集前先执行一次 CAL (T6+2=0x01) */
    if (g_stream_pre_cal) {
      if (!g_touch_inited) {
        MXT_InitTouchScreen();
      }
      if (g_t6_addr != 0) {
        uint8_t cal = 0x01;
        (void)MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 2, &cal, 1);
      }
    }
    
    // 读取完整帧
    if (MXT_ReadCompleteDiagnosticFrame() == 0) {
      if (g_stream_map16_hex) {
        MXT_SendMode3Packets(g_stream_rot, g_stream_flip, g_stream_frame_id++);
      } else if (g_stream_map16_char) {
        MXT_OutputMap16Transformed(g_stream_rot, g_stream_flip);
      } else {
        MXT_OutputDiagnosticData();
      }
    }
  }
}

