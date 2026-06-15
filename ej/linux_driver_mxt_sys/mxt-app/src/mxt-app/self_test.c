//------------------------------------------------------------------------------
/// \file   self_test.c
/// \brief  T25 Self Test functions
/// \author Nick Dyer
//------------------------------------------------------------------------------
// Copyright 2012 Atmel Corporation. All rights reserved.
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
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

#include "libmaxtouch/libmaxtouch.h"
#include "libmaxtouch/info_block.h"
#include "libmaxtouch/utilfuncs.h"
#include "libmaxtouch/log.h"

#include "mxt_app.h"

#define SELFTEST_TIMEOUT   10

//******************************************************************************
/// \brief Handle messages from the self test object
/// \return #mxt_rc
static int self_test_handle_messages(struct mxt_device *mxt, uint8_t *msg,
                                     void *context, uint8_t size, uint8_t msg_count)
{
  unsigned int object_type = mxt_report_id_to_type(mxt, msg[0]);
  int ret;

  mxt_verb(mxt->ctx, "收到来自 T%u 的消息", object_type);

  if (object_type == SPT_SELFTEST_T25) {
    switch (msg[1]) {
    case SELF_TEST_ALL:
      mxt_info(mxt->ctx, "PASS: 所有测试通过");
      ret = MXT_SUCCESS;
      break;
    case SELF_TEST_INVALID:
      mxt_err(mxt->ctx, "FAIL: 无效或不支持的测试命令");
      ret = MXT_ERROR_NOT_SUPPORTED;
      break;
    case SELF_TEST_TIMEOUT:
      mxt_err(mxt->ctx, "FAIL: 测试超时");
      ret = MXT_ERROR_TIMEOUT;
      break;
    case SELF_TEST_ANALOG:
      mxt_err(mxt->ctx, "FAIL: AVdd 模拟电源不存在");
      ret = MXT_ERROR_SELF_TEST_ANALOG;
      break;
    case SELF_TEST_PIN_FAULT:
      mxt_err(mxt->ctx, "FAIL: 引脚故障");
      ret = MXT_ERROR_SELF_TEST_PIN_FAULT;
      break;
    case SELF_TEST_PIN_FAULT_2:
      if (msg[3] == 0 && msg[4] == 0)
        mxt_err(mxt->ctx, "FAIL: 引脚故障 SEQ_NUM=%d，驱动屏蔽线失败");
      else if (msg[3] > 0)
        mxt_err(mxt->ctx, "FAIL: 引脚故障 SEQ_NUM=%d，X%d", msg[2], msg[3] - 1);
      else if (msg[4] > 0)
        mxt_err(mxt->ctx, "FAIL: 引脚故障 SEQ_NUM=%d，Y%d", msg[2], msg[4] - 1);

      ret = MXT_ERROR_SELF_TEST_PIN_FAULT;
      break;
    case SELF_TEST_AND_GATE:
      mxt_err(mxt->ctx, "FAIL: 与门故障");
      ret = MXT_ERROR_SELF_TEST_AND_GATE;
      break;
    case SELF_TEST_SIGNAL_LIMIT:
      mxt_err(mxt->ctx, "FAIL: T%d[%d] 信号限幅故障", msg[2], msg[3]);
      ret = MXT_ERROR_SELF_TEST_SIGNAL_LIMIT;
      break;
    case SELF_TEST_GAIN:
      mxt_err(mxt->ctx, "FAIL: 增益错误");
      ret = MXT_ERROR_SELF_TEST_GAIN;
      break;
    default:
      mxt_err(mxt->ctx, "FAIL: 状态 %02X", msg[1]);
      ret = MXT_ERROR_UNEXPECTED_DEVICE_STATE;
      break;
    }
  } else if (object_type == SPT_SELFTESTCONTROL_T10) {
  	
  	  mxt_info(mxt->ctx, "测试数据: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x", msg[1], msg[2], msg[3], msg[4], msg[5]);
  	  
  	  switch (msg[1]) {
  	  case OND_TEST_ALL_PASS:
  	    mxt_info(mxt->ctx, "PASS: 按需自检通过");
  	    ret = MXT_SUCCESS;
  	    break;
  	  case POST_TEST_ALL_PASS:
  	    mxt_info(mxt->ctx, "PASS: 上电自检（POST）通过");
  	    ret = MXT_SUCCESS;
  	    break;
  	  case BIST_TEST_ALL_PASS:
  	    mxt_info(mxt->ctx, "PASS: BIST 自检通过");
  	    ret = MXT_SUCCESS;
  	    break;
  	  case BIST_TEST_OVERRUN:
  	    mxt_info(mxt->ctx, "OVERRUN: BIST 测试周期溢出");
  	    ret = MXT_BIST_OVERRUN;
  	    break;
  	  case OND_TEST_INVALID:
  	    mxt_err(mxt->ctx, "FAIL: 无效或不支持的测试命令");
  	    ret = MXT_ERROR_NOT_SUPPORTED;
  	    break;
  	  case POST_TEST_FAILED:
  	  case BIST_TEST_FAILED:
  	  case OND_TEST_FAILED:

  	    if (msg[1] == POST_TEST_FAILED) {
  	  	  mxt_err(mxt->ctx, "FAIL: 检测到 POST 测试失败");
  	    } else if (msg[1] == BIST_TEST_FAILED) {
  	  	  mxt_err(mxt->ctx, "FAIL: 检测到 BIST 测试失败");
  	    } else {
  	    	mxt_err(mxt->ctx, "FAIL: 检测到按需自检失败");
  	    }

  	    switch(msg[2]) {
  	    case CLOCK_FAILURE:
  	  	  mxt_err(mxt->ctx, "FAIL: 发生时钟相关故障");
  	  	  ret = MXT_CLOCK_FAILURE;
  	  	  break;
  	    case FLASH_MEM_FAILURE:
  	  	  mxt_err(mxt->ctx, "发生 Flash 存储器相关故障");
  	  	  ret = MXT_FLASH_MEM_FAILURE;
  	  	  break;
  	    case RAM_MEM_FAILURE:
  	  	  mxt_err(mxt->ctx, "发生 RAM 存储器相关故障");
  	  	  ret = MXT_MEM_RAM_FAILURE;
  	  	  break;
  	    case CTE_TEST_FAILURE:
  	  	  mxt_err(mxt->ctx, "发生 CTE 相关故障");
  	  	  ret = MXT_CTE_TEST_FAILURE;
  	  	  break;
  	    case SIGNAL_LIMIT_FAILURE:
  	  	  mxt_err(mxt->ctx, "发生信号限幅相关故障");
  	  	  mxt_err(mxt->ctx, "T%d 实例[%d] 信号限幅测试失败", msg[3], msg[4]);
  	  	  ret = MXT_ERROR_SELF_TEST_SIGNAL_LIMIT;
  	  	  break;
  	    case POWER_TEST_FAILURE:
  	  	  mxt_err(mxt->ctx, "发生电源相关故障");
  	  	  ret = MXT_POWER_FAILURE;
  	  	  break;
  	    case PIN_FAULT_FAILURE:
  	  	  mxt_err(mxt->ctx, "发生引脚故障");
  	  	  ret = MXT_ERROR_SELF_TEST_PIN_FAULT;
  	  	  
  	  	  if (msg[4] == 0 && msg[5] == 0)
            mxt_err(mxt->ctx, "FAIL: 引脚故障 SEQ_NUM = %d，驱动屏蔽线失败");
      	  else if (msg[4] > 0)
        	mxt_err(mxt->ctx, "FAIL: 引脚故障 SEQ_NUM = %d，X%d", msg[3], msg[4] - 1);
      	  else if (msg[5] > 0)
        	mxt_err(mxt->ctx, "FAIL: 引脚故障 SEQ_NUM = %d，Y%d", msg[3], msg[5] - 1);

  	  	  break;
  	  
  	  	default:
  	  	  mxt_err(mxt->ctx, "测试数据: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x", msg[1], msg[2], msg[3], msg[4], msg[5]);
          	  break;
	  	}

	  break;

	default:
  	  mxt_err(mxt->ctx, "FAIL: 状态 %02X", msg[1]);
	  ret = MXT_ERROR_UNEXPECTED_DEVICE_STATE;
	  break; 
  	  }
    } else {
    ret = MXT_MSG_CONTINUE;
  }

  return ret;
}

//******************************************************************************
/// \brief Print T25 limits for enabled touch object instances
static int print_touch_object_limits(struct mxt_device *mxt, uint16_t selftest_addr,
                                     uint16_t object_type, int *touch_object, bool selftest_type)
{
  uint8_t buf[8];
  uint16_t upsiglim;
  uint16_t losiglim;
  uint16_t rangesiglim;
  uint8_t singlex_gain;
  uint8_t dualx_gain;
  bool enabled;
  int instance;
  int ret;
  uint16_t t100_addr;

  if (selftest_type == 0){
  for (instance = 0; (instance < mxt_get_object_instances(mxt, object_type));
       instance++) {
    ret = mxt_read_register(mxt, (uint8_t *)&buf,
                            mxt_get_object_address(mxt, object_type, instance), 1);
    if (ret)
      return ret;

    enabled = buf[0] & 0x01;

    mxt_info(mxt->ctx, "%s[%d] %s",
             mxt_get_object_name(object_type),
             instance,
             enabled ? "已启用":"已禁用");


    if (enabled) {
      ret = mxt_read_register(mxt, (uint8_t *)&buf,
                              selftest_addr + 2 + *touch_object * 4, 4);
      if (ret)
        return ret;

      upsiglim = (uint16_t)((buf[1] << 8u) | buf[0]);
      losiglim = (uint16_t)((buf[3] << 8u) | buf[2]);

      mxt_info(mxt->ctx, "  UPSIGLIM:%d", upsiglim);
      mxt_info(mxt->ctx, "  LOSIGLIM:%d", losiglim);
    }

    (*touch_object)++;
  }
} else {

	ret = mxt_read_register(mxt, (uint8_t *)&buf,
                            mxt_get_object_address(mxt, object_type, instance), 1);
    if (ret)
      return ret;

    enabled = buf[0] & 0x01; 

    mxt_info(mxt->ctx, "%s[%d] %s",
             mxt_get_object_name(object_type),
             instance,
             enabled ? "已启用":"已禁用");

    
    if (enabled) {
      ret = mxt_read_register(mxt, (uint8_t *)&buf,
                              selftest_addr + 1, 8); /* skip reserved byte */
      if (ret)
        return ret;

      singlex_gain = buf[0];
      dualx_gain = buf[1];
      losiglim = (uint16_t)((buf[3] << 8u) | buf[2]);
      upsiglim = (uint16_t)((buf[5] << 8u) | buf[4]);
      rangesiglim = (uint16_t)((buf[7] << 8u) | buf[6]);

      mxt_info(mxt->ctx, "  SingleX GAIN:%d", singlex_gain);
      mxt_info(mxt->ctx, "  DualX GAIN:%d", dualx_gain);
      mxt_info(mxt->ctx, "  UPSIGLIM（上限信号）：%d", upsiglim);
      mxt_info(mxt->ctx, "  LOSIGLIM（下限信号）：%d", losiglim);
      mxt_info(mxt->ctx, "  RANGESIGLIM（信号范围）：%d\n", rangesiglim);

	}
  
}

  return MXT_SUCCESS;

}

//******************************************************************************
/// \brief Print T25 limits for each enabled touch object
static int print_t25_limits(struct mxt_device *mxt, uint16_t t25_addr)
{
  int touch_object = 0;
  int ret;

  ret = print_touch_object_limits(mxt, t25_addr, TOUCH_MULTITOUCHSCREEN_T9,
                                  &touch_object, 0);
  if (ret)
    return ret;

  ret = print_touch_object_limits(mxt, t25_addr, TOUCH_MULTITOUCHSCREEN_T100,
                                  &touch_object, 0);
  if (ret)
    return ret;

  ret = print_touch_object_limits(mxt, t25_addr, TOUCH_PROXKEY_T52,
                                  &touch_object, 0);
  if (ret)
    return ret;

  ret = print_touch_object_limits(mxt, t25_addr, TOUCH_KEYARRAY_T15,
                                  &touch_object, 0);
  if (ret)
    return ret;

  ret = print_touch_object_limits(mxt, t25_addr, TOUCH_PROXIMITY_T23,
                                  &touch_object, 0);
  if (ret)
    return ret;

  return MXT_SUCCESS;
}


//******************************************************************************
/// \brief Print T25 limits for each enabled touch object
static int print_t12_limits(struct mxt_device *mxt, uint16_t t12_addr)
{
  int touch_object = 0;
  int ret;

  ret = print_touch_object_limits(mxt, t12_addr, TOUCH_MULTITOUCHSCREEN_T100,
                                  &touch_object, 1);
  if (ret)
    return ret;

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief Disable noise suppression objects
static void disable_noise_suppression(struct mxt_device *mxt)
{
  uint16_t addr;
  uint8_t disable = 0;

  addr = mxt_get_object_address(mxt, PROCG_NOISESUPPRESSION_T22, 0);
  if (addr != OBJECT_NOT_FOUND) {
    mxt_write_register(mxt, &disable, addr, 1);
  }

  addr = mxt_get_object_address(mxt, PROCG_NOISESUPPRESSION_T48, 0);
  if (addr != OBJECT_NOT_FOUND) {
    mxt_write_register(mxt, &disable, addr, 1);
  }

  addr = mxt_get_object_address(mxt, PROCG_NOISESUPPRESSION_T54, 0);
  if (addr != OBJECT_NOT_FOUND) {
    mxt_write_register(mxt, &disable, addr, 1);
  }

  addr = mxt_get_object_address(mxt, PROCG_NOISESUPPRESSION_T62, 0);
  if (addr != OBJECT_NOT_FOUND) {
    mxt_write_register(mxt, &disable, addr, 1);
  }
}

//******************************************************************************
/// \brief Run self test
int run_self_tests(struct mxt_device *mxt, uint8_t cmd, bool type)
{
  uint16_t t25_addr;
  uint16_t t10_addr;
  uint16_t t12_addr;
  uint8_t enable = 3;
  uint8_t disable_t10 = 0;
  int ret;

 if (type == 0) {

    t25_addr = mxt_get_object_address(mxt, SPT_SELFTEST_T25, 0);
    
    if (t25_addr == OBJECT_NOT_FOUND) {
      mxt_info(mxt->ctx, "未找到 T25 自检对象 ... 退出\n");
      return MXT_SUCCESS;
    }

   // Enable self test object & reporting

   mxt_info(mxt->ctx, "正在启用自检对象");
   mxt_write_register(mxt, &enable, t25_addr, 1);

   mxt_info(mxt->ctx, "正在禁用噪声抑制");
   disable_noise_suppression(mxt);

   ret = print_t25_limits(mxt, t25_addr);
   if (ret)
     return ret;

   switch (cmd) {
   case SELF_TEST_ANALOG:
     mxt_info(mxt->ctx, "正在运行模拟电源测试");
    break;
   case SELF_TEST_PIN_FAULT:
     mxt_info(mxt->ctx, "正在运行引脚故障测试");
     break;
   case SELF_TEST_PIN_FAULT_2:
     mxt_info(mxt->ctx, "正在运行引脚故障 2 测试");
     break;
   case SELF_TEST_AND_GATE:
     mxt_info(mxt->ctx, "正在运行与门测试");
     break;
   case SELF_TEST_SIGNAL_LIMIT:
     mxt_info(mxt->ctx, "正在运行信号限幅测试");
     break;
   case SELF_TEST_GAIN:
     mxt_info(mxt->ctx, "正在运行增益测试");
     break;
   case SELF_TEST_OFFSET:
     mxt_info(mxt->ctx, "正在运行偏移测试");
     break;
   case SELF_TEST_ALL:
     mxt_info(mxt->ctx, "正在运行全部测试");
     break;
   default:
     mxt_info(mxt->ctx, "向 CMD 寄存器写入 %02X", cmd);
     break;
   }

   mxt_msg_reset(mxt);

   mxt_dump_messages(mxt);

   mxt_write_register(mxt, &cmd, t25_addr + 1, 1);
 } else {

   // Enable self test object & reporting
    t10_addr = mxt_get_object_address(mxt, SPT_SELFTESTCONTROL_T10, 0);
    
    if (t10_addr == OBJECT_NOT_FOUND) {
      mxt_info(mxt->ctx, "未找到 T10 自检对象 ... 退出\n");
      return MXT_SUCCESS;
    }

   t12_addr = mxt_get_object_address(mxt, SPT_SELFTESTSIGLIMIT_T12,  0);
   t10_addr = mxt_get_object_address(mxt, SPT_SELFTESTCONTROL_T10,  0);
 	
  mxt_info(mxt->ctx, "正在启用 T10 自检对象");
  mxt_info(mxt->ctx, "禁用 POST 和 BIST 周期性测试");
   mxt_write_register(mxt, &disable_t10, t10_addr, 1);

   usleep(20000);	//* Delay 20ms */

  mxt_info(mxt->ctx, "启用 T10 报告");
   mxt_write_register(mxt, &enable, t10_addr, 1);

  mxt_info(mxt->ctx, "正在禁用噪声抑制\n");
   disable_noise_suppression(mxt);

   ret = print_t12_limits(mxt, t12_addr);
   if (ret)
     return ret;

  switch (cmd) {
  case OND_POWER_TEST:
    mxt_info(mxt->ctx, "正在运行电源测试");
    break;
  case OND_PIN_FAULT_TEST:
    mxt_info(mxt->ctx, "正在运行引脚故障测试");
    break;
  case OND_SIGNAL_LIMIT_TEST:
    mxt_info(mxt->ctx, "正在运行信号限幅测试");
    break;
  case OND_RUN_ALL_TEST:
    mxt_info(mxt->ctx, "正在运行全部测试");
    break;
  default:
    mxt_info(mxt->ctx, "向 CMD 寄存器写入 %02X", cmd);
    break;
  }

  mxt_msg_reset(mxt);

  mxt_dump_messages(mxt);

  mxt_write_register(mxt, &cmd, t10_addr + 1, 1);

 }

   return mxt_read_messages_sigint(mxt, SELFTEST_TIMEOUT, NULL, self_test_handle_messages);
}

//******************************************************************************
/// \brief Run self test
uint8_t self_test_t25_menu(struct mxt_device *mxt)
{
  int self_test;
  uint8_t cmd;
  int err;

  while (1) {
    cmd = 0;

    printf("\n自检菜单：\n\
      输入 1：运行模拟电源测试\n\
      输入 2：运行引脚故障测试\n\
      输入 3：运行引脚故障 2 测试\n\
      输入 4：运行与门测试\n\
      输入 5：运行信号限幅测试\n\
      输入 6：运行增益测试\n\
      输入 7：运行以上所有测试\n\
      输入 255：退出自检菜单\n");

    if (scanf("%d", &self_test) != 1) {
      printf("输入错误\n");
      return MXT_ERROR_BAD_INPUT;
    }

    switch(self_test) {
    case 1:
      cmd = SELF_TEST_ANALOG;
      break;
    case 2:
      cmd = SELF_TEST_PIN_FAULT;
      break;
    case 3:
      cmd = SELF_TEST_PIN_FAULT_2;
      break;
    case 4:
      cmd = SELF_TEST_AND_GATE;
      break;
    case 5:
      cmd = SELF_TEST_SIGNAL_LIMIT;
      break;
    case 6:
      cmd = SELF_TEST_GAIN;
      break;
    case 7:
      cmd = SELF_TEST_ALL;
      break;
    case 255:
      return MXT_SUCCESS;
      break;
    default:
      printf("无效选项\n");
      break;
    }

    if (cmd)
      run_self_tests(mxt, cmd, 0);

    if (mxt->conn->type == E_I2C_DEV && mxt->debug_fs.enabled == true) {

      err = debugfs_set_irq(mxt, true);

      if (err)
        mxt_dbg(mxt->ctx, "Could not disable IRQ");
    }
  }

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief Run self test
uint8_t self_test_t10_menu(struct mxt_device *mxt)
{
  int self_test;
  uint8_t cmd;
  int err;
  uint16_t t10_addr = 0x0000;


    t10_addr = mxt_get_object_address(mxt, SPT_SELFTESTCONTROL_T10, 0);
    
    if (t10_addr == OBJECT_NOT_FOUND) {
      mxt_info(mxt->ctx, "T10 Self Test Object not Found ... Exiting\n");
      return MXT_SUCCESS;
    } 

  while (1) {
    cmd = 0;

    printf("\n按需自检菜单：\n\
      输入 1：运行电源测试\n\
      输入 2：运行引脚故障测试\n\
      输入 3：运行信号限幅测试\n\
      输入 7：运行以上所有测试\n\
      输入 255：退出自检菜单\n");

    if (scanf("%d", &self_test) != 1) {
      printf("输入错误\n");
      return MXT_ERROR_BAD_INPUT;
    }

    switch(self_test) {
    case 1:
      cmd = OND_POWER_TEST;
      break;
    case 2:
      cmd = OND_PIN_FAULT_TEST;
      break;
    case 3:
      cmd = OND_SIGNAL_LIMIT_TEST;
      break;
    case 7:
      cmd = OND_RUN_ALL_TEST;
      break;
    case 255:
      return MXT_SUCCESS;
      break;
    default:
      printf("无效选项\n");
      break;
    }

    if (cmd)
      run_self_tests(mxt, cmd, 1);

  	/* Reset chip to normal config values */

  	 err = mxt_reset_chip(mxt, false, 0);
      
     if (err) {
       mxt_err(mxt->ctx, "复位出错");
     } else {
         mxt_info(mxt->ctx, "芯片已复位");
     }

    if (mxt->conn->type == E_I2C_DEV && mxt->debug_fs.enabled == true) {

      err = debugfs_set_irq(mxt, true);

      if (err)
        mxt_dbg(mxt->ctx, "Could not disable IRQ");
    }
  }

  return MXT_SUCCESS;
}


//******************************************************************************
/// \brief Run self test
uint8_t self_test_main_menu(struct mxt_device *mxt)
{
  int self_test;
  uint8_t cmd;
  int err;
  uint16_t t25_addr = 0x0000;

  while (1) {
    cmd = 0;

 printf("\n自检主菜单：\n\
      输入 1：进入 T25 自检菜单\n\
      输入 2：进入 T10 按需自检菜单\n\
      输入 255：退出自检菜单\n");

    if (scanf("%d", &self_test) != 1) {
      printf("输入错误\n");
      return MXT_ERROR_BAD_INPUT;
    }

    switch(self_test) {
    case 1:

      t25_addr = mxt_get_object_address(mxt, SPT_SELFTEST_T25, 0);
      
      if (t25_addr == OBJECT_NOT_FOUND) {
        mxt_info(mxt->ctx, "\nT25 Self Test Object not Found ... Exiting\n");
        return MXT_SUCCESS;
      }

      self_test_t25_menu(mxt);
      return MXT_SUCCESS;
      break;
    case 2:
      self_test_t10_menu(mxt);
      break;
    case 255:
      return MXT_SUCCESS;
      break;
    default:
      printf("无效选项\n");
      break;
    }
  }

  return MXT_SUCCESS;
}

