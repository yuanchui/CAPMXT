//------------------------------------------------------------------------------
/// \file   mxt_app.c
/// \brief  Command line tool for Atmel maXTouch chips.
/// \author Nick Dyer
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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#ifdef MXT_OS_WINDOWS
# include <windows.h>
#endif

#include "libmaxtouch/libmaxtouch.h"
#include "libmaxtouch/log.h"
#include "libmaxtouch/utilfuncs.h"
#include "libmaxtouch/info_block.h"
#include "libmaxtouch/serial/serial_device.h"
#include "serial_data.h"
#include "libmaxtouch/msg.h"

#include "broken_line.h"
#include "sensor_variant.h"
#include "mxt_app.h"
#include "freq_sweep.h"

#define BUF_SIZE 1024

//******************************************************************************
/// \brief Initialize mXT device and read the info block
/// \return #mxt_rc
static int mxt_init_chip(struct libmaxtouch_ctx *ctx, struct mxt_device **mxt,
                         struct mxt_conn_info **conn)
{
  int ret;

  ret = mxt_scan(ctx, conn, false);
  
  if (ret == MXT_ERROR_NO_DEVICE) {
    mxt_err(ctx, "无法找到设备");
    return ret;
  } else if (ret) {
    mxt_err(ctx, "查找设备失败");
    return ret;
  }

  ret = mxt_new_device(ctx, *conn, mxt);
  if (ret)
    return ret;

#ifdef HAVE_LIBUSB
  if ((*mxt)->conn->type == E_USB && usb_is_bootloader(*mxt)) {
    mxt_free_device(*mxt);
    mxt_err(ctx, "USB 设备处于 bootloader 模式");
    return MXT_ERROR_UNEXPECTED_DEVICE_STATE;
  }
#endif
  ret = mxt_get_info(*mxt);
  if (ret)
    return ret;

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief Print usage for mxt-app
static void print_usage(char *prog_name)
{
  fprintf(stderr, "Atmel maXTouch 芯片命令行工具 版本: %s\n\n"
          "用法: %s [命令] [选项]\n\n"
          "不带选项运行时，进入交互菜单界面。\n\n"
          "通用命令:\n"
          "  -h [--help]                : 显示本帮助并退出\n"
          "  -i [--info]                : 打印设备信息\n"
          "  -M [--messages] [TIMEOUT]  : 打印消息（持续 TIMEOUT 秒）\n"
          "  -F [--msg-filter] TYPE     : 按对象 TYPE 过滤消息\n"
          "  --reset                    : 复位设备\n"
          "  --calibrate                : 发送校准命令\n"
          "  -g                         : 保存 golden references\n"
          "  --self-cap-tune-config     : 将自容参数调谐结果写入当前配置\n"
          "  --self-cap-tune-nvram      : 将自容参数调谐结果写入 NVRAM\n"
          "  --version                  : 打印版本\n"
          "  --block-size BLOCKSIZE     : 设置 I2C 传输最大块大小（默认 %d）\n"
          "\n"
          "配置文件命令:\n"
          "  --load FILE                : 从 FILE 加载配置并写入（.xcfg 或 OBP_RAW）\n"
          "  --save FILE                : 读取配置并保存到 FILE（.xcfg 或 OBP_RAW）\n"
          "  --format N                 : 保存为指定格式 N（0 或 3）\n"
          "  --backup[=COMMAND]         : 备份配置到 NVRAM\n"
          "  --checksum FILE            : 校验 .xcfg 或 OBP_RAW 配置校验和\n"
          "\n"
          "寄存器/对象 读写命令:\n"
          "  -R [--read]                : 从对象读取\n"
          "  -W [--write]               : 写入对象\n"
          "  -n [--count] COUNT         : 读/写 COUNT 个寄存器\n"
          "  -f [--format]              : 格式化寄存器输出\n"
          "  -I [--instance] INSTANCE   : 选择对象实例 INSTANCE\n"
          "  -r [--register] REGISTER   : 从 REGISTER 开始（与 TYPE 一起使用时为偏移）\n"
          "  -T [--type] TYPE           : 选择对象类型 TYPE\n"
          "  --zero                     : 将全部配置清零\n"
          "\n"
          "TCP Socket 命令:\n"
          "  -C [--bridge-client] HOST  : 通过 TCP 连接到 HOST\n"
          "  -S [--bridge-server]       : 启动 TCP 服务器\n"
          "  -p [--port] PORT           : TCP 端口（默认 4000）\n"
          "\n"
          "Bootloader 命令:\n"
          "  --bootloader-version       : 查询 bootloader 版本\n"
          "  --flash FIRMWARE           : 向 bootloader 发送 FIRMWARE\n"
          "  --reset-bootloader         : 复位设备并进入 bootloader 模式\n"
          "  --firmware-version VERSION : 烧录前后检查固件 VERSION\n"
          "\n"
          "T68 串行数据命令:\n"
          "  --t68-file FILE            : 上传 FILE\n"
          "  --t68-datatype DATATYPE    : 选择 DATATYPE\n"
          "\n"
          "T25 自检命令:\n"
          "  -t [--test]                : 运行所有自检\n"
          "  -tXX [--test=XX]           : 运行单项自检（向 CMD 寄存器写入 XX）\n"
          "\n"
          "T10 按需测试命令:\n"
          "  --odtest                   : 运行所有按需自检\n"
          "\n"
          "T37 诊断数据命令:\n"
          "  --debug-dump FILE          : 抓取诊断数据并保存到 FILE（FILE='-' 输出到 stdout）\n"
          "  --frames N                 : 抓取 N 帧\n"
          "  --instance INSTANCE        : 选择对象实例 INSTANCE\n"
	  "  --format 0/1               : 使用格式 0 或 1 抓取\n"
          "  --references               : 抓取 references 数据\n"
          "  --self-cap-signals         : 抓取自容 signals\n"
          "  --self-cap-deltas          : 抓取自容 deltas\n"
          "  --self-cap-refs            : 抓取自容 references\n"
	  "  --key-array-deltas         : 抓取 key array deltas\n"
	  "  --key-array-refs           : 抓取 key array references\n"
	  "  --key-array-signals        : 抓取 key array signals\n"
          "  --active-stylus-deltas     : 抓取主动笔 deltas\n"
          "  --active-stylus-refs       : 抓取主动笔 references\n"
          "\n"
          "T72/T108 频率扫描工具:\n"
          "  --freq-sweep FILE          : 输入测试参数文件\n"
          "  -o [--output] FILE         : 扫描结果输出文件\n"
          "\n"
          "断线检测命令:\n"
          "  --broken-line              : 运行断线检测\n"
          "  --dualx                    : X 线为双连接\n"
          "  --x-center-threshold N     : X 中心阈值（百分比）\n"
          "  --x-border-threshold N     : X 边缘阈值（百分比）\n"
          "  --y-center-threshold N     : Y 中心阈值（百分比）\n"
          "  --y-border-threshold N     : Y 边缘阈值（百分比）\n"
          "  --pattern PATTERN          : 传感器图案（ITO 或 XSense）\n"
          "\n"
          "Sensor Variant 算法命令:\n"
          "  --sensor-variant           : 执行 Sensor Variant 算法\n"
          "  --dualx                    : X 线为双连接\n"
          "  --fail-if-any              : 任一缺陷即判失败\n"
          "  --max-defects N            : 最大连续缺陷数\n"
          "  --upper-limit N            : 回归上限（%%）\n"
          "  --lower-limit N            : 回归下限（%%）\n"
          "  --matrix-size N            : 允许的矩阵尺寸\n"
          "\n"
          "设备连接选项:\n"
          "  -q [--query]               : 扫描设备\n"
          "  -d [--device] DEVICESTRING : 选择设备（DEVICESTRING 为 --query 输出）\n\n"
          "示例:\n"
          "  -d i2c-dev:ADAPTER:ADDRESS : 原生 i2c 设备，例如 \"i2c-dev:2-004a\"\n"
#ifdef HAVE_LIBUSB
          "  -d usb:BUS-DEVICE          : USB 设备，例如 \"usb:001-003\"\n"
          "  -d usb:BUS-DEVICE-ADDRESS  : USB 设备，例如 \"usb:001-003-4a\"\n"
          "  -d serial:PORT             : 串口设备，例如 \"serial:COM5\" 或 \"serial:/dev/ttyACM0\"\n"
          "  -d serial:proxy:HOST:PORT  : 复用已打开串口（经 serial-app TCP 代理）\n"
          "  --serial-test              : 测试串口/代理连接（mode0 + FIND_IIC）\n"
#endif
          "  -d sysfs:PATH              : sysfs 接口\n"
          "  -d hidraw:PATH             : hidraw 设备，例如 \"hidraw:/dev/hidraw0\"\n"
          "\n"
#ifdef HAVE_LIBUSB
          "5030 Bridge Board 命令:\n"
          "  --bridge-config 0xnn       : 0xnn 为 maXTouch 从地址，并保存到 EEPROM\n"
          "  --switch-fast              : 切换到 QRG 接口模式（无 USBHID reports）\n"
          "  --switch-parallel          : 切换到 HID 并行 digitizer 模式\n"
#endif
          "调试选项:\n"
          "  -v [--verbose] LEVEL       : 设置调试级别\n",
          MXT_VERSION, prog_name, I2C_DEV_MAX_BLOCK);
  fflush(stderr);
  fflush(stdout);
}

//******************************************************************************
/// \brief Main function for mxt-app
int main (int argc, char *argv[])
{
  int ret;
  int c;

#ifdef MXT_OS_WINDOWS
  /* 控制台使用 UTF-8，与源码编码一致，避免中文乱码 */
  SetConsoleOutputCP(65001);
  SetConsoleCP(65001);
  /* 确保在 Windows 控制台下立即输出，避免无显示 */
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
#endif
  int msgs_timeout = MSG_CONTINUOUS;
  bool msgs_enabled = false;
  uint8_t backup_cmd = BACKUPNV_COMMAND;
  unsigned char self_test_cmd = SELF_TEST_ALL;
  unsigned char ondemand_test_cmd = OND_RUN_ALL_TEST;
  uint16_t address = 0;
  uint16_t count = 0;
  struct mxt_conn_info *conn = NULL;
  uint16_t object_type = 0;
  uint16_t msg_filter_type = 0;
  uint8_t instance = 0;
  uint8_t verbose = 2;
  uint16_t t37_frames = 1;
  uint8_t t37_file_attr = 0;   /* 0 - write, 1 - append */
  uint8_t t37_mode = DELTAS_MODE;
  uint8_t bi2c_addr = 0x4a;
  uint8_t format = 0;
  uint16_t port = 4000;
  int i2c_block_size = I2C_DEV_MAX_BLOCK;
  uint8_t t68_datatype = 1;
  unsigned char databuf;
  char strbuf2[BUF_SIZE];
  char strbuf[BUF_SIZE];
  bool dualx = false;
  struct broken_line_options bl_opts = {0};
  bl_opts.pattern = BROKEN_LINE_PATTERN_ITO;
  bl_opts.x_center_threshold = BROKEN_LINE_DEFAULT_THRESHOLD;
  bl_opts.x_border_threshold = BROKEN_LINE_DEFAULT_THRESHOLD;
  bl_opts.y_center_threshold = BROKEN_LINE_DEFAULT_THRESHOLD;
  bl_opts.y_border_threshold = BROKEN_LINE_DEFAULT_THRESHOLD;
  struct sensor_variant_options sv_opts = {0};
  struct freq_sweep_options fs_opts = {0};
  fs_opts.freq_start = 1;
  fs_opts.freq_end = 255;
  sv_opts.max_defects = 0;
  sv_opts.upper_limit = 15;
  sv_opts.lower_limit = 15;
  sv_opts.matrix_size = 0;
  strbuf[0] = '\0';
  strbuf2[0] = '\0';
  mxt_app_cmd cmd = CMD_NONE;

  while (1) {
    int option_index = 0;

    static struct option long_options[] = {
      {"backup",           optional_argument, 0, 0},
      {"block-size",       required_argument, 0, 0},
      {"bootloader-version", no_argument,     0, 0},
      {"bridge-client",    required_argument, 0, 'C'},
      {"calibrate",        no_argument,       0, 0},
      {"checksum",         required_argument, 0, 0},
      {"debug-dump",       required_argument, 0, 0},
      {"device",           required_argument, 0, 'd'},
      {"freq-sweep",       required_argument, 0, 0},
      {"t68-file",         required_argument, 0, 0},
      {"t68-datatype",     required_argument, 0, 0},
      {"msg-filter",       required_argument, 0, 'F'},
      {"format",           required_argument, 0, 'f'},
      {"flash",            required_argument, 0, 0},
      {"firmware-version", required_argument, 0, 0},
      {"frames",           required_argument, 0, 0},
      {"help",             no_argument,       0, 'h'},
      {"info",             no_argument,       0, 'i'},
      {"instance",         required_argument, 0, 'I'},
      {"file-attr",        required_argument, 0, 0},
      {"load",             required_argument, 0, 0},
      {"save",             required_argument, 0, 0},
      {"messages",         optional_argument, 0, 'M'},
      {"broken-line",      no_argument,       0, 0},
      {"dualx",            no_argument,       0, 0},
      {"x-center-threshold",  required_argument, 0,0},
      {"x-border-threshold",  required_argument, 0,0},
      {"y-center-threshold",  required_argument, 0,0},
      {"y-border-threshold",  required_argument, 0,0},
      {"sensor-variant",      no_argument,       0, 0},
      {"fail-if-any",         no_argument,       0, 0},
      {"matrix-size",         required_argument, 0,0},
      {"max-defects",      required_argument, 0,  0},
      {"upper-limit",      required_argument, 0,  0},
      {"lower-limit",      required_argument, 0,  0},
      {"pattern",          required_argument, 0,  0},
      {"count",            required_argument, 0, 'n'},
      {"output",           required_argument, 0, 'o'},
      {"port",             required_argument, 0, 'p'},
      {"query",            no_argument,       0, 'q'},
      {"read",             no_argument,       0, 'R'},
      {"reset",            no_argument,       0, 0},
      {"reset-bootloader", no_argument,       0, 0},
      {"register",         required_argument, 0, 'r'},
      {"references",       no_argument,       0, 0},
      {"self-cap-tune-config", no_argument,       0, 0},
      {"self-cap-tune-nvram",  no_argument,       0, 0},
      {"self-cap-signals", no_argument,       0, 0},
      {"self-cap-deltas",  no_argument,       0, 0},
      {"self-cap-refs",    no_argument,       0, 0},
      {"key-array-deltas",    no_argument,       0, 0},
      {"key-array-refs",    no_argument,       0, 0},
      {"key-array-signals",    no_argument,       0, 0},
      {"active-stylus-deltas",  no_argument,       0, 0},
      {"active-stylus-refs",    no_argument,       0, 0},
      {"bridge-server",    no_argument,       0, 'S'},
      {"test",             optional_argument, 0, 't'},
      {"odtest",           optional_argument, 0, 0},
      {"type",             required_argument, 0, 'T'},
      {"verbose",          required_argument, 0, 'v'},
      {"version",          no_argument,       0, 0},
      {"write",            no_argument,       0, 'W'},
      {"zero",             no_argument,       0, 0},
      {"switch-parallel",  optional_argument, 0, 0},
      {"switch-fast",      optional_argument, 0, 0},
      {"bridge-config",  required_argument, 0, 0},
      {"serial-test",    no_argument,       0, 0},
      {0,                  0,                 0, 0}
    };

    c = getopt_long(argc, argv,
                    "C:d:D:f:F:ghiI:M::m:n:o:p:qRr:St::T:v:W",
                    long_options, &option_index);

    if (c == -1)
      break;

    switch (c) {
    case 0:
      if (!strcmp(long_options[option_index].name, "t68-file")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_SERIAL_DATA;
          strncpy(strbuf, optarg, sizeof(strbuf));
          strbuf[sizeof(strbuf) - 1] = '\0';
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "t68-datatype")) {
        t68_datatype = strtol(optarg, NULL, 0);
      } else if (!strcmp(long_options[option_index].name, "flash")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_FLASH;
          strncpy(strbuf, optarg, sizeof(strbuf));
          strbuf[sizeof(strbuf) - 1] = '\0';
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "backup")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_BACKUP;
          if (optarg) {
            ret = mxt_convert_hex(optarg, &databuf, &count, sizeof(databuf));
            if (ret < 0) {
              fprintf(stderr, "十六进制转换错误\n");
              ret = MXT_ERROR_BAD_INPUT;
            }
            backup_cmd = databuf;
          }
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "calibrate")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_CALIBRATE;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "debug-dump")) {
        /* Allow FILE to be "-" meaning stdout */
        if (cmd == CMD_NONE) {
          cmd = CMD_DEBUG_DUMP;
          strncpy(strbuf, optarg, sizeof(strbuf));
          strbuf[sizeof(strbuf) - 1] = '\0';
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "freq-sweep")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_FREQ_SWEEP;
          strncpy(strbuf, optarg, sizeof(strbuf));
          strbuf[sizeof(strbuf) - 1] = '\0';
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "broken-line")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_BROKEN_LINE;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "dualx")) {
        dualx = true;
      } else if (!strcmp(long_options[option_index].name, "x-center-threshold")) {
        if (optarg) {
          bl_opts.x_center_threshold = strtol(optarg, NULL, 0);
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "x-border-threshold")) {
        if (optarg) {
          bl_opts.x_border_threshold = strtol(optarg, NULL, 0);
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "y-center-threshold")) {
        if (optarg) {
          bl_opts.y_center_threshold = strtol(optarg, NULL, 0);
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "y-border-threshold")) {
        if (optarg) {
          bl_opts.y_border_threshold = strtol(optarg, NULL, 0);
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "pattern")) {
        strncpy(strbuf, optarg, sizeof(strbuf));
        strbuf[sizeof(strbuf) - 1] = '\0';

        if (!strcasecmp(strbuf, "xsense"))
          bl_opts.pattern = BROKEN_LINE_PATTERN_XSENSE;
        else
          bl_opts.pattern = BROKEN_LINE_PATTERN_ITO;
      } else if (!strcmp(long_options[option_index].name, "sensor-variant")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_SENSOR_VARIANT;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "fail-if-any")) {
        if (optarg) {
          sv_opts.max_defects = 0;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "max-defects")) {
        if (optarg) {
          sv_opts.max_defects = strtol(optarg, NULL, 0);
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "upper-limit")) {
        if (optarg) {
          sv_opts.upper_limit = strtol(optarg, NULL, 0);
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "lower-limit")) {
        if (optarg) {
          sv_opts.lower_limit = strtol(optarg, NULL, 0);
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "matrix-size")) {
        if (optarg) {
          sv_opts.matrix_size = strtol(optarg, NULL, 0);
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "reset")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_RESET;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        } 
      } else if (!strcmp(long_options[option_index].name, "odtest")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_OD_TEST;
          if (optarg) {
            ret = mxt_convert_hex(optarg, &databuf, &count, sizeof(databuf));
              if (ret < 0) {
                fprintf(stderr, "十六进制转换错误\n");
                ret = MXT_ERROR_BAD_INPUT;
              }
              ondemand_test_cmd = databuf; 
          }          
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "self-cap-tune-config")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_SELF_CAP_TUNE_CONFIG;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "self-cap-tune-nvram")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_SELF_CAP_TUNE_NVRAM;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "load")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_LOAD_CFG;
          strncpy(strbuf, optarg, sizeof(strbuf));
          strbuf[sizeof(strbuf) - 1] = '\0';
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "save")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_SAVE_CFG;
          strncpy(strbuf, optarg, sizeof(strbuf));
          strbuf[sizeof(strbuf) - 1] = '\0';
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "reset-bootloader")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_RESET_BOOTLOADER;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "bootloader-version")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_BOOTLOADER_VERSION;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "switch-parallel")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_SWITCH_PARALLEL;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "switch-fast")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_SWITCH_FAST;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } 
#ifdef HAVE_LIBUSB
        else if (!strcmp(long_options[option_index].name, "bridge-config")) {
        if (cmd == CMD_NONE) {        
          cmd = CMD_BRIDGE_CONFIG;
          if (sscanf(optarg, "%x", &conn->usb.b_i2c_addr) != 1) {
            fprintf(stderr, "设备字符串无效 %s\n", optarg);
            conn = mxt_unref_conn(conn);
            print_usage(argv[0]);
            return MXT_ERROR_BAD_INPUT;
          }
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } 
#endif
      else if (!strcmp(long_options[option_index].name, "checksum")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_CRC_CHECK;
          strncpy(strbuf, optarg, sizeof(strbuf));
          strbuf[sizeof(strbuf) - 1] = '\0';
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "zero")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_ZERO_CFG;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "serial-test")) {
        if (cmd == CMD_NONE) {
          cmd = CMD_SERIAL_TEST;
        } else {
          print_usage(argv[0]);
          return MXT_ERROR_BAD_INPUT;
        }
      } else if (!strcmp(long_options[option_index].name, "firmware-version")) {
        strncpy(strbuf2, optarg, sizeof(strbuf2));
      } else if (!strcmp(long_options[option_index].name, "frames")) {
        t37_frames = strtol(optarg, NULL, 0);
      } else if (!strcmp(long_options[option_index].name, "file-attr")) {
        t37_file_attr = strtol(optarg, NULL, 0);
      } else if (!strcmp(long_options[option_index].name, "references")) {
        t37_mode = REFS_MODE;
      } else if (!strcmp(long_options[option_index].name, "self-cap-signals")) {
        t37_mode = SELF_CAP_SIGNALS;
      } else if (!strcmp(long_options[option_index].name, "self-cap-refs")) {
        t37_mode = SELF_CAP_REFS;
      } else if (!strcmp(long_options[option_index].name, "self-cap-deltas")) {
        t37_mode = SELF_CAP_DELTAS;
      } else if (!strcmp(long_options[option_index].name, "key-array-deltas")) {
        t37_mode = KEY_DELTAS_MODE;
      } else if (!strcmp(long_options[option_index].name, "key-array-refs")) {
        t37_mode = KEY_REFS_MODE;		  
      } else if (!strcmp(long_options[option_index].name, "key-array-signals")) {
        t37_mode = KEY_SIGS_MODE;		   
      } else if (!strcmp(long_options[option_index].name, "key-array-raw_sigs")) {
        t37_mode = KEY_RAW_SIGS_MODE;      
      } else if (!strcmp(long_options[option_index].name, "active-stylus-deltas")) {
        t37_mode = AST_DELTAS;
      } else if (!strcmp(long_options[option_index].name, "active-stylus-refs")) {
        t37_mode = AST_REFS;
      } else if (!strcmp(long_options[option_index].name, "block-size")) {
        i2c_block_size = atoi(optarg);
      } else if (!strcmp(long_options[option_index].name, "version")) {
        printf("mxt-app %s%s\n", MXT_VERSION, ENABLE_DEBUG ? " DEBUG":"");
        fflush(stdout);
        fflush(stderr);
        return MXT_SUCCESS;
      } else {
        fprintf(stderr, "未知选项 %s\n",
                long_options[option_index].name);
      }
      break;

    case 'd':
      if (optarg) {
        if (!strncmp(optarg, "i2c-dev:", 8)) {
          ret = mxt_new_conn(&conn, E_I2C_DEV);
          if (ret)
            return ret;

          if (sscanf(optarg, "i2c-dev:%d-%x",
                     &conn->i2c_dev.adapter, &conn->i2c_dev.address) != 2) {
            fprintf(stderr, "设备字符串无效 %s\n", optarg);
            conn = mxt_unref_conn(conn);
            return MXT_ERROR_NO_MEM;
          }
        } else if (!strncmp(optarg, "sysfs_i2c:", 10)) {
          ret = mxt_new_conn(&conn, E_SYSFS_I2C);
          if (ret)
            return ret;

          if (sscanf(optarg, "sysfs_i2c:%d-%x",
                     &conn->sysfs.i2c_bus, &conn->sysfs.i2c_addr) != 2) {
            fprintf(stderr, "设备字符串无效 %s\n", optarg);
            conn = mxt_unref_conn(conn);
            return MXT_ERROR_NO_MEM;
          }

          conn->sysfs.path = (char *)calloc(strlen(optarg) + 1, sizeof(char));
          if (!conn->sysfs.path) {
            fprintf(stderr, "calloc failure\n");
            conn = mxt_unref_conn(conn);
            return MXT_ERROR_NO_MEM;
          }

          memcpy(conn->sysfs.path, optarg + 10, strlen(optarg) - 10);
        } else if (!strncmp(optarg, "sysfs_spi:", 10)) {
          ret = mxt_new_conn(&conn, E_SYSFS_SPI);
          if (ret)
            return ret;

          if (sscanf(optarg, "sysfs_spi:%d-%d",
                     &conn->sysfs.spi_bus, &conn->sysfs.spi_cs) != 2) {
            fprintf(stderr, "设备字符串无效 %s\n", optarg);
            conn = mxt_unref_conn(conn);
            return MXT_ERROR_NO_MEM;
          }
          
          conn->sysfs.path = (char *)calloc(strlen(optarg) + 255, sizeof(char));

          if (!conn->sysfs.path) {
              fprintf(stderr, "calloc failure\n");
              conn = mxt_unref_conn(conn);
              return MXT_ERROR_NO_MEM;
            }

          memcpy(conn->sysfs.path, optarg, strlen(optarg));
        }
#ifdef HAVE_LIBUSB
        else if (!strncmp(optarg, "usb:", 4)) {
          ret = mxt_new_conn(&conn, E_USB);
          if (ret)
            return ret;

          if (sscanf(optarg, "usb:%d-%d-%x", &conn->usb.bus, &conn->usb.device, 
                &conn->usb.b_i2c_addr) != 3) {
            if (sscanf(optarg, "usb:%d-%d", &conn->usb.bus, &conn->usb.device) != 2) {
                fprintf(stderr, "设备字符串无效 %s\n", optarg);
                conn = mxt_unref_conn(conn);
                return MXT_ERROR_NO_MEM;
            }
          }
        }
#endif
        else if (!strncmp(optarg, "hidraw:", 7)) {
          ret = mxt_new_conn(&conn, E_HIDRAW);
          if (ret)
            return ret;

          conn->hidraw.report_id = HIDRAW_REPORT_ID;

          if (sscanf(optarg, "hidraw:%s", conn->hidraw.node) != 1) {
            fprintf(stderr, "设备字符串无效 %s\n", optarg);
            conn = mxt_unref_conn(conn);
            return MXT_ERROR_NO_MEM;
          }
        } else if (!strncmp(optarg, "serial:", 7)) {
          ret = mxt_new_conn(&conn, E_SERIAL);
          if (ret)
            return ret;

          strncpy(conn->serial.path, optarg + 7, sizeof(conn->serial.path) - 1);
          conn->serial.path[sizeof(conn->serial.path) - 1] = '\0';
        } else if (!strncmp(optarg, "com:", 4)) {
          ret = mxt_new_conn(&conn, E_SERIAL);
          if (ret)
            return ret;

          strncpy(conn->serial.path, optarg + 4, sizeof(conn->serial.path) - 1);
          conn->serial.path[sizeof(conn->serial.path) - 1] = '\0';
        } else {
          fprintf(stderr, "设备字符串无效 %s\n", optarg);
          conn = mxt_unref_conn(conn);
          return MXT_ERROR_BAD_INPUT;
        }
      }
      break;

    case 'C':
      if (cmd == CMD_NONE) {
        cmd = CMD_BRIDGE_CLIENT;
        strncpy(strbuf, optarg, sizeof(strbuf));
        strbuf[sizeof(strbuf) - 1] = '\0';
      } else {
        print_usage(argv[0]);
        return MXT_ERROR_BAD_INPUT;
      }
      break;

    case 'g':
      if (cmd == CMD_NONE) {
        cmd = CMD_GOLDEN_REFERENCES;
      } else {
        print_usage(argv[0]);
        return MXT_ERROR_BAD_INPUT;
      }
      break;

    case 'h':
      print_usage(argv[0]);
      return MXT_SUCCESS;

    case 'f':
      if (optarg) {
        format = strtol(optarg, NULL, 0);
      }
      break;

    case 'I':
      if (optarg) {
        instance = strtol(optarg, NULL, 0);
      }
      break;

    case 'M':
      msgs_enabled = true;
      if (cmd == CMD_NONE) {
        cmd = CMD_MESSAGES;
      }
      if (optarg)
        msgs_timeout = strtol(optarg, NULL, 0);
      break;

    case 'F':
      if (optarg) {
        msg_filter_type = strtol(optarg, NULL, 0);
      }
      break;

    case 'o':
      if (optarg) {
        strncpy(strbuf2, optarg, sizeof(strbuf2));
      } else {
        print_usage(argv[0]);
        return MXT_ERROR_BAD_INPUT;
      }
      break;

    case 'n':
      if (optarg) {
        count = strtol(optarg, NULL, 0);
      }
      break;

    case 'p':
      if (optarg) {
        port = strtol(optarg, NULL, 0);
      }
      break;

    case 'q':
      if (cmd == CMD_NONE) {
        cmd = CMD_QUERY;
      } else {
        print_usage(argv[0]);
        return MXT_ERROR_BAD_INPUT;
      }
      break;

    case 'r':
      if (optarg) {
        address = strtol(optarg, NULL, 0);
      }
      break;

    case 'R':
      if (cmd == CMD_NONE) {
        cmd = CMD_READ;
      } else {
        print_usage(argv[0]);
        return MXT_ERROR_BAD_INPUT;
      }
      break;

    case 'S':
      if (cmd == CMD_NONE) {
        cmd = CMD_BRIDGE_SERVER;
      } else {
        print_usage(argv[0]);
        return MXT_ERROR_BAD_INPUT;
      }
      break;

    case 'T':
      if (optarg) {
        object_type = strtol(optarg, NULL, 0);
      }
      break;

    case 'i':
      if (cmd == CMD_NONE) {
        cmd = CMD_INFO;
      } else {
        print_usage(argv[0]);
        return MXT_ERROR_BAD_INPUT;
      }
      break;

    case 't':
      if (cmd == CMD_NONE) {
        if (optarg) {
          ret = mxt_convert_hex(optarg, &databuf, &count, sizeof(databuf));
          if (ret) {
            fprintf(stderr, "十六进制转换错误\n");
            ret = MXT_ERROR_BAD_INPUT;
          } else {
            self_test_cmd = databuf;
          }
        }
        cmd = CMD_TEST;
      } else {
        print_usage(argv[0]);
        return MXT_ERROR_BAD_INPUT;
      }
      break;

    case 'v':
      if (optarg) {
        verbose = strtol(optarg, NULL, 0);
      }
      break;

    case 'W':
      if (cmd == CMD_NONE) {
        cmd = CMD_WRITE;
      } else {
        print_usage(argv[0]);
        return MXT_ERROR_BAD_INPUT;
      }
      break;

    default:
      /* Output newline to create space under getopt error output */
      fprintf(stderr, "\n\n");
      print_usage(argv[0]);
      return MXT_ERROR_BAD_INPUT;
    }
  }

  struct mxt_device *mxt = NULL;
  struct libmaxtouch_ctx *ctx;

  ret = mxt_new(&ctx);
  if (ret) {
    mxt_err(ctx, "初始化 libmaxtouch 失败");
    return ret;
  }

  /* Set debug level */
  mxt_set_log_level(ctx, verbose);
  mxt_verb(ctx, "verbose:%u", verbose);

  /* Update the i2c block size */
  if (i2c_block_size != I2C_DEV_MAX_BLOCK) {
    mxt_verb(ctx, "Setting i2c_block_size from %d to %d", ctx->i2c_block_size, i2c_block_size);
    ctx->i2c_block_size = i2c_block_size;
  }

  if (cmd == CMD_WRITE || cmd == CMD_READ) {
    mxt_verb(ctx, "instance:%u", instance);
    mxt_verb(ctx, "count:%u", count);
    mxt_verb(ctx, "address:%u", address);
    mxt_verb(ctx, "object_type:%u", object_type);
    mxt_verb(ctx, "format:%s", format ? "true" : "false");
  }

  if (cmd == CMD_QUERY) {
    ret = mxt_scan(ctx, &conn, true);
    goto free;


  /* Initialization of chip, scan new device */
  } else if (cmd == CMD_SERIAL_TEST) {
    ret = mxt_scan(ctx, &conn, false);
    if (ret)
      goto free;
    ret = serial_run_link_test(ctx, conn);
    goto free;
  } else if (cmd != CMD_FLASH && cmd != CMD_BOOTLOADER_VERSION) {
    ret = mxt_init_chip(ctx, &mxt, &conn);
    if (ret && cmd != CMD_CRC_CHECK )
      goto free;
  }

  switch (cmd) {
  case CMD_WRITE:
    mxt_verb(ctx, "Write command");
    ret = mxt_handle_write_cmd(mxt, object_type, count, instance, address,
                               argc, argv);
    if (ret == MXT_ERROR_BAD_INPUT)
      goto free;

    mxt_info(ctx, "写入完成");
    break;

  case CMD_READ:
    mxt_verb(ctx, "Read command");
    ret = mxt_read_object(mxt, object_type, instance, address, count, format);
       
    mxt_info(ctx, "读取完成");
    break;

  case CMD_INFO:
    mxt_verb(ctx, "CMD_INFO");
    mxt_print_info_block(mxt);
    mxt_print_config_crc(mxt);
    ret = MXT_SUCCESS;
    break;

  case CMD_GOLDEN_REFERENCES:
    mxt_verb(ctx, "CMD_GOLDEN_REFERENCES");
    ret = mxt_store_golden_refs(mxt);
    break;

  case CMD_BRIDGE_SERVER:
    mxt_verb(ctx, "CMD_BRIDGE_SERVER");
    mxt_verb(ctx, "port:%u", port);
    ret = mxt_socket_server(mxt, port);
    break;

  case CMD_BRIDGE_CLIENT:
    mxt_verb(ctx, "CMD_BRIDGE_CLIENT");
    ret = mxt_socket_client(mxt, strbuf, port);
    break;

  case CMD_SERIAL_DATA:
    mxt_verb(ctx, "CMD_SERIAL_DATA");
    mxt_verb(ctx, "t68_datatype:%u", t68_datatype);
    ret = mxt_serial_data_upload(mxt, strbuf, t68_datatype);
    break;

  case CMD_TEST:
    mxt_verb(ctx, "CMD_TEST");
    ret = run_self_tests(mxt, self_test_cmd, 0);
    break;

  case CMD_OD_TEST:
    mxt_verb(ctx, "CMD_OD_TEST");
    ret = run_self_tests(mxt, ondemand_test_cmd, 1);
    break;

  case CMD_FLASH:
    mxt_verb(ctx, "CMD_FLASH");
    ret = mxt_flash_firmware(ctx, mxt, strbuf, strbuf2, conn);
    break;

  case CMD_RESET:
    mxt_verb(ctx, "CMD_RESET");
    ret = mxt_reset_chip(mxt, false, 0);
    break;

  case CMD_BROKEN_LINE:
    if (dualx)
      bl_opts.dualx = dualx;
    mxt_verb(ctx, "CMD_BROKEN_LINE");
    ret = mxt_broken_line(mxt, &bl_opts);
    break;

  case CMD_SENSOR_VARIANT:
    if (dualx)
      sv_opts.dualx = dualx;
    mxt_verb(ctx, "CMD_SENSOR_VARIANT");
    ret = mxt_sensor_variant(mxt, &sv_opts);
    break;

  case CMD_RESET_BOOTLOADER:
    mxt_verb(ctx, "CMD_RESET_BOOTLOADER");
    ret = mxt_reset_chip(mxt, true, 0);
    break;

  case CMD_BOOTLOADER_VERSION:
    mxt_verb(ctx, "CMD_RESET_BOOTLOADER");
    ret = mxt_bootloader_version(ctx, mxt, conn);
    break;
#ifdef HAVE_LIBUSB
  case CMD_SWITCH_PARALLEL:
    mxt_verb(ctx, "CMD_SWITCH_PARALLEL");
    ret = usb_switch_parallel_mode(mxt, conn);
    break;

  case CMD_SWITCH_FAST:
    mxt_verb(ctx, "CMD_SWITCH_FAST");
    ret = usb_switch_fast_mode(mxt, conn);
    break;

  case CMD_BRIDGE_CONFIG:
    mxt_verb(ctx, "CMD_BRIDGE_CONFIG");
    ret = bridge_configure(mxt);
    break;
#endif

  case CMD_MESSAGES:
    // Messages handled after switch
    break;

  case CMD_BACKUP:
    mxt_verb(ctx, "CMD_BACKUP");
    ret = mxt_backup_config(mxt, backup_cmd);
    break;

  case CMD_CALIBRATE:
    mxt_verb(ctx, "CMD_CALIBRATE");
    ret = mxt_calibrate_chip(mxt);
    break;

  case CMD_DEBUG_DUMP:
    /* CSV-only mode when outputting to stdout */
    if (!strcmp(strbuf, "-")) {
      mxt_set_log_level(ctx, 0);
    }

    mxt_verb(ctx, "CMD_DEBUG_DUMP");
    mxt_verb(ctx, "mode:%u", t37_mode);
    mxt_verb(ctx, "frames:%u", t37_frames);
    mxt_verb(ctx, "file_attr:%u", t37_file_attr);
    ret = mxt_debug_dump(mxt, t37_mode, strbuf, t37_frames, instance, format, t37_file_attr);
    break;

  case CMD_FREQ_SWEEP:
    mxt_verb(ctx, "CMD_FREQ_SWEEP");
    mxt_verb(ctx, "filename: %s", strbuf);
    ret = mxt_freq_sweep(mxt, strbuf, strbuf2, &fs_opts);
    break;

  case CMD_ZERO_CFG:
    mxt_verb(ctx, "CMD_ZERO_CFG");
    ret = mxt_zero_config(mxt);
    if (ret)
      mxt_err(ctx, "清零所有配置设置出错");
    break;

  case CMD_LOAD_CFG:
    mxt_verb(ctx, "CMD_LOAD_CFG");
    mxt_verb(ctx, "filename:%s", strbuf);
    ret = mxt_load_config_file(mxt, strbuf);
    if (ret) {
      mxt_err(ctx, "加载配置出错");
    } else {
      mxt_info(ctx, "配置已加载");
    }
    break;

  case CMD_SAVE_CFG:
    mxt_verb(ctx, "CMD_SAVE_CFG");
    mxt_verb(ctx, "filename:%s", strbuf);
    mxt_verb(ctx, "format %d", format);
    ret = mxt_save_config_file(mxt, strbuf, format);
    break;

  case CMD_SELF_CAP_TUNE_CONFIG:
  case CMD_SELF_CAP_TUNE_NVRAM:
    mxt_verb(ctx, "CMD_SELF_CAP_TUNE");
    ret = mxt_self_cap_tune(mxt, cmd);
    break;

  case CMD_CRC_CHECK:
    mxt_verb(ctx, "CMD_CRC_CHECK");
    mxt_verb(ctx, "filename:%s", strbuf);
    ret = mxt_checkcrc(mxt, strbuf);
    break;

  case CMD_NONE:
  default:
    mxt_verb(ctx, "cmd: %d", cmd);
    mxt_set_log_fn(ctx, mxt_log_stdout);

    if (verbose <= 2)
      mxt_set_log_level(ctx, 2);

    ret = mxt_menu(mxt);
    break;
  }

  if (cmd == CMD_MESSAGES || (msgs_enabled && ret == MXT_SUCCESS)) {
    mxt_verb(ctx, "CMD_MESSAGES");
    mxt_verb(ctx, "msgs_timeout:%d", msgs_timeout);
    // Support message filtering with -T
    if (cmd == CMD_MESSAGES && !msg_filter_type)
      msg_filter_type = object_type;

    ret = print_raw_messages(mxt, msgs_timeout, msg_filter_type);
  }

  if (cmd != CMD_FLASH && cmd != CMD_BOOTLOADER_VERSION && mxt) {
    mxt_set_debug(mxt, false);
    mxt_free_device(mxt);
    mxt_unref_conn(conn);
  }

free:
  if (ret != MXT_SUCCESS)
    fprintf(stderr, "mxt-app 退出码 %d\n", ret);
  else if (cmd == CMD_SERIAL_TEST)
    fprintf(stdout, "SERIAL_TEST: done OK\n");

  mxt_free(ctx);

  return ret;
}
