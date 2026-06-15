#include "mxt_bridge.h"
#include "mxt_state.h"
#include "mxt_work.h"
#include "mxt_config.h"
#include "mxt_i2c.h"
#include "mxt_usb_io.h"
#include "mxt_touch.h"
#include "mxt_cmd.h"
#include "mxt_enc.h"
#include "usbd_cdc_if.h"
#include "gpio.h"
#include <stdio.h>
#include <string.h>
#include "stm32f1xx_hal.h"

void MXT_ExportConfigAsTxt(void)
{
    if (!g_touch_inited) {
        MXT_InitTouchScreen();
    }
    if (!g_touch_inited) {
        USB_SendString("ERR: Touch not initialized\r\n");
        return;
    }

    uint8_t id_info[7] = {0};
    if (MXT_I2C_Read(g_mxt_i2c_addr, 0x0000, id_info, 7) != 0) {
        USB_SendString("ERR: Read Info Block failed\r\n");
        return;
    }

    uint8_t n_obj = id_info[6];
    if (n_obj == 0) {
        USB_SendString("ERR: Object count is 0\r\n");
        return;
    }
    if (n_obj > 96) n_obj = 96;

    USB_SendString("=== EXPORTTXT COMPACT START ===\r\n");
    snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ID family=%u variant=%u ver=%u build=%u objs=%u\r\n",
             id_info[0], id_info[1], id_info[2], id_info[3], n_obj);
    USB_SendString(MXT_WORK_STR);

    uint8_t obj_entry[6];
    uint16_t obj_table_start = 0x0007;

    for (uint8_t i = 0; i < n_obj; i++) {
        uint16_t entry_addr = (uint16_t)(obj_table_start + i * 6);
        if (MXT_I2C_Read(g_mxt_i2c_addr, entry_addr, obj_entry, 6) != 0) {
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "ERR: Read object table index=%u failed\r\n", i);
            USB_SendString(MXT_WORK_STR);
            break;
        }

        uint8_t obj_type = obj_entry[0];
        uint16_t obj_addr = (uint16_t)(obj_entry[1] | (obj_entry[2] << 8));
        uint16_t obj_size = (uint16_t)(obj_entry[3] + 1);
        uint8_t instances = (uint8_t)(obj_entry[4] + 1);
        uint8_t report_ids = obj_entry[5];

        snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE,
                 "T%u addr=0x%04X size=%u inst=%u rid=%u\r\n",
                 obj_type, obj_addr, obj_size, instances, report_ids);
        USB_SendString(MXT_WORK_STR);

        /* 导出期间主动冲刷，减小环形缓冲积压 */
        MXT_FlushMessageBuffer();
    }

    USB_SendString("=== EXPORTTXT COMPACT END ===\r\n");
}


void MXT_ExportConfigAsBin(void)
{
    if (!g_touch_inited) {
        MXT_InitTouchScreen();
    }
    if (!g_touch_inited) {
        USB_SendString("ERR: Touch not initialized\r\n");
        return;
    }

    uint8_t id_info[7] = {0};
    if (MXT_I2C_Read(g_mxt_i2c_addr, 0x0000, id_info, 7) != 0) {
        USB_SendString("ERR: Read Info Block failed\r\n");
        return;
    }

    uint8_t n_obj = id_info[6];
    if (n_obj == 0) {
        USB_SendString("ERR: Object count is 0\r\n");
        return;
    }
    if (n_obj > 96) n_obj = 96;

    /* 二进制导出帧格式：
     * START: [A0][family][variant][version][build][n_obj]
     * DATA : [A1][obj_type][inst][addr_l][addr_h][off_l][off_h][chunk_len][payload...]
     * END  : [A2][status]
     */
    MXT_WORK_U8[0] = 0xA0;
    MXT_WORK_U8[1] = id_info[0];
    MXT_WORK_U8[2] = id_info[1];
    MXT_WORK_U8[3] = id_info[2];
    MXT_WORK_U8[4] = id_info[3];
    MXT_WORK_U8[5] = n_obj;
    SendResponse(MXT_WORK_U8, 6);

    uint8_t obj_entry[6];
    uint16_t obj_table_start = 0x0007;
    uint8_t status = 0;

    for (uint8_t i = 0; i < n_obj; i++) {
        uint16_t entry_addr = (uint16_t)(obj_table_start + i * 6);
        if (MXT_I2C_Read(g_mxt_i2c_addr, entry_addr, obj_entry, 6) != 0) {
            status = 1;
            break;
        }

        uint8_t obj_type = obj_entry[0];
        uint16_t obj_addr = (uint16_t)(obj_entry[1] | (obj_entry[2] << 8));
        uint16_t obj_size = (uint16_t)(obj_entry[3] + 1);
        uint8_t instances = (uint8_t)(obj_entry[4] + 1);

        for (uint8_t inst = 0; inst < instances; inst++) {
            uint16_t inst_addr = (uint16_t)(obj_addr + (uint16_t)inst * obj_size);
            uint16_t offset = 0;
            while (offset < obj_size) {
                uint8_t chunk = (uint8_t)(obj_size - offset);
                if (chunk > 56) chunk = 56; /* 64B USB 包减去8B头 */

                if (MXT_I2C_Read(g_mxt_i2c_addr, (uint16_t)(inst_addr + offset), &MXT_WORK_U8[8], chunk) != 0) {
                    status = 2;
                    break;
                }

                MXT_WORK_U8[0] = 0xA1;
                MXT_WORK_U8[1] = obj_type;
                MXT_WORK_U8[2] = inst;
                MXT_WORK_U8[3] = (uint8_t)(inst_addr & 0xFF);
                MXT_WORK_U8[4] = (uint8_t)((inst_addr >> 8) & 0xFF);
                MXT_WORK_U8[5] = (uint8_t)(offset & 0xFF);
                MXT_WORK_U8[6] = (uint8_t)((offset >> 8) & 0xFF);
                MXT_WORK_U8[7] = chunk;
                SendResponse(MXT_WORK_U8, (uint16_t)(8 + chunk));
                offset = (uint16_t)(offset + chunk);
            }
            if (status != 0) break;
        }
        if (status != 0) break;
    }

    MXT_WORK_U8[0] = 0xA2;
    MXT_WORK_U8[1] = status;
    SendResponse(MXT_WORK_U8, 2);
}


static void CFG_SendResp(uint8_t resp_cmd, uint16_t seq, uint8_t status)
{
  uint8_t out[6];
  out[0] = resp_cmd;
  out[1] = (uint8_t)(seq & 0xFF);
  out[2] = (uint8_t)((seq >> 8) & 0xFF);
  out[3] = status;
  uint16_t crc = Map16_CalcCRC16(out, 4);
  out[4] = (uint8_t)(crc & 0xFF);
  out[5] = (uint8_t)((crc >> 8) & 0xFF);
  SendResponse(out, 6);
}


static uint8_t CFG_CheckCRC16LE(const uint8_t *frame, uint16_t frame_len)
{
  if (!frame || frame_len < 2) return 0;
  uint16_t crc_rx = frame[frame_len - 2] | ((uint16_t)frame[frame_len - 1] << 8);
  uint16_t crc_calc = Map16_CalcCRC16(frame, frame_len - 2);
  return (crc_rx == crc_calc) ? 1 : 0;
}


void ProcessBridgePacket(uint8_t *buf, uint32_t len)
{
    if (len < 1) return;

    /* mode0<->mode1：固定 4 字节序列 02 01 10 20
     *
     * 注意：USB CDC 可能把 4 字节拆成多次回调（len!=4）。
     * 旧逻辑要求 len==4 才能识别，可能导致切换失败，从而 help 在 mode0(binary) 下也看不到。
     * 这里改为“按字节连续匹配”，提高切换成功率。
     */
    static uint8_t mode_switch_state = 0;
    static const uint8_t mode_switch_seq[4] = {0x02, 0x01, 0x10, 0x20};
    uint8_t mode_switched = 0;
    if (g_cfg_rx_len == 0 && !g_cfgwrite_active && !g_cfgread_waiting_ack && !g_encwrite_active) {
        for (uint32_t i = 0; i < len; i++) {
            uint8_t b = buf[i];
            if (b == mode_switch_seq[mode_switch_state]) {
                mode_switch_state++;
            } else {
                mode_switch_state = (b == mode_switch_seq[0]) ? 1 : 0;
            }

            if (mode_switch_state >= 4) {
                mode_switch_state = 0;
                mode_switched = 1;

                if (g_bridge_mode == BRIDGE_MODE_BINARY) {
                    g_bridge_mode = BRIDGE_MODE_STRING;
                    g_menu_state = MENU_IDLE;
                    MXT_InitTouchScreen();

                    /* 切换后发送十六进制 02 01 10 20 */
                    uint8_t switch_hex[] = {0x02, 0x01, 0x10, 0x20};
                    SendResponse(switch_hex, 4);
                } else {
                    g_bridge_mode = BRIDGE_MODE_BINARY;
                    g_menu_state = MENU_IDLE;
                }
                break; /* 消耗序列，避免继续按本包其它协议解析 */
            }
        }
    }
    if (mode_switched) return;

    /* 默认字符串模式：仅当当前为桥模式且数据明显为二进制协议时才走二进制，否则优先按文本处理 */
    uint8_t as_binary = 0;
    if (g_bridge_mode == BRIDGE_MODE_BINARY) {
        /* mode0 下强制按二进制协议处理，不再识别字符串指令 (如 help, mode1 等)
         * 除非是前面已处理的特殊切换序列 02 01 10 20
         */
        as_binary = 1;
    } else {
        /* 字符串模式：仅明确多字节二进制包才走二进制（如 mxt-app 发来的 01 51...），单字节 0x82/0xE0 当文本 */
        if (buf[0] == REPORT_ID && len >= 2 && buf[1] == IIC_DATA_1) as_binary = 1;
        else if (buf[0] == IIC_DATA_1 && len >= 3) as_binary = 1;
        else if (buf[0] == CMD_CONFIG && len >= 3) as_binary = 1;
        /* CMD_READ_PINS(0x82)/CMD_FIND_IIC_ADDRESS(0xE0) 在字符串模式下不按二进制处理 */
    }

    if (!as_binary) {
        /* 仅在 mode1(BRIDGE_MODE_STRING) 下才会进入此分支 */
        uint8_t is_text = 1;
        for (uint32_t i = 0; i < len && i < 10; i++) {
            if (buf[i] < 0x20 && buf[i] != '\r' && buf[i] != '\n' && buf[i] != '\t') {
                is_text = 0;
                break;
            }
        }
        if (is_text) {
            ProcessStringCommand(buf, len);
            return;
        }
    }
    
    /* ENC 帧流式重组（单帧最大 276B，缓冲仅 ~292B） */
    if (as_binary && len > 0) {
        uint8_t maybe_enc = (buf[0] == ENC_START_CMD || buf[0] == ENC_FRAME_CMD || buf[0] == ENC_END_CMD);
        if ((g_enc_rx_len > 0 || maybe_enc) && g_cfg_rx_len == 0 && !g_cfgwrite_active) {
            uint32_t copy_len = len;
            uint32_t cap_left = (uint32_t)sizeof(g_enc_rx_buf) - g_enc_rx_len;
            if (copy_len > cap_left) {
                g_enc_rx_len = 0;
                MXT_ENC_Abort();
                CFG_SendResp(ENC_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
                return;
            }
            memcpy(&g_enc_rx_buf[g_enc_rx_len], buf, copy_len);
            g_enc_rx_len = (uint16_t)(g_enc_rx_len + copy_len);

            if (g_enc_rx_len < 1) return;

            uint16_t expected_len = 0;
            uint8_t enc_cmd = g_enc_rx_buf[0];
            if (enc_cmd == ENC_START_CMD) {
                expected_len = 8;
            } else if (enc_cmd == ENC_FRAME_CMD) {
                if (g_enc_rx_len < 7) return;
                uint16_t frame_len = g_enc_rx_buf[3] | ((uint16_t)g_enc_rx_buf[4] << 8);
                if (frame_len < 2 || frame_len > ENC_MAX_FRAME_BYTES) {
                    g_enc_rx_len = 0;
                    MXT_ENC_Abort();
                    CFG_SendResp(ENC_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
                    return;
                }
                expected_len = (uint16_t)(9 + frame_len);
            } else if (enc_cmd == ENC_END_CMD) {
                expected_len = 7;
            } else {
                g_enc_rx_len = 0;
            }

            if (expected_len > 0) {
                if (g_enc_rx_len < expected_len) return;
                buf = g_enc_rx_buf;
                len = expected_len;
                if (g_enc_rx_len > expected_len) {
                    uint16_t remain = (uint16_t)(g_enc_rx_len - expected_len);
                    memmove(g_enc_rx_buf, &g_enc_rx_buf[expected_len], remain);
                    g_enc_rx_len = remain;
                } else {
                    g_enc_rx_len = 0;
                }
            }
        }
    }

    /* CFG 帧是流式传输（USB CDC 会拆包），这里先做重组再进入原有校验流程 */
    if (as_binary && len > 0) {
        uint8_t maybe_cfg = (buf[0] == CFGWRITE_START_CMD || buf[0] == CFGWRITE_CHUNK_CMD || buf[0] == CFGWRITE_END_CMD ||
                             buf[0] == CFG_RESP_ACK_CMD || buf[0] == CFG_RESP_NACK_CMD ||
                             buf[0] == FREEZE_COMMAND || buf[0] == UNFREEZE_COMMAND || buf[0] == BACKUPNV_COMMAND);
        if (g_cfg_rx_len > 0 || maybe_cfg) {
            uint32_t copy_len = len;
            uint32_t cap_left = (uint32_t)sizeof(g_cfg_rx_buf) - g_cfg_rx_len;
            if (copy_len > cap_left) {
                /* 重组溢出：帧过大（常见为 START 对象表超过 CFG_RX_BUF_SIZE） */
                g_cfg_rx_len = 0;
                CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
                return;
            }
            memcpy(&g_cfg_rx_buf[g_cfg_rx_len], buf, copy_len);
            g_cfg_rx_len = (uint16_t)(g_cfg_rx_len + copy_len);

            if (g_cfg_rx_len < 1) return;

            uint16_t expected_len = 0;
            uint8_t stream_cmd = g_cfg_rx_buf[0];
            if (stream_cmd == CFGWRITE_START_CMD) {
                if (g_cfg_rx_len < 12) return;
                uint16_t total_objects = g_cfg_rx_buf[2] | ((uint16_t)g_cfg_rx_buf[3] << 8);
                if (total_objects == 0 || total_objects > CFG_MAX_OBJECTS) {
                    g_cfg_rx_len = 0;
                    CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
                    return;
                }
                expected_len = (uint16_t)(12 + 4 * total_objects);
            } else if (stream_cmd == CFGWRITE_CHUNK_CMD) {
                if (g_cfg_rx_len < 11) return;
                uint16_t chunk_len = g_cfg_rx_buf[7] | ((uint16_t)g_cfg_rx_buf[8] << 8);
                if (chunk_len == 0 || chunk_len > CFG_MAX_DATA_PER_FRAME) {
                    g_cfg_rx_len = 0;
                    CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
                    return;
                }
                expected_len = (uint16_t)(11 + chunk_len);
            } else if (stream_cmd == CFGWRITE_END_CMD) {
                expected_len = 7;
            } else if (stream_cmd == CFG_RESP_ACK_CMD || stream_cmd == CFG_RESP_NACK_CMD) {
                expected_len = 6;
            } else if (stream_cmd == FREEZE_COMMAND || stream_cmd == UNFREEZE_COMMAND || stream_cmd == BACKUPNV_COMMAND) {
                expected_len = 3;
            } else {
                /* 非 CFG 帧，清空重组缓冲，走原始分支 */
                g_cfg_rx_len = 0;
            }

            if (expected_len > 0) {
                if (g_cfg_rx_len < expected_len) return;
                buf = g_cfg_rx_buf;
                len = expected_len;
                if (g_cfg_rx_len > expected_len) {
                    uint16_t remain = (uint16_t)(g_cfg_rx_len - expected_len);
                    memmove(g_cfg_rx_buf, &g_cfg_rx_buf[expected_len], remain);
                    g_cfg_rx_len = remain;
                } else {
                    g_cfg_rx_len = 0;
                }
            }
        }
    }

    uint8_t cmd = buf[0];

    /* ==============================================================
     * CFGREAD host ACK/NACK: 只在 MCU 正在等待读回确认时处理
     * ==============================================================
     */
    if (g_cfgread_waiting_ack && (cmd == CFG_RESP_ACK_CMD || cmd == CFG_RESP_NACK_CMD)) {
        if (len == 6 && CFG_CheckCRC16LE(buf, 6)) {
            uint16_t seq = buf[1] | ((uint16_t)buf[2] << 8);
            uint8_t status = buf[3];
            if (seq == g_cfgread_current_seq) {
                g_cfgread_last_ack_status = status;
                g_cfgread_waiting_ack = 0;
            }
        }
        return;
    }

    /* ==============================================================
     * CFGWRITE/CFGREAD: host -> MCU 配置写入协议
     * ==============================================================
     */
    if (cmd == CFGWRITE_START_CMD) {
        /* 最小: D0 + ver(1) + total_objects(2) + total_chunks(2) + total_bytes(4) + crc(2) = 12 */
        if (len < 12) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }
        if (buf[1] != CFG_PROTOCOL_VERSION) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        uint16_t total_objects = buf[2] | ((uint16_t)buf[3] << 8);
        uint16_t total_chunks  = buf[4] | ((uint16_t)buf[5] << 8);
        /* total_bytes 当前仅用于协议占位校验，暂不参与后续处理 */
        (void)(((uint32_t)buf[6]) | ((uint32_t)buf[7] << 8) | ((uint32_t)buf[8] << 16) | ((uint32_t)buf[9] << 24));

        if (total_objects == 0 || total_objects > CFG_MAX_OBJECTS || total_chunks == 0) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        uint16_t expected_len = (uint16_t)(12 + 4 * total_objects);
        if (len != expected_len) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        if (!CFG_CheckCRC16LE(buf, expected_len)) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        /* 收到 START 前强制刷新 I2C 地址与对象表，避免默认地址不匹配导致 start 失败 */
        uint8_t found_addr = MXT_FindI2CAddress();
        if (found_addr == STATUS_NO_DEVICE) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
            return;
        }
        g_touch_inited = 0;
        MXT_InitTouchScreen();
        if (g_t6_addr == 0) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
            return;
        }

        /* 保存对象元数据（写入后用于读回导出） */
        for (uint16_t i = 0; i < total_objects; i++) {
            uint16_t p = (uint16_t)(10 + i * 4);
            uint16_t addr = buf[p] | ((uint16_t)buf[p + 1] << 8);
            uint16_t size = buf[p + 2] | ((uint16_t)buf[p + 3] << 8);
            g_cfgwrite_objects[i].addr = addr;
            g_cfgwrite_objects[i].size = size;
        }

        /* 冻结配置写入（等价 mxt-app FREEZE_COMMAND=0x22） */
        uint8_t freeze_cmd = FREEZE_COMMAND;
        uint8_t r = MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 1, &freeze_cmd, 1);
        if (r != 0) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, r);
            return;
        }

        g_cfgwrite_active = 1;
        g_cfgwrite_total_objects = total_objects;
        g_cfgwrite_total_chunks = total_chunks;
        g_cfgwrite_next_seq = 1;

        g_cfgread_waiting_ack = 0;
        g_cfgread_current_seq = 0;

        CFG_SendResp(CFG_RESP_ACK_CMD, 0, STATUS_OK);
        return;
    }

    if (cmd == CFGWRITE_CHUNK_CMD) {
        if (!g_cfgwrite_active) {
            uint16_t seq = (len >= 3) ? (buf[1] | ((uint16_t)buf[2] << 8)) : 0;
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }
        if (len < 11) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        uint16_t seq = buf[1] | ((uint16_t)buf[2] << 8);
        uint16_t obj_index = buf[3] | ((uint16_t)buf[4] << 8);
        uint16_t offset = buf[5] | ((uint16_t)buf[6] << 8);
        uint16_t chunk_len = buf[7] | ((uint16_t)buf[8] << 8);

        uint16_t expected_len = (uint16_t)(11 + chunk_len);
        if (chunk_len == 0 || expected_len != len || chunk_len > CFG_MAX_DATA_PER_FRAME) {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }

        if (!CFG_CheckCRC16LE(buf, expected_len)) {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }

        if (seq != g_cfgwrite_next_seq) {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }
        if (obj_index >= g_cfgwrite_total_objects) {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }
        if (offset + chunk_len > g_cfgwrite_objects[obj_index].size) {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }

        uint16_t write_addr = (uint16_t)(g_cfgwrite_objects[obj_index].addr + offset);
        uint8_t i2c_res = MXT_I2C_Write(g_mxt_i2c_addr, write_addr, &buf[9], chunk_len);
        if (i2c_res == 0) {
            g_cfgwrite_next_seq++;
            /* 每个对象最后一包返回 STATUS_OBJ_DONE，供主机显示“对象写入完成” */
            if ((uint16_t)(offset + chunk_len) == g_cfgwrite_objects[obj_index].size) {
                CFG_SendResp(CFG_RESP_ACK_CMD, seq, STATUS_OBJ_DONE);
            } else {
                CFG_SendResp(CFG_RESP_ACK_CMD, seq, STATUS_OK);
            }
        } else {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, i2c_res);
        }
        return;
    }

    if (cmd == CFGWRITE_END_CMD) {
        /* 帧格式: D2 | end_seq(u16) | reserved(u16) | crc16  => 固定 7 字节 */
        if (len != 7) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }
        if (!g_cfgwrite_active) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }
        if (!CFG_CheckCRC16LE(buf, 7)) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        uint16_t end_seq = buf[1] | ((uint16_t)buf[2] << 8);
        /* uint16_t reserved = buf[3] | (buf[4] << 8); */
        if (end_seq != (uint16_t)(g_cfgwrite_total_chunks + 1)) {
            CFG_SendResp(CFG_RESP_NACK_CMD, end_seq, STATUS_ADDR_NACK);
            return;
        }

        /* END: 仅确认写入完成，不做完整读回（避免占用链路并阻塞控制反馈） */
        CFG_SendResp(CFG_RESP_ACK_CMD, end_seq, STATUS_OK);
        g_cfgwrite_active = 0;
        g_cfgread_waiting_ack = 0;
        g_cfg_rx_len = 0;
        return;
    }

    /* ==============================================================
     * ENCWRITE: Host 流式下发 .enc 帧 → MCU 写 Bootloader @I2C
     * ==============================================================
     */
    if (cmd == ENC_START_CMD) {
        if (len != 8) {
            CFG_SendResp(ENC_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }
        if (!CFG_CheckCRC16LE(buf, 8)) {
            CFG_SendResp(ENC_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }
        if (buf[1] != ENC_PROTOCOL_VERSION) {
            CFG_SendResp(ENC_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        uint8_t bl_addr = buf[2];
        uint8_t flags = buf[3];
        uint16_t total_frames = buf[4] | ((uint16_t)buf[5] << 8);
        uint8_t r = MXT_ENC_Start(bl_addr, flags, total_frames);
        if (r == 0) {
            CFG_SendResp(ENC_RESP_ACK_CMD, 0, STATUS_OK);
        } else {
            CFG_SendResp(ENC_RESP_NACK_CMD, 0, r);
        }
        return;
    }

    if (cmd == ENC_FRAME_CMD) {
        if (!g_encwrite_active) {
            uint16_t seq = (len >= 3) ? (buf[1] | ((uint16_t)buf[2] << 8)) : 0;
            CFG_SendResp(ENC_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }
        if (len < 9) {
            CFG_SendResp(ENC_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        uint16_t seq = buf[1] | ((uint16_t)buf[2] << 8);
        uint16_t frame_len = buf[3] | ((uint16_t)buf[4] << 8);
        uint16_t expected_len = (uint16_t)(9 + frame_len);
        if (frame_len < 2 || expected_len != len || frame_len > ENC_MAX_FRAME_BYTES) {
            CFG_SendResp(ENC_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }
        if (!CFG_CheckCRC16LE(buf, expected_len)) {
            CFG_SendResp(ENC_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }
        if (seq != g_enc_next_seq) {
            CFG_SendResp(ENC_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }

        uint8_t r = MXT_ENC_SendFrame(&buf[5], frame_len, seq);
        if (r == 0) {
            g_enc_next_seq++;
            CFG_SendResp(ENC_RESP_ACK_CMD, seq, STATUS_OK);
        } else {
            CFG_SendResp(ENC_RESP_NACK_CMD, seq, r);
        }
        return;
    }

    if (cmd == ENC_END_CMD) {
        if (len != 7) {
            CFG_SendResp(ENC_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }
        if (!g_encwrite_active) {
            CFG_SendResp(ENC_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }
        if (!CFG_CheckCRC16LE(buf, 7)) {
            CFG_SendResp(ENC_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        uint16_t end_seq = buf[1] | ((uint16_t)buf[2] << 8);
        if (end_seq != g_enc_next_seq) {
            CFG_SendResp(ENC_RESP_NACK_CMD, end_seq, STATUS_ADDR_NACK);
            return;
        }

        uint8_t r = MXT_ENC_End(end_seq);
        if (r == 0) {
            CFG_SendResp(ENC_RESP_ACK_CMD, end_seq, STATUS_OK);
        } else {
            CFG_SendResp(ENC_RESP_NACK_CMD, end_seq, r);
        }
        return;
    }

    /* ==============================================================
     * 控制命令反馈：FREEZE/UNFREEZE/BACKUPNV
     * 帧格式: [cmd][crc16]，固定 3 字节
     * 返回: D3/D4(seq=0, status)
     * ==============================================================
     */
    if ((cmd == FREEZE_COMMAND || cmd == UNFREEZE_COMMAND || cmd == BACKUPNV_COMMAND) && len >= 3) {
        if (!CFG_CheckCRC16LE(buf, 3)) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        /* 确保地址和对象表有效，避免 T6 地址失效 */
        if (g_t6_addr == 0 || !g_touch_inited) {
            uint8_t found_addr = MXT_FindI2CAddress();
            if (found_addr == STATUS_NO_DEVICE) {
                CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
                return;
            }
            g_touch_inited = 0;
            MXT_InitTouchScreen();
            if (g_t6_addr == 0) {
                CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
                return;
            }
        }

        /* 如果正在执行 BACKUPNV（NVM 写入窗口），UNFREEZE 先延后到主循环处理
         * 避免在 USB 接收回调里做阻塞等待，也避免 NVM 还没写完就解除冻结导致异常状态。 */
        if (cmd == UNFREEZE_COMMAND && g_backup_busy && (HAL_GetTick() < g_backup_busy_until_ms)) {
            g_unfreeze_pending = 1;
            return; /* 不直接 ACK，由主循环到点后再写并 ACK */
        }

        uint8_t ctrl = cmd;
        uint8_t r = MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 1, &ctrl, 1);
        if (r == 0) {
            if (cmd == BACKUPNV_COMMAND) {
                /* BACKUPNV 触发后 NVM 写入可能需要时间 */
                g_backup_busy = 1;
                g_backup_busy_until_ms = HAL_GetTick() + 2000;
            }
            if (cmd == UNFREEZE_COMMAND) {
                /* UNFREEZE 后强制回字符串模式，确保 HELP/T100CFG 可直接响应 */
                g_bridge_mode = BRIDGE_MODE_STRING;
                g_menu_state = MENU_IDLE;
                g_cfg_rx_len = 0;
                g_backup_busy = 0;
                g_unfreeze_pending = 0;
            }
            CFG_SendResp(CFG_RESP_ACK_CMD, 0, STATUS_OK);
        } else {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, r);
        }
        return;
    }
    
    /*=========================================================================
     * 1. 带 REPORT_ID 的 I2C 读写命令: [01] [51] ...
     *    mxt-app 对非 bridge_chip 设备使用此格式
     *=========================================================================*/
    if (cmd == REPORT_ID && len >= 2 && buf[1] == IIC_DATA_1)
    {
        if (len >= 6) {
            uint16_t reg_addr = buf[4] | (buf[5] << 8);
            
            if (buf[2] == 2) {
                /* Read: [01] [51] [02] [count] [addr_l] [addr_h] */
                uint16_t count = buf[3];
                if (count > (APP_TX_DATA_SIZE - 3)) count = APP_TX_DATA_SIZE - 3;
                
                uint8_t i2c_res = MXT_I2C_Read(g_mxt_i2c_addr, reg_addr, &UserTxBufferFS[3], count);
                
                UserTxBufferFS[0] = REPORT_ID;
                UserTxBufferFS[1] = i2c_res ? STATUS_ADDR_NACK : STATUS_OK;
                UserTxBufferFS[2] = 0x00;
                
                SendResponse(UserTxBufferFS, count + 3);
            }
            else {
                /* Write: [01] [51] [2+count] [00] [addr_l] [addr_h] [data...] */
                uint16_t count = buf[2] - 2;
                if (count > (len - 6)) count = len - 6;
                
                uint8_t i2c_res = MXT_I2C_Write(g_mxt_i2c_addr, reg_addr, &buf[6], count);
                
                UserTxBufferFS[0] = REPORT_ID;
                UserTxBufferFS[1] = i2c_res ? STATUS_ADDR_NACK : STATUS_WRITE_OK;
                
                SendResponse(UserTxBufferFS, 2);
            }
        }
    }
    /*=========================================================================
     * 2. 不带 REPORT_ID 的 I2C 读写命令: [51] ...
     *    mxt-app 对 bridge_chip 设备使用此格式（也用于 Bootloader 模式）
     *=========================================================================*/
    else if (cmd == IIC_DATA_1 && len >= 3)
    {
        uint8_t write_len = buf[1];   /* 写入字节数 */
        uint16_t read_len = buf[2];   /* 读取字节数 */
        
        if (write_len == 2 && read_len > 0 && len >= 5) {
            /* Read with register: [51] [02] [count] [addr_l] [addr_h] */
            uint16_t reg_addr = buf[3] | (buf[4] << 8);
            if (read_len > (APP_TX_DATA_SIZE - 2)) read_len = (APP_TX_DATA_SIZE - 2);
            
            uint8_t i2c_res = MXT_I2C_Read(g_mxt_i2c_addr, reg_addr, &UserTxBufferFS[2], read_len);
            
            UserTxBufferFS[0] = i2c_res ? STATUS_ADDR_NACK : STATUS_OK;
            UserTxBufferFS[1] = 0x00;
            
            SendResponse(UserTxBufferFS, read_len + 2);
        }
        else if (write_len == 0 && read_len > 0) {
            /* Bootloader Read (no register): [51] [00] [count] */
            if (read_len > (APP_TX_DATA_SIZE - 2)) read_len = (APP_TX_DATA_SIZE - 2);
            
            uint8_t i2c_res = MXT_I2C_ReadNoReg(g_mxt_i2c_addr, &UserTxBufferFS[2], read_len);
            
            UserTxBufferFS[0] = i2c_res ? STATUS_ADDR_NACK : STATUS_OK;
            UserTxBufferFS[1] = 0x00;
            
            SendResponse(UserTxBufferFS, read_len + 2);
        }
        else if (write_len > 2 && read_len == 0 && len >= (3 + write_len)) {
            /* Write with register: [51] [2+count] [00] [addr_l] [addr_h] [data...] */
            uint16_t reg_addr = buf[3] | (buf[4] << 8);
            uint16_t count = write_len - 2;
            
            uint8_t i2c_res = MXT_I2C_Write(g_mxt_i2c_addr, reg_addr, &buf[5], count);
            
            UserTxBufferFS[0] = i2c_res ? STATUS_ADDR_NACK : STATUS_WRITE_OK;
            
            SendResponse(UserTxBufferFS, 1);
        }
        else if (write_len > 0 && read_len == 0 && len >= (3 + write_len)) {
            /* Bootloader Write (no register): [51] [count] [00] [data...] */
            uint8_t i2c_res = MXT_I2C_WriteNoReg(g_mxt_i2c_addr, &buf[3], write_len);
            
            UserTxBufferFS[0] = i2c_res ? STATUS_ADDR_NACK : STATUS_WRITE_OK;
            
            SendResponse(UserTxBufferFS, 1);
        }
    }
    /*=========================================================================
     * 3. 读取引脚状态: [82] (CMD_READ_PINS)
     *=========================================================================*/
    else if (cmd == CMD_READ_PINS)
    {
        UserTxBufferFS[0] = CMD_READ_PINS;
        UserTxBufferFS[1] = STATUS_OK;
        /* CHG 引脚状态在 bit 2 (0x04)，根据 mxt-app: chg = pkt[2] & 0x4 */
        UserTxBufferFS[2] = (HAL_GPIO_ReadPin(CHG_EXTI3_GPIO_Port, CHG_EXTI3_Pin) == GPIO_PIN_RESET) ? 0x00 : 0x04;
        
        SendResponse(UserTxBufferFS, 3);
    }
    /*=========================================================================
     * 4. 查找 I2C 地址: [E0] (CMD_FIND_IIC_ADDRESS)
     *=========================================================================*/
    else if (cmd == CMD_FIND_IIC_ADDRESS)
    {
        uint8_t found_addr = MXT_FindI2CAddress();
        
        UserTxBufferFS[0] = CMD_FIND_IIC_ADDRESS;
        UserTxBufferFS[1] = found_addr;  /* 找到的地址，或 0x81 表示未找到 */
        
        SendResponse(UserTxBufferFS, 2);
    }
    /*=========================================================================
     * 5. 配置命令: [80] (CMD_CONFIG)
     *    格式: [80] [speed] [i2c_addr|flags] [...]
     *=========================================================================*/
    else if (cmd == CMD_CONFIG && len >= 3)
    {
        /* 解析 I2C 地址配置（如果指定） */
        uint8_t addr_config = buf[2] & 0x7F;  /* 低 7 位是地址 */
        if (addr_config >= 0x24 && addr_config <= 0x4B) {
            g_mxt_i2c_addr = addr_config;
        }
        
        /* 检查是否设置了模式位 (最高位) */
        uint8_t mode_flag = buf[2] & 0x80;  /* 最高位作为模式标志 */
        if (mode_flag) {
            g_bridge_mode = BRIDGE_MODE_STRING;
            MXT_InitTouchScreen();
            USB_SendString("Bridge mode switched to STRING mode\r\n");
        } else {
            g_bridge_mode = BRIDGE_MODE_BINARY;
            USB_SendString("Bridge mode switched to I2C-USB mode\r\n");
        }
        
        /* 返回配置成功 */
        UserTxBufferFS[0] = CMD_CONFIG;
        UserTxBufferFS[1] = STATUS_OK;
        
        SendResponse(UserTxBufferFS, 2);
    }
    /*=========================================================================
     * 6. 模式切换命令: [FF] (自定义命令)
     *    格式: [FF] [mode] 
     *    mode: 0=桥模式, 1=字符串模式
     *=========================================================================*/
    else if (cmd == 0xFF && len >= 2)
    {
        uint8_t new_mode = buf[1];
        if(new_mode == 0) {
            g_bridge_mode = BRIDGE_MODE_BINARY;
            USB_SendString("Bridge Mode: I2C-USB bridge\r\n");
        } else if (new_mode == 1) {
            g_bridge_mode = BRIDGE_MODE_STRING;
            // 初始化触摸屏配置
            MXT_InitTouchScreen();
            USB_SendString("Bridge Mode: String mode\r\n");
            snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Config: X=%d, Y=%d\r\n", g_matrix_x_size, g_matrix_y_size);
            USB_SendString(MXT_WORK_STR);
        }
        
        /* 返回模式切换成功 */
        UserTxBufferFS[0] = 0xFF;
        UserTxBufferFS[1] = g_bridge_mode;
        
        SendResponse(UserTxBufferFS, 2);
    }
    /*=========================================================================
     * 7. 输出控制命令: [FE] (自定义命令)
     *    格式: [FE] [control] 
     *    control: 0=停止输出, 1=启动输出
     *=========================================================================*/
    else if (cmd == 0xFE && len >= 2)
    {
        uint8_t control = buf[1];
        if (control == 0) {
            MXT_EnableOutput(0);  /* 停止输出 */
            USB_SendString("Output: STOPPED\r\n");
        } else if (control == 1) {
            MXT_EnableOutput(1);  /* 启动输出 */
            USB_SendString("Output: STARTED\r\n");
        }
            
        /* 返回输出控制成功 */
        UserTxBufferFS[0] = 0xFE;
        UserTxBufferFS[1] = MXT_IsOutputEnabled();
            
        SendResponse(UserTxBufferFS, 2);
    }
}


void MXT_ProcessControlPending(void)
{
  /* SPI 流模式优先，暂停控制面的 I2C 操作。 */
  if (g_spi_stream_enabled != 0U) {
    return;
  }

  if (!g_unfreeze_pending) return;

  /* 等 BACKUPNV 结束时间窗到点后再真正执行 UNFREEZE */
  if (g_backup_busy && (HAL_GetTick() < g_backup_busy_until_ms)) return;

  /* 确保 T6 地址与触摸对象表有效 */
  if (g_t6_addr == 0 || !g_touch_inited) {
    uint8_t found_addr = MXT_FindI2CAddress();
    if (found_addr == STATUS_NO_DEVICE) {
      g_unfreeze_pending = 0;
      g_backup_busy = 0;
      CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
      return;
    }
    g_touch_inited = 0;
    MXT_InitTouchScreen();
    if (g_t6_addr == 0) {
      g_unfreeze_pending = 0;
      g_backup_busy = 0;
      CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
      return;
    }
  }

  uint8_t ctrl = UNFREEZE_COMMAND;
  uint8_t r = MXT_I2C_Write(g_mxt_i2c_addr, (uint16_t)(g_t6_addr + 1), &ctrl, 1);
  if (r == 0) {
    g_bridge_mode = BRIDGE_MODE_STRING;
    g_menu_state = MENU_IDLE;
    g_cfg_rx_len = 0; /* 清理 CFG 重组缓存，避免残片影响后续字符串/协议 */
    g_backup_busy = 0;
    g_unfreeze_pending = 0;
    CFG_SendResp(CFG_RESP_ACK_CMD, 0, STATUS_OK);
  } else {
    g_backup_busy = 0;
    g_unfreeze_pending = 0;
    CFG_SendResp(CFG_RESP_NACK_CMD, 0, r);
  }
}

