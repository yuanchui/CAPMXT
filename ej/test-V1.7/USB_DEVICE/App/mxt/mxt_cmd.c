#include "mxt_cmd.h"
#include "mxt_state.h"
#include "mxt_work.h"
#include "mxt_config.h"
#include "mxt_i2c.h"
#include "mxt_usb_io.h"
#include "mxt_touch.h"
#include "mxt_msg.h"
#include "mxt_spi_stream.h"
#include "mxt_bridge.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"

static char g_cmd_queue[CMD_BUFFER_SIZE];
static volatile uint8_t g_cmd_queued = 0U;

static void MXT_QueueStringCommand(const char *cmd)
{
    if (cmd == NULL || cmd[0] == '\0') {
        return;
    }
    if (strlen(cmd) >= CMD_BUFFER_SIZE) {
        return;
    }
    if (g_cmd_queued == 0U) {
        memcpy(g_cmd_queue, cmd, strlen(cmd) + 1U);
        g_cmd_queued = 1U;
    }
}

void ProcessStringCommand(uint8_t *buf, uint32_t len)
{
    /* 复制到临时缓冲区并去掉尾部 \r \n 和空格 */
    if (len >= CMD_BUFFER_SIZE) len = CMD_BUFFER_SIZE - 1;
    memcpy(MXT_WORK_STR, buf, len);
    MXT_WORK_STR[len] = '\0';
    for (int i = len - 1; i >= 0; i--) {
        if (MXT_WORK_STR[i] == '\r' || MXT_WORK_STR[i] == '\n' || MXT_WORK_STR[i] == ' ') {
            MXT_WORK_STR[i] = '\0';
        } else {
            break;
        }
    }
    if (strlen(MXT_WORK_STR) == 0) return;
    if (g_cmd_pending != 0U) {
      if ((strcmp(MXT_WORK_STR, "SPISTOP") != 0) && (strcmp(MXT_WORK_STR, "spistop") != 0)
          && (strcmp(MXT_WORK_STR, "SPIDBG") != 0) && (strcmp(MXT_WORK_STR, "spidbg") != 0)) {
        MXT_QueueStringCommand(MXT_WORK_STR);
        return;
      }
    }

    memcpy(g_cmd_buffer, MXT_WORK_STR, strlen(MXT_WORK_STR) + 1);
    g_cmd_pending = 1U;
}


void ProcessPendingCommand(void)
{
    if (!g_cmd_pending) return;
    g_cmd_pending = 0;
    
    char *cmd_str = g_cmd_buffer;
    

    
    /* Command: "MODE0" / "BRIDGEBIN" - Switch to I2C-USB bridge mode */
    if (strcmp(cmd_str, "MODE0") == 0 || strcmp(cmd_str, "mode0") == 0 ||
        strcmp(cmd_str, "BRIDGEBIN") == 0 || strcmp(cmd_str, "bridgebin") == 0) {
        /* 须先回 OK 再切 mode0：USB_SendString 在 BINARY 下会静默丢弃文本 */
        USB_SendString("OK: Switched to I2C-USB bridge mode\r\n");
        MXT_WaitUsbIdle(500U);
        g_bridge_mode = BRIDGE_MODE_BINARY;
        g_menu_state = MENU_IDLE;
    }
    /* Command: "MODE1" - Switch to string mode */
    else if (strcmp(cmd_str, "MODE1") == 0 || strcmp(cmd_str, "mode1") == 0) {
        g_bridge_mode = BRIDGE_MODE_STRING;
        MXT_InitTouchScreen();
        g_menu_state = MENU_IDLE;
        USB_SendString("OK: Switched to string mode\r\n");
        snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Config: X=%d, Y=%d\r\n", g_matrix_x_size, g_matrix_y_size);
        USB_SendString(MXT_WORK_STR);
        USB_SendString("Type 'u' to enter diagnostic menu\r\n");
    }
    else if (strcmp(cmd_str, "SPISTART") == 0 || strcmp(cmd_str, "spistart") == 0) {
        MXT_SPI_PrepareStream(0U);
        g_spi_stream_enabled = 1U;
        USB_SendString("INFO: SPI stream START (raw hex)\r\n");
    }
    else if (strcmp(cmd_str, "SPISTART1") == 0 || strcmp(cmd_str, "spistart1") == 0) {
        MXT_SPI_PrepareStream(1U);
        g_spi_stream_enabled = 1U;
        USB_SendString("INFO: SPI stream START1 (16x16 text)\r\n");
    }
    else if (strcmp(cmd_str, "SPISTART3") == 0 || strcmp(cmd_str, "spistart3") == 0) {
        MXT_SPI_PrepareStream(2U);
        g_spi_stream_enabled = 1U;
        USB_SendString("INFO: SPI stream START3 (Mode3 AA 10 33 packets)\r\n");
    }
    else if (strcmp(cmd_str, "SPISTOP") == 0 || strcmp(cmd_str, "spistop") == 0) {
        g_spi_stream_enabled = 0U;
        SPIUSB_TryFlush();
        if (g_spi_rx_overflow > 0U || g_spi_tx_drop > 0U) {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE,
                     "WARN: SPISTOP rx_ovf=%u tx_drop=%lu\r\n",
                     (unsigned)g_spi_rx_overflow, (unsigned long)g_spi_tx_drop);
            USB_SendString(MXT_WORK_STR);
        }
        USB_SendString("INFO: SPI stream STOP\r\n");
    }
    /* Command: "START" / "START1" - Start diagnostic output
     * 语法1: START  [MAP16*] [HEX|CHAR] interval_ms   -> 默认 FRAME0 (Mutual Delta, 0x10)
     * 语法2: START1 [MAP16*] [HEX|CHAR] interval_ms   -> 默认 FRAME1 (Mutual Reference, 0x11)
     * 语法3: START CHGNO [X|Y|XY] interval_ms         -> CHGNO 模式在 Mode3 包中增加 [触点号,x,y,动作类型]
     * 说明: START1 模式下每次采集前会执行一次 CAL。
     */
    else if (strncmp(cmd_str, "START", 5) == 0 || strncmp(cmd_str, "start", 5) == 0) {
        uint8_t start_frame1_mode = (strncmp(cmd_str, "START1", 6) == 0 || strncmp(cmd_str, "start1", 6) == 0) ? 1 : 0;
        /* 解析参数 */
        char *tok = strtok(cmd_str, " "); /* START/START1 */
        char *arg1 = strtok(NULL, " ");   /* 可选: MAP16... */
        char *arg2 = strtok(NULL, " ");   /* 可选: HEX/CHAR 或 翻转/interval ms */
        char *arg3 = strtok(NULL, " ");   /* 可选: interval ms (若 arg2 是 HEX/CHAR 或 翻转) */

        /* 默认：无旋转翻转；MAP16 默认二进制（Mode3）；可用 CHAR 切换为文本 */
        g_stream_rot = 0;
        g_stream_flip = 0;        /* 仅用于 MAP16 矩阵旋转/翻转 */
        g_stream_touch_flip = 0;  /* 仅用于 CHGNO 触点坐标翻转 */
        g_stream_map16_hex = 0;
        g_stream_map16_char = 0;
        g_stream_chgno = 0;
        g_last_touch_valid = 0;

        /* 默认间隔字符串 */
        const char *iv_str = NULL;

        /* START CHGNO 模式：
         *  - 语法A: START CHGNO [X|Y|XY] [MAP16*] interval_ms
         *  - 语法B: START CHGNOXY [MAP16*] interval_ms   (X/Y/XY 后缀直接跟在 CHGNO 后面)
         *  使用与 MAP16 Mode3 相同的二进制输出（AA 10 33），只是额外在每行尾部附加触点信息。
         */
        if (arg1 && (strncasecmp(arg1, "CHGNO", 5) == 0)) {
            g_stream_chgno = 1;
            g_stream_map16_hex = 1;   /* CHGNO 模式强制启用 Mode3 二进制输出 */

            /* 1) 解析 CHGNO 自身可能携带的后缀（如 CHGNOX / CHGNOXY），仅作用于触点坐标 */
            const char *p_chg = arg1 + 5;
            for (; *p_chg; p_chg++) {
                if (*p_chg == 'X' || *p_chg == 'x') g_stream_touch_flip |= 0x01;
                else if (*p_chg == 'Y' || *p_chg == 'y') g_stream_touch_flip |= 0x02;
            }

            /* 2) 如果后面紧跟 MAP16 变体，则解析其旋转/翻转：START CHGNOXY MAP16L90X 10 */
            if (arg2 && (strncasecmp(arg2, "MAP16", 5) == 0)) {
                const char *p = arg2 + 5;
                if (*p == 'R' || *p == 'r') {
                    if (p[1] == '9' && p[2] == '0') { g_stream_rot = 1; p += 3; }
                } else if (*p == 'L' || *p == 'l') {
                    if (p[1] == '9' && p[2] == '0') { g_stream_rot = 2; p += 3; }
                }
                for (; *p; p++) {
                    if (*p == 'X' || *p == 'x') g_stream_flip |= 0x01;
                    else if (*p == 'Y' || *p == 'y') g_stream_flip |= 0x02;
                }
                /* MAP16 在 CHGNO 下始终为二进制 Mode3，不支持 CHAR 文本输出 */
                iv_str = arg3;  /* 第三个参数为间隔 ms */
            }
            else {
                /* 3) 无 MAP16 变体时，沿用原 CHGNO [X|Y|XY] interval_ms 语法（翻转在 arg2 中，仍然只影响触点坐标） */
                uint8_t has_flip = 0;
                if (arg2 && (strchr(arg2, 'X') || strchr(arg2, 'x') || strchr(arg2, 'Y') || strchr(arg2, 'y'))) {
                    const char *p = arg2;
                    for (; *p; p++) {
                        if (*p == 'X' || *p == 'x') g_stream_touch_flip |= 0x01;
                        else if (*p == 'Y' || *p == 'y') g_stream_touch_flip |= 0x02;
                    }
                    has_flip = 1;
                }
                /* 时间参数：若存在翻转，则取 arg3，否则取 arg2 */
                iv_str = has_flip ? arg3 : arg2;
            }

            USB_SendString("INFO: START CHGNO mode enabled (Mode3 + touch info)\r\n");
        }
        /* 参数1: MAP16 变体（如 MAP16R90X、MAP16L90XY 等），保持原有行为不变（无 CHGNO 时） */
        else if (arg1 && (strncasecmp(arg1, "MAP16", 5) == 0)) {
            const char *p = arg1 + 5;
            if (*p == 'R' || *p == 'r') {
                if (p[1] == '9' && p[2] == '0') { g_stream_rot = 1; p += 3; }
            } else if (*p == 'L' || *p == 'l') {
                if (p[1] == '9' && p[2] == '0') { g_stream_rot = 2; p += 3; }
            }
            for (; *p; p++) {
                if (*p == 'X' || *p == 'x') g_stream_flip |= 0x01;
                else if (*p == 'Y' || *p == 'y') g_stream_flip |= 0x02;
            }
            g_stream_map16_hex = 1; /* MAP16 变体默认二进制 */
            /* 可选后缀/参数：HEX 或 CHAR */
            if ((arg2 && strcasecmp(arg2, "CHAR") == 0) || (arg3 && strcasecmp(arg3, "CHAR") == 0)) {
                g_stream_map16_char = 1;
                g_stream_map16_hex = 0;
            }
            if ((arg2 && strcasecmp(arg2, "HEX") == 0) || (arg3 && strcasecmp(arg3, "HEX") == 0)) {
                g_stream_map16_hex = 1;
                g_stream_map16_char = 0;
            }

            /* MAP16 路径下，若未指定专门的间隔参数，则保持原逻辑 */
            if (!iv_str) {
                iv_str = (arg3 && ( (arg2 && (strcasecmp(arg2,"HEX")==0 || strcasecmp(arg2,"CHAR")==0)) )) ? arg3 : arg2;
            }
        }

        /* 若上面未设置 iv_str，则默认为原始 START 语法: 第二个参数为间隔 */
        if (!iv_str) {
            iv_str = arg2;
        }

        /* 参数: 间隔 ms */
        if (iv_str) {
            uint32_t iv = (uint32_t)atoi(iv_str);
            /* 允许的范围从原来的 50–5000 ms 放宽到 20–5000 ms */
            if (iv >= 10 && iv <= 5000) {
                g_diag_interval_ms = iv;
            } else {
                g_diag_interval_ms = 1000;
            }
        } else {
            g_diag_interval_ms = 1000;
        }

        /* 默认诊断模式：START=互容 Delta(0x10), START1=互容 Reference(0x11) */
        if (g_diag_mode == DIAG_MODE_NONE) {
            if (!g_touch_inited) {
                MXT_InitTouchScreen();
            }
            if (start_frame1_mode) {
                g_diag_mode = DIAG_MODE_MUTUAL_REF;
                USB_SendString("INFO: Default diag mode = Mutual Reference (0x11)\r\n");
            } else {
                g_diag_mode = DIAG_MODE_MUTUAL_DELTA;
                USB_SendString("INFO: Default diag mode = Mutual Delta (0x10)\r\n");
            }
        }

        /* START1 强制切到 FRAME1，并开启每次采集前 CAL */
        if (start_frame1_mode) {
            g_diag_mode = DIAG_MODE_MUTUAL_REF;
            g_stream_pre_cal = 1;
            USB_SendString("INFO: START1 mode = FRAME1 + pre-CAL\r\n");
        } else {
            g_stream_pre_cal = 0;
        }

        /* 启动输出，重置计时、帧号；并关闭 CHG 消息输出 */
        MXT_EnableOutput(1);
        g_last_diag_time = HAL_GetTick();
        g_stream_frame_id = 0;
        /* 普通 START/MAP16 保持原有行为：关闭 CHG 文本输出
         * START CHGNO 模式：为了能够依赖 CHG 队列获取触点坐标，不强制关闭消息处理
         */
        if (g_stream_chgno) {
            g_chg_process_enabled = 1;   /* 强制打开 CHG 处理，用于更新触点信息 */
            g_msg_output_enabled = 0;    /* 不需要文本输出，只使用消息更新触点坐标 */
        } else {
            g_msg_output_enabled = 0;    /* 等效 MSG_OFF */
        }
        USB_SendString("OK: Diagnostic output started\r\n");
        if (g_stream_map16_hex) {
            USB_SendString("INFO: MAP16 binary stream (Mode3 packets) enabled\r\n");
        } else if (g_stream_map16_char) {
            USB_SendString("INFO: MAP16 text output enabled\r\n");
        }
    }
    /* Command: "FRAME1" - Mutual Reference (0x11)
     * 扩展语法：FRAME1 [AT16] [HEX|CHAR]
     */
    else if (strncmp(cmd_str, "FRAME1", 6) == 0 || strncmp(cmd_str, "frame1", 6) == 0) {
        strncpy(MXT_WORK_STR, cmd_str, MXT_WORK_BUF_SIZE - 1);
        MXT_WORK_STR[MXT_WORK_BUF_SIZE - 1] = '\0';

        strtok(MXT_WORK_STR, " ");
        char *arg1 = strtok(NULL, " ");
        char *arg2 = strtok(NULL, " ");

        uint8_t use_map16 = 0;
        uint8_t out_hex = 0;

        if (arg1 && strcasecmp(arg1, "AT16") == 0) {
            use_map16 = 1;
            if (arg2 && strcasecmp(arg2, "HEX") == 0) out_hex = 1;
        }

        if (!g_touch_inited) MXT_InitTouchScreen();
        g_diag_mode = DIAG_MODE_MUTUAL_REF;
        if (!(use_map16 && out_hex)) {
            USB_SendString("INFO: Mutual Reference (0x11)\r\n");
        }
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            if (use_map16) {
                if (out_hex) {
                    MXT_SendMode3Packets(0, 0, g_stream_frame_id++);
                } else {
                    MXT_OutputMap16();
                }
            } else {
                MXT_OutputDiagnosticData();
            }
        } else {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(MXT_WORK_STR);
        }
    }

    /* Command: "FRAME0" - Mutual Delta (0x10)
     * 扩展语法：FRAME0 [AT16] [HEX|CHAR]
     */
    else if (strncmp(cmd_str, "FRAME0", 6) == 0 || strncmp(cmd_str, "frame0", 6) == 0) {
        strncpy(MXT_WORK_STR, cmd_str, MXT_WORK_BUF_SIZE - 1);
        MXT_WORK_STR[MXT_WORK_BUF_SIZE - 1] = '\0';

        char *tok = strtok(MXT_WORK_STR, " ");
        char *arg1 = strtok(NULL, " ");
        char *arg2 = strtok(NULL, " ");

        uint8_t use_map16 = 0;
        uint8_t out_hex = 0;  /* 0=CHAR(文本) */

        if (arg1 && strcasecmp(arg1, "AT16") == 0) {
            use_map16 = 1;
            if (arg2 && strcasecmp(arg2, "HEX") == 0) out_hex = 1;
            else out_hex = 0; /* 默认 CHAR */
        }

        (void)tok;
        if (!g_touch_inited) MXT_InitTouchScreen();
        g_diag_mode = DIAG_MODE_MUTUAL_DELTA;
        if (!(use_map16 && out_hex)) {
            USB_SendString("INFO: Mutual Delta (0x10)\r\n");
        }
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            if (use_map16) {
                if (out_hex) {
                    MXT_SendMode3Packets(0, 0, g_stream_frame_id++);
                } else {
                    MXT_OutputMap16();
                }
            } else {
                MXT_OutputDiagnosticData();
            }
        } else {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(MXT_WORK_STR);
        }
    }
    /* Command: "FRAME3" - Self Delta (0xF7)
     * 扩展语法：FRAME3 [AT16] [HEX|CHAR]
     * - 默认(无参数)：文本输出（Y:/X:）
     * - MAP16：裁剪为 Y前16 + X前16
     *   - HEX：Mode3 二进制包（AA 10 33...），不裁剪发4包，裁剪发2包；line 字段作发送计数
     *   - CHAR：文本输出（当前实现仍输出完整 Y20/X32；若你需要裁剪文本也可再加）
     */
    else if (strncmp(cmd_str, "FRAME3", 6) == 0 || strncmp(cmd_str, "frame3", 6) == 0) {
        strncpy(MXT_WORK_STR, cmd_str, MXT_WORK_BUF_SIZE - 1);
        MXT_WORK_STR[MXT_WORK_BUF_SIZE - 1] = '\0';

        (void)strtok(MXT_WORK_STR, " ");
        char *arg1 = strtok(NULL, " ");
        char *arg2 = strtok(NULL, " ");

        uint8_t use_map16 = 0;
        uint8_t out_hex = 0;
        if (arg1 && strcasecmp(arg1, "AT16") == 0) {
            use_map16 = 1;
            if (arg2 && strcasecmp(arg2, "HEX") == 0) out_hex = 1;
        }

        if (!g_touch_inited) MXT_InitTouchScreen();
        g_diag_mode = DIAG_MODE_SELF_DELTA;
        if (!(use_map16 && out_hex)) {
            USB_SendString("INFO: Self Delta (0xF7)\r\n");
        }
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            if (out_hex) {
                MXT_SendSelfCapMode3Packets(use_map16, g_stream_frame_id++);
            } else {
                MXT_OutputDiagnosticData();
            }
        } else {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(MXT_WORK_STR);
        }
    }
    /* Command: "FRAME4" - Self Reference (0xF8)
     * 扩展语法：FRAME4 [AT16] [HEX|CHAR]
     */
    else if (strncmp(cmd_str, "FRAME4", 6) == 0 || strncmp(cmd_str, "frame4", 6) == 0) {
        strncpy(MXT_WORK_STR, cmd_str, MXT_WORK_BUF_SIZE - 1);
        MXT_WORK_STR[MXT_WORK_BUF_SIZE - 1] = '\0';

        (void)strtok(MXT_WORK_STR, " ");
        char *arg1 = strtok(NULL, " ");
        char *arg2 = strtok(NULL, " ");

        uint8_t use_map16 = 0;
        uint8_t out_hex = 0;
        if (arg1 && strcasecmp(arg1, "AT16") == 0) {
            use_map16 = 1;
            if (arg2 && strcasecmp(arg2, "HEX") == 0) out_hex = 1;
        }

        if (!g_touch_inited) MXT_InitTouchScreen();
        g_diag_mode = DIAG_MODE_SELF_REF;
        if (!(use_map16 && out_hex)) {
            USB_SendString("INFO: Self Reference (0xF8)\r\n");
        }
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            if (out_hex) {
                MXT_SendSelfCapMode3Packets(use_map16, g_stream_frame_id++);
            } else {
                MXT_OutputDiagnosticData();
            }
        } else {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(MXT_WORK_STR);
        }
    }
    /* Command: "FRAME5" - Read Key Array diagnostic data */
    else if (strcmp(cmd_str, "FRAME5") == 0 || strcmp(cmd_str, "frame5") == 0) {
        if (!g_touch_inited) {
            MXT_InitTouchScreen();
        }
        /* Key Array使用Self Signal模式 (0xF5) */
        g_diag_mode = DIAG_MODE_SELF_SIGNAL;
        USB_SendString("INFO: Key Array / Self Signal (0xF5)\r\n");
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            MXT_OutputDiagnosticData();
        } else {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(MXT_WORK_STR);
        }
    }
    /* Command: "FRAME38" - Read Self DC level estimate (0x38) */
    else if (strcmp(cmd_str, "FRAME38") == 0 || strcmp(cmd_str, "frame38") == 0) {
        if (!g_touch_inited) {
            MXT_InitTouchScreen();
        }
        g_diag_mode = DIAG_MODE_SELF_DC;
        USB_SendString("INFO: Self DC level estimate (0x38)\r\n");
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            MXT_OutputDiagnosticData();
        } else {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(MXT_WORK_STR);
        }
    }
    /* Command: "STOP" - Stop diagnostic output */
    else if (strcmp(cmd_str, "STOP") == 0 || strcmp(cmd_str, "stop") == 0) {
        /* STOP should fully stop all periodic output paths */
        g_spi_check_requested = 0U;
        g_spi_stream_enabled = 0U;
        SPIUSB_TryFlush();
        MXT_EnableOutput(0);
        g_stream_pre_cal = 0;
        g_stream_chgno = 0;
        USB_SendString("OK: Diagnostic output stopped\r\n");
    }
    /* Command: "MSG_ON" - Enable message output */
    else if (strcmp(cmd_str, "MSG_ON") == 0 || strcmp(cmd_str, "msg_on") == 0) {
        g_msg_output_enabled = 1;
        USB_SendString("OK: Message output enabled\r\n");
    }
    /* Command: "MSG_OFF" - Disable message output */
    else if (strcmp(cmd_str, "MSG_OFF") == 0 || strcmp(cmd_str, "msg_off") == 0) {
        g_msg_output_enabled = 0;
        USB_SendString("OK: Message output disabled\r\n");
    }
    /* Command: "CHGON" - Enable CHG processing and perform one CHG read
     * 注意：START 会将 g_msg_output_enabled 置 0 以关闭 CHG 消息输出；
     * 为了在 STOP 之后仅靠 CHGON 也能恢复 CHG 处理，这里同时重新打开消息输出。
     */
    else if (strcmp(cmd_str, "CHGON") == 0 || strcmp(cmd_str, "chgon") == 0) {
        g_msg_output_enabled = 1;       /* 确保 MXT_CheckAndProcessMessages() 不再被 MSG_OFF 拦住 */
        g_chg_process_enabled = 1;
        USB_SendString("CHG processing enabled\r\n");
        MXT_CheckAndProcessMessages();  /* 立即执行一次 CHG 读取 */
    }
    /* Command: "CHGOFF" - Disable CHG processing (default) */
    else if (strcmp(cmd_str, "CHGOFF") == 0 || strcmp(cmd_str, "chgoff") == 0) {
        g_chg_process_enabled = 0;
        USB_SendString("CHG processing disabled (default)\r\n");
    }
    /* Command: "MAPALL" - Read one frame and output full matrix */
    else if (strcmp(cmd_str, "MAPALL") == 0 || strcmp(cmd_str, "mapall") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        if (g_diag_mode == DIAG_MODE_NONE) g_diag_mode = DIAG_MODE_MUTUAL_DELTA;
        if (MXT_ReadCompleteDiagnosticFrame() == 0) {
            MXT_OutputMapAll();
        } else {
            USB_SendString("ERR: MAPALL read frame failed\r\n");
        }
    }
    /* Command: "MAP16" - Read one frame and output 16x16 (index 0-15) */
    else if (strcmp(cmd_str, "MAP16") == 0 || strcmp(cmd_str, "map16") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        if (g_diag_mode == DIAG_MODE_NONE) g_diag_mode = DIAG_MODE_MUTUAL_DELTA;
        if (MXT_ReadCompleteDiagnosticFrame() == 0) {
            MXT_OutputMap16();
        } else {
            USB_SendString("ERR: MAP16 read frame failed\r\n");
        }
    }
    /* Command: "RESET" - Reset the controller */
    else if (strcasecmp(cmd_str, "RESET") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        if (g_t6_addr == 0) {
            USB_SendString("ERR: T6 not found\r\n");
        } else {
            uint8_t val = 0x01;
            if (MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr, &val, 1) == 0) {
                USB_SendString("OK: Reset command sent\r\n");
                g_touch_inited = 0; /* 重置后标记为未初始化 */
            } else {
                USB_SendString("ERR: Reset failed\r\n");
            }
        }
    }
    /* Command: "CALIBRATE" or "CAL" - Calibrate the controller */
    else if (strcasecmp(cmd_str, "CALIBRATE") == 0 || strcasecmp(cmd_str, "CAL") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        if (g_t6_addr == 0) {
            USB_SendString("ERR: T6 not found\r\n");
        } else {
            uint8_t val = 0x01;
            if (MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 2, &val, 1) == 0) {
                USB_SendString("OK: Calibration command sent\r\n");
            } else {
                USB_SendString("ERR: Calibration failed\r\n");
            }
        }
    }
    /* Command: "BACKUP" - Backup config to NV memory */
    else if (strcasecmp(cmd_str, "BACKUP") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        if (g_t6_addr == 0) {
            USB_SendString("ERR: T6 not found\r\n");
        } else {
            uint8_t val = 0x55;
            if (MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 1, &val, 1) == 0) {
                USB_SendString("OK: Backup command sent\r\n");
            } else {
                USB_SendString("ERR: Backup failed\r\n");
            }
        }
    }
    /* Command: "EXPORTTXT" - Export compact config text (summary only) */
    else if (strcasecmp(cmd_str, "EXPORTTXT") == 0 || strcasecmp(cmd_str, "DUMPTXT") == 0) {
        MXT_ExportConfigAsTxt();
    }
    /* Command: "EXPORTBIN" - Export configuration as compact binary stream */
    else if (strcasecmp(cmd_str, "EXPORTBIN") == 0 || strcasecmp(cmd_str, "DUMPBIN") == 0) {
        MXT_ExportConfigAsBin();
    }
    /* MAP16 组合指令：先旋转(R90/L90 可选)，再翻转(X/Y/XY 可选)
     * 支持：MAP16R90X / MAP16R90Y / MAP16R90XY / MAP16L90X / MAP16L90Y / MAP16L90XY
     * 以及原有：MAP16R90 / MAP16L90 / MAP16X / MAP16Y / MAP16XY
     */
    else if (strncmp(cmd_str, "MAP16", 5) == 0 || strncmp(cmd_str, "map16", 5) == 0) {
        uint8_t rot = 0;       /* 0=不旋转, 1=CW90, 2=CCW90 */
        uint8_t flip = 0;      /* bit0=X, bit1=Y */
        const char *p = cmd_str + 5; /* after MAP16 */
        /* 允许空后缀：MAP16 已在上面处理；这里处理带后缀的组合 */
        if (*p == 'R' || *p == 'r') {
            if ((p[1] == '9') && (p[2] == '0')) { rot = 1; p += 3; }
        } else if (*p == 'L' || *p == 'l') {
            if ((p[1] == '9') && (p[2] == '0')) { rot = 2; p += 3; }
        }
        /* 翻转后缀：X / Y / XY（顺序不敏感） */
        for (; *p; p++) {
            if (*p == 'X' || *p == 'x') flip |= 0x01;
            else if (*p == 'Y' || *p == 'y') flip |= 0x02;
        }

        /* 仅当确实是这些后缀之一才处理；否则让后续 UNKNOWN 去报错 */
        if (rot != 0 || flip != 0) {
            if (!g_touch_inited) MXT_InitTouchScreen();
            if (g_diag_mode == DIAG_MODE_NONE) g_diag_mode = DIAG_MODE_MUTUAL_DELTA;
            if (MXT_ReadCompleteDiagnosticFrame() == 0) {
                MXT_OutputMap16Transformed(rot, flip);
            } else {
                USB_SendString("ERR: MAP16* read frame failed\r\n");
            }
        } else {
            USB_SendString("ERROR: Unknown MAP16 variant\r\n");
        }
    }
    /* Command: "INFO" - Read Info Block (addr 0x0000, 7 bytes), ref maXTouch_Serial_Commands.md */
    else if (strcmp(cmd_str, "INFO") == 0 || strcmp(cmd_str, "info") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        uint8_t id_info[7];
        if (MXT_I2C_Read(g_mxt_i2c_addr, 0x0000, id_info, 7) != 0) {
            USB_SendString("ERR: Read Info Block failed\r\n");
        } else {
            MXT_FormatInfoBlockLine(id_info, MXT_WORK_STR, MXT_WORK_BUF_SIZE);
            USB_SendString(MXT_WORK_STR);
        }
    }
    /* Command: "FINDIIC" - Scan maXTouch I2C address (application mode) */
    else if (strcmp(cmd_str, "FINDIIC") == 0 || strcmp(cmd_str, "findiic") == 0) {
        uint8_t found = MXT_FindI2CAddress();
        if (found == 0x81U || found == 0U) {
            SendResponse((uint8_t *)"ERR: FINDIIC no device\r\n", 24U);
        } else {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "OK: FINDIIC addr=0x%02X\r\n", found);
            SendResponse((uint8_t *)MXT_WORK_STR, (uint16_t)strlen(MXT_WORK_STR));
        }
    }
#if MXT_HAS_ENC
    /* Command: "ENCRESETBL" - T6@offset0 写 0xA5 强制进 Bootloader（enc.txt 阶段 B 末步） */
    else if (strcmp(cmd_str, "ENCRESETBL") == 0 || strcmp(cmd_str, "encresetbl") == 0) {
        uint8_t app = 0U;
        uint16_t t6 = 0U;
        uint8_t bl = 0U;
        uint8_t st = 0U;
        uint8_t r = MXT_ENC_ForceResetToBootloader(&app, &t6, &bl, &st);
        if (r != 0U) {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: ENCRESETBL failed status=0x%02X\r\n", r);
            SendResponse((uint8_t *)MXT_WORK_STR, (uint16_t)strlen(MXT_WORK_STR));
        } else {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE,
                     "OK: ENCRESETBL app=0x%02X t6=0x%04X bl=0x%02X bl_status=0x%02X\r\n",
                     app, t6, bl, st);
            SendResponse((uint8_t *)MXT_WORK_STR, (uint16_t)strlen(MXT_WORK_STR));
        }
    }
    /* Command: "FINDBL [hint]" - 扫描 Bootloader 地址并读状态 E0（enc.txt 阶段 C） */
    else if (strncmp(cmd_str, "FINDBL", 6) == 0 || strncmp(cmd_str, "findbl", 6) == 0) {
        const char *arg = cmd_str + 6;
        while (*arg == ' ') {
            arg++;
        }
        uint8_t hint = 0U;
        if (*arg != '\0') {
            hint = (uint8_t)strtoul(arg, NULL, 16);
        }
        uint8_t bl = 0U, st = 0U;
        uint8_t r = MXT_ENC_FindBootloader(hint, &bl, &st);
        if (r != 0U) {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: FINDBL failed status=0x%02X\r\n", r);
            SendResponse((uint8_t *)MXT_WORK_STR, (uint16_t)strlen(MXT_WORK_STR));
        } else {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE,
                     "OK: FINDBL bootloader=0x%02X bl_status=0x%02X\r\n", bl, st);
            SendResponse((uint8_t *)MXT_WORK_STR, (uint16_t)strlen(MXT_WORK_STR));
        }
    }
    /* Command: "ENCENTERBL [hint]" - Reset to Bootloader, report BL addr + status */
    else if (strncmp(cmd_str, "ENCENTERBL", 10) == 0 || strncmp(cmd_str, "encenterbl", 10) == 0) {
        const char *arg = cmd_str + 10;
        while (*arg == ' ') {
            arg++;
        }
        uint8_t hint = 0U;
        if (*arg != '\0') {
            hint = (uint8_t)strtoul(arg, NULL, 16);
        }
        uint8_t app = 0U, bl = 0U, st = 0U;
        uint8_t r = MXT_ENC_PrepareEnterBootloader(hint, &app, &bl, &st);
        if (r != 0U) {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: ENCENTERBL failed status=0x%02X\r\n", r);
            SendResponse((uint8_t *)MXT_WORK_STR, (uint16_t)strlen(MXT_WORK_STR));
        } else {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE,
                     "OK: ENCENTERBL app=0x%02X bootloader=0x%02X bl_status=0x%02X\r\n",
                     app, bl, st);
            SendResponse((uint8_t *)MXT_WORK_STR, (uint16_t)strlen(MXT_WORK_STR));
        }
    }
    /* Command: "ENCUNLOCK" - Bootloader unlock (DC AA), wait 0xA0 */
    else if (strcmp(cmd_str, "ENCUNLOCK") == 0 || strcmp(cmd_str, "encunlock") == 0) {
        uint8_t st = 0U;
        uint8_t r = MXT_ENC_PrepareUnlock(&st);
        if (r != 0U) {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: ENCUNLOCK failed status=0x%02X\r\n", r);
            SendResponse((uint8_t *)MXT_WORK_STR, (uint16_t)strlen(MXT_WORK_STR));
        } else {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "OK: ENCUNLOCK bl_status=0x%02X ready\r\n", st);
            SendResponse((uint8_t *)MXT_WORK_STR, (uint16_t)strlen(MXT_WORK_STR));
        }
    }
#endif /* MXT_HAS_ENC */
    /* Command: "OBJTBL" - Read object table (addr 0x0007, 6 bytes per object); read per-entry to avoid long I2C read */
    else if (strcmp(cmd_str, "OBJTBL") == 0 || strcmp(cmd_str, "objtbl") == 0 ||
             strcmp(cmd_str, "OBJ") == 0 || strcmp(cmd_str, "obj") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        uint8_t n_obj = (g_num_objects > 0) ? g_num_objects : 40;
        if (n_obj > 64) n_obj = 64;
        USB_SendString("Object table:\r\n");
        uint8_t obj_entry[6];
        uint8_t fail = 0;
        for (uint8_t i = 0; i < n_obj && !fail; i++) {
            uint16_t obj_addr = 0x0007 + (uint16_t)i * 6;
            if (MXT_I2C_Read(g_mxt_i2c_addr, obj_addr, obj_entry, 6) != 0) {
                snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: Read object %d at 0x%04X failed\r\n", i, obj_addr);
                USB_SendString(MXT_WORK_STR);
                fail = 1;
                break;
            }
            uint8_t type = obj_entry[0];
            uint16_t addr = obj_entry[1] | (obj_entry[2] << 8);
            uint8_t size = obj_entry[3] + 1;
            uint8_t instances = obj_entry[4] + 1;
            uint8_t report_ids = obj_entry[5];
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "  T%2d @ 0x%04X size=%2d inst=%2d rid=%2d\r\n",
                     type, addr, size, instances, report_ids);
            USB_SendString(MXT_WORK_STR);
        }
    }
    /* Command: "MSGCNT" - Read message count (T44 addr from object table, 1 byte), ref maXTouch_Serial_Commands.md */
    else if (strcmp(cmd_str, "MSGCNT") == 0 || strcmp(cmd_str, "msgcnt") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        uint16_t t44_addr = MXT_GetObjectAddr(44);
        if (t44_addr == 0) {
            USB_SendString("ERR: T44 not found in object table\r\n");
        } else {
            uint8_t cnt;
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Reading T44 at addr=0x%04X...\r\n", t44_addr);
            USB_SendString(MXT_WORK_STR);
            if (MXT_I2C_Read(g_mxt_i2c_addr, t44_addr, &cnt, 1) != 0) {
                USB_SendString("ERR: Read message count failed\r\n");
            } else {
                snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Message count (T44): %d\r\n", cnt);
                USB_SendString(MXT_WORK_STR);
            }
        }
    }
    /* Command: "MSG" - Read one message (T5 addr from object table, 11 bytes), ref maXTouch_Serial_Commands.md */
    else if (strcmp(cmd_str, "MSG") == 0 || strcmp(cmd_str, "msg") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        uint16_t t5_addr = MXT_GetObjectAddr(5);
        if (t5_addr == 0) {
            USB_SendString("ERR: T5 not found in object table\r\n");
        } else {
            uint8_t msg_data[11];
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Reading T5 at addr=0x%04X...\r\n", t5_addr);
            USB_SendString(MXT_WORK_STR);
            if (MXT_I2C_Read(g_mxt_i2c_addr, t5_addr, msg_data, 11) != 0) {
                USB_SendString("ERR: Read message failed\r\n");
            } else {
                int pos = 0;
                pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "MSG raw: ");
                for (int j = 0; j < 11; j++)
                    pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "%02X ", msg_data[j]);
                pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "\r\n");
                pos = MXT_ParseMessage(MXT_WORK_STR, pos, (int)MXT_WORK_BUF_SIZE, msg_data[0], msg_data);
                pos += snprintf(MXT_WORK_STR + pos, MXT_WORK_BUF_SIZE - pos, "\r\n");
                USB_SendString(MXT_WORK_STR);
            }
        }
    }
    /* Command: "T100CFG" - Read T100 object and print UNKNOWN[9]/UNKNOWN[20] */
    else if (strcmp(cmd_str, "T100CFG") == 0 || strcmp(cmd_str, "t100cfg") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        MXT_ReadT100UnknownFields();
    }
    /* Command: "STATUS" - Query current status */
    else if (strcmp(cmd_str, "STATUS") == 0 || strcmp(cmd_str, "status") == 0) {
        snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE,
                 "Mode: %d, Diag Output: %s, Msg Output: %s\r\n"
                 "Config: X=%d, Y=%d, DiagMode: 0x%02X\r\n",
                 g_bridge_mode,
                 MXT_IsOutputEnabled() ? "ENABLED" : "DISABLED",
                 g_msg_output_enabled ? "ENABLED" : "DISABLED",
                 g_matrix_x_size,
                 g_matrix_y_size,
                 g_diag_mode);
        USB_SendString(MXT_WORK_STR);
    }
    /* Command: "SPI" - toggle SPI debug stream check */
    else if (strcmp(cmd_str, "SPI") == 0 || strcmp(cmd_str, "spi") == 0) {
        g_spi_stream_enabled = (uint8_t)!g_spi_stream_enabled;
        if (g_spi_stream_enabled) {
            MXT_SPI_PrepareStream(0U);
            USB_SendString("INFO: SPI stream START (raw hex)\r\n");
        } else {
            SPIUSB_TryFlush();
            if (g_spi_rx_overflow > 0U || g_spi_tx_drop > 0U) {
                snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE,
                         "WARN: SPISTOP rx_ovf=%u tx_drop=%lu\r\n",
                         (unsigned)g_spi_rx_overflow, (unsigned long)g_spi_tx_drop);
                USB_SendString(MXT_WORK_STR);
            }
            USB_SendString("INFO: SPI stream STOP\r\n");
        }
    }
    /* Command: "HELP" - Show available commands */
    else if (strcmp(cmd_str, "HELP") == 0 || strcmp(cmd_str, "help") == 0) {
        USB_SendString("Available commands:\r\n");
        USB_SendString("  [Mode]\r\n");
        USB_SendString("  mode0 = binary bridge (CFGWRITE/CFGREAD/hex frames)\r\n");
        USB_SendString("  mode1 = string command mode (help/status/start...)\r\n");
        USB_SendString("  Switch to mode1 from mode0:\r\n");
        USB_SendString("    1) Send text command: MODE1 (when text path is available)\r\n");
        USB_SendString("    2) Send fixed 4-byte sequence: 02 01 10 20\r\n");
        USB_SendString("  Use STATUS to check current mode\r\n");
        USB_SendString("  u        - Enter diagnostic dump menu\r\n");
        USB_SendString("  MODE0    - Switch to I2C-USB bridge mode\r\n");
        USB_SendString("  MODE1    - Switch to string mode\r\n");
        USB_SendString("  START    - Start diagnostic output (FRAME0, 1000ms timer)\r\n");
        USB_SendString("  START1   - Start diagnostic output (FRAME1 + pre-CAL, 1000ms timer)\r\n");
        USB_SendString("  STOP     - Stop diagnostic output\r\n");
        USB_SendString("  FRAME0   - Mutual Delta (0x10)\r\n");
        USB_SendString("  FRAME1   - Mutual Reference (0x11)\r\n");
        USB_SendString("  FRAME3   - Self Delta (0xF7)\r\n");
        USB_SendString("  FRAME4   - Self Reference (0xF8)\r\n");
        USB_SendString("  FRAME5   - Key Array / Self Signal (0xF5)\r\n");
        USB_SendString("  FRAME38  - Self DC Level (0x38)\r\n");
        USB_SendString("  MSG_ON   - Enable CHG message output\r\n");
        USB_SendString("  MSG_OFF  - Disable CHG message output\r\n");
        USB_SendString("  CHGON    - Enable CHG processing (read messages on CHG)\r\n");
        USB_SendString("  CHGOFF   - Disable CHG processing (default)\r\n");
        USB_SendString("  MAPALL   - Read one frame, output full matrix\r\n");
        USB_SendString("  MAP16    - Read one frame, output 16x16 (index 0-15)\r\n");
        USB_SendString("  MAP16R90 - 16x16 clockwise 90\r\n");
        USB_SendString("  MAP16L90 - 16x16 counterclockwise 90\r\n");
        USB_SendString("  MAP16X   - 16x16 flip X\r\n");
        USB_SendString("  MAP16Y   - 16x16 flip Y\r\n");
        USB_SendString("  MAP16XY  - 16x16 flip X and Y\r\n");
        USB_SendString("  START CHGNO [X|Y|XY] ms - Mode3 stream with [touch_id,x,y,action] from CHG\r\n");
        USB_SendString("  INFO     - Read Info Block (0x0000, 7 bytes)\r\n");
        USB_SendString("  FINDIIC  - Scan maXTouch I2C address\r\n");
#if MXT_HAS_ENC
        USB_SendString("  ENCRESETBL - T6 RESET=0xA5, force enter Bootloader\r\n");
        USB_SendString("  FINDBL [hint] - Scan Bootloader addr + read status\r\n");
        USB_SendString("  ENCENTERBL [hint] - ENCRESETBL + FINDBL (combined)\r\n");
        USB_SendString("  ENCUNLOCK - Bootloader unlock (DC AA)\r\n");
#endif
        USB_SendString("  BRIDGEBIN - Switch to binary I2C bridge (same as MODE0)\r\n");
        USB_SendString("  OBJTBL   - Read object table (0x0007, first 10 objects)\r\n");
        USB_SendString("  MSGCNT   - Read message count (T44 from object table)\r\n");
        USB_SendString("  MSG      - Read one message (T5 from object table)\r\n");
        USB_SendString("  T100CFG  - Read T100 UNKNOWN[9]/UNKNOWN[20]\r\n");
        USB_SendString("  STATUS   - Query current status\r\n");
        USB_SendString("  SPISTART  - SPI binary, 88 77 66 + LE u16 len + payload\r\n");
        USB_SendString("  SPISTART -no - SSN only (no SPI read/USB tx)\r\n");
        USB_SendString("  SPISTART1 - SPI crop 20x16 to 16x16 text rows\r\n");
        USB_SendString("  SPISTART3 - SPI 514B frame to Mode3 packets (AA 10 33)\r\n");
        USB_SendString("  SPISTOP   - Stop SPI stream\r\n");
        USB_SendString("  SPIDBG    - SSN/SPI diagnostic snapshot\r\n");
        USB_SendString("  SPI       - Toggle SPISTART raw stream\r\n");
        USB_SendString("  EXPORTTXT - Export compact config summary text\r\n");
        USB_SendString("  EXPORTBIN - Export config as compact binary stream\r\n");
        USB_SendString("  HELP     - Show this help message\r\n");
    }
    /* Unknown command */
    else {
        USB_SendString("ERROR: Unknown command. Type HELP for available commands\r\n");
    }

    MXT_FlushMessageBuffer();
}


void MXT_ProcessCommand(void)
{
  uint8_t guard = 0U;
  while (g_cmd_pending && guard < 4U) {
    ProcessPendingCommand();
    guard++;
  }
  if (g_cmd_queued != 0U) {
    g_cmd_queued = 0U;
    memcpy(g_cmd_buffer, g_cmd_queue, strlen(g_cmd_queue) + 1U);
    g_cmd_pending = 1U;
    ProcessPendingCommand();
  }
}

