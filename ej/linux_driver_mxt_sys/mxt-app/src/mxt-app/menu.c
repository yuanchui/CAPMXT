//------------------------------------------------------------------------------
/// \file   menu.c
/// \brief  Menu functions for mxt-app
/// \author Srivalli Ineni & Iiro Valkonen.

#include <stdio.h>

#include <ctype.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>

#include "libmaxtouch/libmaxtouch.h"
#include "libmaxtouch/utilfuncs.h"
#include "libmaxtouch/info_block.h"
#include "libmaxtouch/msg.h"

#include "mxt_app.h"

//******************************************************************************
/// \brief Load config from file
static void load_config(struct mxt_device *mxt)
{
  char cfg_file[255];

  /* Load config file */
  printf("请输入配置文件名：");
  if (scanf("%255s", cfg_file) != 1) {
    printf("输入解析错误\n");
    return;
  }

  printf("正在尝试打开 %s...\n", cfg_file);

  if (mxt_load_config_file(mxt, cfg_file) == MXT_SUCCESS) {
    printf("配置文件上传成功\n");
  } else {
    printf("配置上传失败\n");
  }
}

//******************************************************************************
/// \brief Save config to file
static int save_config(struct mxt_device *mxt)
{
  char cfg_file[255];
  uint16_t format;

  /* Save config file */
  printf("请输入配置文件名：");
  if (scanf("%255s", cfg_file) != 1) {
    printf("输入解析错误\n");
    return MXT_ERROR_BAD_INPUT;
  }

  printf("请输入配置版本格式 0 或 3（其他值无效）：");

  if (scanf("%hu", &format) == EOF) {
    fprintf(stderr, "无法处理输入，正在退出");
    return MXT_ERROR_BAD_INPUT; 
  }

  if (mxt_save_config_file(mxt, cfg_file, format) == MXT_SUCCESS) {
    printf("配置已成功保存到文件\n");
  } else {
    printf("配置保存失败\n");
  }
}

//******************************************************************************
/// \brief Flash firmware to chip
static void flash_firmware_command(struct mxt_device *mxt)
{
  char fw_file[255];
  struct mxt_conn_info *conn = NULL;

  /* Enter firmware file */
  printf("请输入固件 .enc 文件名：");
  if (scanf("%255s", fw_file) != 1) {
    printf("输入解析错误\n");
    return;
  }

  mxt_flash_firmware(mxt->ctx, mxt, fw_file, "", conn);
}


//******************************************************************************
/// \brief Read objects according to the input value
static void read_object_command(struct mxt_device *mxt)
{
  uint16_t obj_num;
  uint8_t instance = 0;
  uint8_t obj_instance = 0;
  struct mxt_object obj;

  while(1) {
    printf("请输入要读取的对象号（Txx 的数字部分），输入 0 结束\n");
    if (scanf("%" SCNu16, &obj_num) != 1) {
      printf("输入解析错误\n");
      return;
    };

    if (obj_num == 0)
      return;

    obj_instance = mxt_get_object_instances(mxt, obj_num);
    
    if (obj_instance > 1) {
      printf("请输入对象实例号\n");
        
      if (scanf("%" SCNu8, &instance) != 1) {
        printf("输入解析错误\n");
        return;
      }
    }
    mxt_read_object(mxt, obj_num, instance, 0, 0, true);
  }
}

//******************************************************************************
/// \brief Menu function to write values to object
static void write_to_object(struct mxt_device *mxt, int obj_num, uint8_t instance)
{
  uint8_t obj_tbl_num, i;
  uint8_t *buffer;
  uint16_t start_position;
  int yn;
  uint8_t value;
  uint8_t size;

  obj_tbl_num = mxt_get_object_table_num(mxt, obj_num);
  if (obj_tbl_num == 255) {
    printf("未找到该对象\n");
    return;
  }

  buffer = (uint8_t *)calloc(MXT_SIZE(mxt->info.objects[obj_tbl_num]), sizeof(char));
  if (buffer == NULL) {
    mxt_err(mxt->ctx, "Memory error");
    return;
  }

  const char *obj_name = mxt_get_object_name(mxt->info.objects[obj_tbl_num].type);
  if (obj_name)
    printf("%s:\n", obj_name);
  else
    printf("UNKNOWN_T%d:\n", mxt->info.objects[obj_tbl_num].type);

  start_position = mxt_get_start_position(mxt->info.objects[obj_tbl_num], instance);
  size = mxt_get_object_size(mxt, obj_num);

  mxt_read_register(mxt, buffer, start_position, size);

  for(i = 0; i < size; i++) {
    printf("对象元素 %d =\t %d\n",i, *(buffer+i));
    printf("要修改这个值吗？ (1=是 / 2=否) ");
    if (scanf("%d", &yn) != 1) {
    printf("输入错误\n");
      return;
    }
    if (yn == 1) {
      printf("请输入要写入对象元素 %d 的新值\t :", i);
      if (scanf("%" SCNu8, &value) != 1) {
        printf("输入错误\n");
        return;
      }
      *(buffer+i) = value;
      printf("已写入 %d\n", value);
    }
  }

  mxt_write_register(mxt, buffer, start_position, size);
}

//******************************************************************************
/// \brief Write objects
static void write_object_command(struct mxt_device *mxt)
{
  uint16_t obj_num;
  uint8_t instance = 0;
  uint8_t obj_instance = 0;

  while(1) {
    printf("请输入要写入的对象号（Txx 的数字部分），输入 0 结束\n");
    if (scanf("%" SCNu16, &obj_num) != 1) {
      printf("输入解析错误\n");
      return;
    }

    if (obj_num == 0)
      return;

    obj_instance = mxt_get_object_instances(mxt, obj_num);

    if (obj_instance > 1) {
      printf("请输入对象实例号\n");

      if (scanf("%" SCNu8, &instance) != 1) {
        printf("输入解析错误\n");
        return;
      }
    }
    write_to_object(mxt, obj_num, instance);
  }
}

//******************************************************************************
/// \brief Print Messages
static void print_messages_command(struct mxt_device *mxt)
{
  int msgs_timeout = MSG_CONTINUOUS;
  char tmp_buf[8];

  /* Flush stdin */
#ifdef MXT_OS_WINDOWS
  (void)0;  /* no __fpurge on Windows */
#else
  __fpurge(stdin);
#endif

  printf("请输入消息打印超时时间（秒）。[默认：持续运行]\n");

  fgets(tmp_buf, sizeof(tmp_buf), stdin);
  if (sscanf(tmp_buf, "%d", &msgs_timeout) == EOF)
    printf("请按 Ctrl-C 返回主菜单。\n");

  print_raw_messages(mxt, msgs_timeout, 0);

}

//******************************************************************************
/// \brief Handle command
static bool mxt_app_command(struct mxt_device *mxt, char selection)
{
  bool exit_loop = false;

  switch(selection) {
  case 'l':
    load_config(mxt);
    break;
  case 's':
    save_config(mxt);
    break;
  case 'i':
    /* Print info block */
    printf("正在读取信息块...\n");
    mxt_print_info_block(mxt);
    /* Print config crc */
    printf("正在读取配置 CRC...\n\n");
    mxt_print_config_crc(mxt);
    break;
  case 'd':
    read_object_command(mxt);
    break;
  case 'w':
    write_object_command(mxt);
    break;
  case 'f':
    flash_firmware_command(mxt);
    break;
  case 't':
    /* Run the self-test */
    self_test_main_menu(mxt);
    break;
  case 'b':
    /* Backup the config data */
    if (mxt_backup_config(mxt, BACKUPNV_COMMAND) == MXT_SUCCESS) {
      printf("设置已成功备份到非易失存储器（NVM）\n");
    } else {
      printf("备份设置失败\n");
    }
    break;
  case 'r':
    /* Reset the chip */
    if (mxt_reset_chip(mxt, false, 0) == MXT_SUCCESS) {
      printf("已成功强制复位设备\n");
    } else {
      printf("强制复位失败\n");
    }
    break;
  case 'c':
    /* Calibrate the device*/
    if (mxt_calibrate_chip(mxt) == MXT_SUCCESS) {
      printf("已成功对所有通道执行全局重新校准\n");
    } else {
      printf("全局重新校准失败\n");
    }
    break;
  case 'm':
    /* Display raw messages */
    print_messages_command(mxt);
    break;
  case 'u':
    mxt_dd_menu(mxt);
    break;
  case 'q':
    printf("退出\n");
    exit_loop = true;
    break;
  default:
    printf("无效的菜单选项\n");
    exit_loop = true;
    break;
  }
  return exit_loop;
}

//******************************************************************************
/// \brief Menu function for mxt-app
int mxt_menu(struct mxt_device *mxt)
{
  unsigned char menu_input;
  bool exit_loop = false;
  int ret;

  printf("maXTouch 芯片命令行工具 版本: %s\n\n",
         MXT_VERSION);

  while(!exit_loop) {
    printf("请选择一个选项：\n\n"
           "输入 L：   加载配置文件 (L)oad\n"
           "输入 S：   保存配置文件 (S)ave\n"
           "输入 I：   读取信息块与配置 CRC (I)nfo\n"
           "输入 D：   读取单个对象配置 Rea(D)\n"
           "输入 W：   写入单个对象 (W)rite\n"
           "输入 T：   运行自检 sel(T)-test\n"
           "输入 F：   烧录固件到芯片 (F)lash\n"
           "输入 B：   备份配置到 NVM (B)ackup\n"
           "输入 R：   复位设备 (R)eset\n"
           "输入 C：   校准设备 (C)alibrate\n"
           "输入 M：   显示原始消息 (M)essages\n"
           "输入 U：   导出诊断数据 D(U)mp\n"
           "输入 Q：   退出 (Q)uit\n");

    ret = scanf("%1s", &menu_input);
    if (ret == 1) {
      /* force lower case */
      menu_input = tolower(menu_input);

      exit_loop = mxt_app_command(mxt, menu_input);
    } else if (ret == EOF) {
      fprintf(stderr, "Error %s\n", strerror(errno));
      return MXT_ERROR_BAD_INPUT;
    }
  }

  return MXT_SUCCESS;
}
