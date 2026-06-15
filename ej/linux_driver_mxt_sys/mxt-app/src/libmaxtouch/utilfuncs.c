//------------------------------------------------------------------------------
/// \file   utilfuncs.c
/// \brief  Utility functions for Linux maXTtouch app.
/// \author Iiro Valkonen.
//------------------------------------------------------------------------------
// Copyright 2011 Atmel Corporation. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
//    2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY ATMEL ''AS IS'' AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
// EVENT SHALL ATMEL OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
// OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include <getopt.h>

#include "libmaxtouch.h"
#include "utilfuncs.h"

#define BUF_SIZE 1024
#define BYTETOBINARYPATTERN "%d%d%d%d %d%d%d%d"
#define BYTETOBINARY(byte)  \
  (byte & 0x80 ? 1 : 0), \
  (byte & 0x40 ? 1 : 0), \
  (byte & 0x20 ? 1 : 0), \
  (byte & 0x10 ? 1 : 0), \
  (byte & 0x08 ? 1 : 0), \
  (byte & 0x04 ? 1 : 0), \
  (byte & 0x02 ? 1 : 0), \
  (byte & 0x01 ? 1 : 0)

//******************************************************************************
/// \brief Print out info block
void mxt_print_info_block(struct mxt_device *mxt)
{
  int i;
  int report_id = 1;
  int report_id_start, report_id_end;
  struct mxt_object obj;
  struct mxt_id_info *id = mxt->info.id;

  /* Show the Version Info */
  printf("\nFamily: %u Variant: %u Firmware V%u.%u.%02X Objects: %u\n",
         id->family, id->variant,
         id->version >> 4,
         id->version & 0x0F,
         id->build, id->num_objects);

  printf("Matrix size: X%uY%u\n",
         id->matrix_x_size, id->matrix_y_size);
  /* Show Information Block CRC */
  printf("Information Block CRC: 0x%06X\n\n", mxt->info.crc);
	
  /* Show the object table */
  printf("类型 起始 地址 长度 实例 报告ID 名称\n");
  printf("-----------------------------------------------------------------\n");
  for (i = 0; i < id->num_objects; i++) {
    obj = mxt->info.objects[i];

    if (obj.num_report_ids > 0) {
      report_id_start = report_id;
      report_id_end = report_id_start + obj.num_report_ids * MXT_INSTANCES(obj) - 1;
      report_id = report_id_end + 1;
    } else {
      report_id_start = 0;
      report_id_end = 0;
    }

    printf("T%-3u %4u  %4u    %2u       %2u-%-2u   ",
           obj.type,
           mxt_get_start_position(obj, 0),
           MXT_SIZE(obj),
           MXT_INSTANCES(obj),
           report_id_start, report_id_end);

    const char *obj_name = mxt_get_object_name(obj.type);
    if (obj_name)
      printf("%s\n", obj_name);
    else
      printf("T%d-未知对象\n", obj.type);
  }

  printf("\n");
}

//******************************************************************************
/// \brief Convert object type to object name
/// \return null terminated string, or NULL for object not found
const char *mxt_get_object_name(uint8_t objtype)
{
  switch(objtype) {
  case 0: return "T0-RESERVED_T0";
  case 1: return "T1-RESERVED_T1";
  case 2: return "T2-加密状态";
  case 3: return "T3-调试参考值";
  case 4: return "T4-调试信号";
  case 5: return "T5-消息处理器";
  case 6: return "T6-命令处理器";
  case 7: return "T7-功耗配置";
  case 8: return "T8-采集配置";
  case 9: return "T9-多点触摸屏";
  case 10: return "T10-自检控制";
  case 11: return "T11-自检引脚故障";
  case 12: return "T12-自检信号限制";
  case 13: return "T13-触摸X-Wheel";
  case 14: return "T14-按键阈值";
  case 15: return "T15-触摸按键阵列";
  case 16: return "T16-信号滤波";
  case 17: return "T17-线性化表";
  case 18: return "T18-通信配置";
  case 19: return "T19-GPIO/PWM";
  case 20: return "T20-边缘握持抑制";
  case 21: return "T21-PWM";
  case 22: return "T22-噪声抑制";
  case 23: return "T23-接近感应";
  case 24: return "T24-单点触摸手势";
  case 25: return "T25-自检";
  case 26: return "T26-CTE范围调试";
  case 27: return "T27-两点触摸手势";
  case 28: return "T28-CTE配置";
  case 29: return "T29-GPI";
  case 30: return "T30-Gate";
  case 31: return "T31-触摸按键集";
  case 32: return "T32-触摸X滑块集";
  case 33: return "T33-RESERVED_T33";
  case 34: return "T34-消息块";
  case 35: return "T35-原型";
  case 36: return "T36-RESERVED_T36";
  case 37: return "T37-诊断数据";
  case 38: return "T38-用户数据";
  case 39: return "T39-SPARE_T39";
  case 40: return "T40-握持抑制";
  case 41: return "T41-手掌抑制";
  case 42: return "T42-触摸抑制";
  case 43: return "T43-数字化仪";
  case 44: return "T44-消息计数";
  case 45: return "T45-虚拟按键";
  case 46: return "T46-CTE配置";
  case 47: return "T47-触摸笔";
  case 48: return "T48-噪声抑制";
  case 49: return "T49-双脉冲";
  case 50: return "T50-SPARE_T50";
  case 51: return "T51-索尼自定义";
  case 52: return "T52-接近感应按键";
  case 53: return "T53-数据源";
  case 54: return "T54-噪声抑制";
  case 55: return "T55-自适应阈值";
  case 56: return "T56-无屏蔽层";
  case 57: return "T57-额外触摸屏数据";
  case 58: return "T58-额外噪声抑制控制";
  case 59: return "T59-快速漂移";
  case 61: return "T61-定时器";
  case 62: return "T62-噪声抑制";
  case 63: return "T63-主动笔";
  case 64: return "T64-参考值重载";
  case 65: return "T65-Lens Bending";
  case 66: return "T66-Golden References";
  case 67: return "T67-自定义手势";
  case 68: return "T68-串行数据命令";
  case 69: return "T69-手掌手势";
  case 70: return "T70-动态配置控制器";
  case 71: return "T71-动态配置容器";
  case 72: return "T72-噪声抑制";
  case 73: return "T73-区域指示";
  case 74: return "T74-简单手势";
  case 75: return "T75-运动传感";
  case 76: return "T76-运动手势";
  case 77: return "T77-CTE扫描配置";
  case 78: return "T78-手套检测";
  case 79: return "T79-触摸事件触发";
  case 80: return "T80-重传补偿";
  case 81: return "T81-解锁手势";
  case 82: return "T82-噪声抑制扩展";
  case 83: return "T83-环境光传感";
  case 84: return "T84-手势处理器";
  case 85: return "T85-主动笔电源";
  case 86: return "T86-主动笔噪声抑制";
  case 87: return "T87-主动笔数据";
  case 88: return "T88-主动笔接收";
  case 89: return "T89-主动笔发送";
  case 90: return "T90-主动笔窗口";
  case 91: return "T91-自定义数据配置";
  case 92: return "T92-符号手势";
  case 93: return "T93-触摸序列记录";
  case 95: return "T95-PTC配置";
  case 96: return "T96-PTC调谐参数";
  case 97: return "T97-PTC按键";
  case 98: return "T98-PTC噪声抑制";
  case 99: return "T99-按键手势";
  case 100: return "T100-多点触摸屏";
  case 101: return "T101-触摸屏悬停";
  case 102: return "T102-自容悬停CTE配置";
  case 103: return "T103-自容悬停噪声抑制";
  case 104: return "T104-辅助触摸配置";
  case 105: return "T105-驱动板悬停配置";
  case 106: return "T106-主动笔MMB配置";
  case 107: return "T107-主动笔";
  case 108: return "T108-自容噪声抑制";
  case 109: return "T109-自容全局配置";
  case 110: return "T110-自容调谐参数";
  case 111: return "T111-自容配置";
  case 112: return "T112-自容握持抑制";
  case 113: return "T113-接近测量配置";
  case 114: return "T114-主动笔测量配置";
  case 115: return "T115-符号手势";
  case 116: return "T116-符号手势配置";
  case 117: return "T117-数据容器";
  case 118: return "T118-数据容器控制";
  case 121: return "T121-传感器校正";
  case 129: return "T129-悬停手势";
  case 132: return "T132-消息过滤器";
  case 133: return "T133-自容电压调制";
  case 141: return "T141-忽略节点";
  case 144: return "T144-消息计数";
  case 145: return "T145-忽略节点控制";
  case 148: return "T148-噪声均衡数据";
  case 150: return "T150-形状搜索";
  case 151: return "T151-形状搜索数据";
  case 155: return "T155-智能触觉触发";
  case 156: return "T156-智能互容配置";
  case 160: return "T160-多芯片通信配置";
  case 161: return "T161-峰值恢复";
  case 170: return "T170-事件计数器";
  case 254: return "T254-16位信息块";
  case 220: return "T220-原型";
  case 221: return "T221-原型";
  case 222: return "T222-原型";
  case 223: return "T223-原型";
  case 224: return "T224-原型";
  case 225: return "T225-原型";
  case 226: return "T226-原型";
  case 227: return "T227-原型";
  case 228: return "T228-原型";
  case 229: return "T229-原型";
  case 230: return "T230-原型";
  case 231: return "T231-原型";
  case 232: return "T232-原型";
  case 233: return "T233-原型";
  case 234: return "T234-原型";
  case 235: return "T235-原型";
  case 236: return "T236-原型";
  case 237: return "T237-原型";
  case 238: return "T238-原型";
  case 239: return "T239-原型";
  case 255: return "T255-RESERVED_T255";
  default:
    return NULL;
  }
}
//******************************************************************************
/// \brief Menu function to read values from object
/// \return #mxt_rc
int mxt_read_object(struct mxt_device *mxt, uint16_t object_type,
                    uint8_t instance, uint16_t address,
                    size_t count, bool format)
{
  uint8_t *databuf;
  uint16_t object_address = 0;
  uint16_t i;
  int ret;

  if (object_type > 0) {
    object_address = mxt_get_object_address(mxt, object_type, instance);
    if (object_address == OBJECT_NOT_FOUND) {
      printf("没有该对象\n");
      return MXT_ERROR_OBJECT_NOT_FOUND;
    }

    mxt_dbg(mxt->ctx, "T%u address:%u offset:%u", object_type,
            object_address, address);
    address = object_address + address;

    if (count == 0) {
      count = mxt_get_object_size(mxt, object_type);
    }
  } else if (count == 0) {
    mxt_err(mxt->ctx, "缺少长度信息");
    return MXT_ERROR_BAD_INPUT;
  }

  databuf = (uint8_t *)calloc(count, sizeof(uint8_t));
  if (databuf == NULL) {
    mxt_err(mxt->ctx, "内存分配失败");
    return MXT_ERROR_NO_MEM;
  }

  ret = mxt_read_register(mxt, databuf, address, count);
  if (ret) {
    printf("读取错误\n");
    goto free;
  }

  if (format) {
    if (object_type > 0) {
      const char *obj_name = mxt_get_object_name(object_type);
      if (obj_name)
        printf("%s\n\n", obj_name);
      else
        printf("T%d-未知对象\n\n", object_type);
    }

    for (i = 0; i < count; i++) {
      printf("%02d:\t0x%02X\t%3d\t" BYTETOBINARYPATTERN "\n",
             address - object_address + i,
             databuf[i],
             databuf[i],
             BYTETOBINARY(databuf[i]));
    }
  } else {
    for (i = 0; i < count; i++) {
      printf("%02X ", databuf[i]);
    }

    printf("\n");
  }

  ret = MXT_SUCCESS;

free:
  free(databuf);
  return ret;
}


//******************************************************************************
/// \brief Handles parsing of the write parameters
/// \return #mxt_rc
int mxt_handle_write_cmd(struct mxt_device *mxt, const uint16_t type,
                         uint16_t count, const uint8_t inst, uint16_t address,
                         int argc, char *argv[])
{
  uint16_t obj_addr = 0;
  unsigned char databuf[BUF_SIZE];
  unsigned char *p_databuf = databuf;
  int ret = MXT_SUCCESS;

  if (type > 0) {
    obj_addr = mxt_get_object_address(mxt, type, inst);
    if (obj_addr == OBJECT_NOT_FOUND) {
      fprintf(stderr, "没有该对象\n");
      return MXT_ERROR_OBJECT_NOT_FOUND;
    }

    mxt_verb(mxt->ctx, "T%u address:%u offset:%u", type, obj_addr, address);
    address = obj_addr + address;

    if (count == 0) {
      count = mxt_get_object_size(mxt, type);
    }
  } else if (count == 0) {
    fprintf(stderr, "参数不足！\n");
    return MXT_ERROR_BAD_INPUT;
  }

  /* Parse unprocessed arguments */
  while (optind < argc) {
    ret = mxt_convert_hex(argv[optind++], p_databuf, &count, sizeof(databuf) - (p_databuf - databuf));

    if (ret || count == 0) {
      fprintf(stderr, "十六进制转换错误\n");
      return MXT_ERROR_BAD_INPUT;
    }
    p_databuf += count;
  }

  ret = mxt_write_register(mxt, databuf, address, (p_databuf - databuf));
  if (ret)
    fprintf(stderr, "写入错误\n");

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief Convert hex nibble to digit
/// \return #mxt_rc
static int to_digit(const char hex, char *const decimal)
{
  if (hex >= '0' && hex <= '9')
    *decimal = hex - '0';
  else if (hex >= 'A' && hex <= 'F')
    *decimal = hex - 'A' + 10;
  else if (hex >= 'a' && hex <= 'f')
    *decimal = hex - 'a' + 10;
  else {
    *decimal = 0;
    return MXT_ERROR_BAD_INPUT;
  }

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief Convert ASCII buffer containing hex digits to binary
/// \return #mxt_rc
int mxt_convert_hex(char *hex, unsigned char *databuf,
                    uint16_t *count, unsigned int buf_size)
{
  unsigned int pos = 0;
  uint16_t datapos = 0;
  char highnibble;
  char lownibble;
  char high_dec_nibble;
  char low_dec_nibble;
  int ret = MXT_SUCCESS;

  while (1) {
    highnibble = *(hex + pos);
    lownibble = *(hex + pos + 1);

    /* end of string */
    if (highnibble == '\0' || highnibble == '\n')
      break;

    /* uneven number of hex digits */
    if (lownibble == '\0' || lownibble == '\n') {
      ret = MXT_ERROR_BAD_INPUT;
      break;
    }

    if (pos > buf_size) {
      ret = MXT_ERROR_NO_MEM;
      break;
    }

    ret = to_digit(highnibble, &high_dec_nibble);
    if (ret)
      break;

    ret = to_digit(lownibble, &low_dec_nibble);
    if (ret)
      break;

    *(databuf + datapos) = (high_dec_nibble << 4) | low_dec_nibble;
    datapos++;
    pos += 2;
  }

  *count = datapos;
  return ret;
}

//******************************************************************************
/// \brief Output timestamp to stream with millisecond accuracy
/// \param stream pointer to FILE object
/// \param date   output date, true or false
/// \return #mxt_rc
int mxt_print_timestamp(FILE *stream, bool date)
{
  struct timeval tv;
  time_t nowtime;
  struct tm *nowtm;
  char tmbuf[64];
  int ret;

  gettimeofday(&tv, NULL);
  nowtime = tv.tv_sec;
  nowtm = localtime(&nowtime);

  if (date) {
    strftime(tmbuf, sizeof(tmbuf), "%c", nowtm);
    ret = fprintf(stream, "%s,", tmbuf);
  } else {
    strftime(tmbuf, sizeof(tmbuf), "%H:%M:%S", nowtm);
    ret = fprintf(stream, "%s.%06ld,", tmbuf, tv.tv_usec);
  }

  return (ret < 0) ? MXT_ERROR_IO : MXT_SUCCESS;
}
