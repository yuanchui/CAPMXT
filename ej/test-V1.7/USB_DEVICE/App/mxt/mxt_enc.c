#include "mxt_enc.h"
#include "mxt_config.h"
#include "mxt_state.h"
#include "mxt_i2c.h"
#include "mxt_touch.h"
#include "gpio.h"
#include "main.h"
#include "stm32f1xx_hal.h"

#define BL_POLL_DELAY_MS       5U
#define BL_FRAME_TIMEOUT_MS    15000U
#define BL_ENTER_DELAY_MS      500U
#define BL_PROBE_RETRY_MS      10000U
#define BL_STATUS_READ_TRIES   16U
#define BL_CHG_WAIT_MS         3000U

#define BL_STATUS_WAIT_CMD     0xE0U
#define BL_STATUS_WAIT_DATA    0xA0U
#define BL_STATUS_CRC_CHECK    0x02U
#define BL_STATUS_CRC_PASS     0x04U
#define BL_STATUS_CRC_FAIL     0x03U

static uint8_t g_enc_bl_addr = 0;
static uint8_t g_enc_prepared = 0U;

static uint8_t ENC_IsBootloaderStatusValid(uint8_t st)
{
  if ((st & 0xC0U) == 0xC0U) {
    return 1U;
  }
  if (st == BL_STATUS_WAIT_DATA) {
    return 1U;
  }
  return 0U;
}

static uint8_t ENC_ComputeBootloaderHint(uint8_t app)
{
  if (app == MXT_I2C_ADDR_APP_HIGH || app == MXT_I2C_ADDR_APP_LOW) {
    return (uint8_t)(app - 0x24U);
  }
  return 0U;
}

/* 复位后等待 CHG 脉冲结束（低→高），与 mxt-app wait_for_chg 一致 */
static void ENC_WaitChgAfterReset(void)
{
  uint32_t start = HAL_GetTick();
  uint8_t saw_low = 0U;

  while ((HAL_GetTick() - start) < BL_CHG_WAIT_MS) {
    if (HAL_GPIO_ReadPin(CHG_EXTI3_GPIO_Port, CHG_EXTI3_Pin) == GPIO_PIN_RESET) {
      saw_low = 1U;
    } else if (saw_low != 0U) {
      return;
    }
    HAL_Delay(2U);
  }
}

/* enc.txt 阶段 C：对 BL 地址直接 read（不用 IsDeviceReady，BL 模式常 NACK probe） */
static uint8_t ENC_TryBootloaderAtAddr(uint8_t addr, uint8_t *out_status)
{
  uint8_t st = 0U;

  for (uint8_t attempt = 0U; attempt < BL_STATUS_READ_TRIES; attempt++) {
    if (MXT_I2C_ReadNoReg(addr, &st, 1U) == 0U && ENC_IsBootloaderStatusValid(st)) {
      if (out_status != NULL) {
        *out_status = st;
      }
      return addr;
    }
    HAL_Delay(BL_POLL_DELAY_MS);
  }
  return 0U;
}

static uint8_t ENC_ProbeBootloaderAddr(uint8_t hint, uint8_t *out_status)
{
  static const uint8_t addrs[] = {
    MXT_I2C_ADDR_BL_LOW,
    MXT_I2C_ADDR_BL_HIGH,
    MXT_I2C_ADDR_BL_ALT,
    MXT_I2C_ADDR_BL_MXT640
  };
  uint8_t found = 0U;
  uint8_t st = 0U;

  if (hint >= 0x24U && hint <= 0x4BU) {
    found = ENC_TryBootloaderAtAddr(hint, &st);
    if (found != 0U) {
      if (out_status != NULL) {
        *out_status = st;
      }
      return found;
    }
  }

  for (uint8_t i = 0U; i < sizeof(addrs); i++) {
    if (hint != 0U && addrs[i] == hint) {
      continue;
    }
    found = ENC_TryBootloaderAtAddr(addrs[i], &st);
    if (found != 0U) {
      if (out_status != NULL) {
        *out_status = st;
      }
      return found;
    }
  }
  return 0U;
}

/* T6 复位后 Bootloader 上电略慢，短时轮询避免过早判定 0x81 */
static uint8_t ENC_ProbeBootloaderAddrWithRetry(uint8_t hint, uint32_t retry_ms, uint8_t *out_status)
{
  uint32_t start = HAL_GetTick();
  while ((HAL_GetTick() - start) < retry_ms) {
    uint8_t bl = ENC_ProbeBootloaderAddr(hint, out_status);
    if (bl != 0U) {
      return bl;
    }
    HAL_Delay(50U);
  }
  return 0U;
}

static uint8_t ENC_ProbeAppAddr(void)
{
  if (MXT_I2C_Probe(MXT_I2C_ADDR_APP_HIGH) == 0U) return MXT_I2C_ADDR_APP_HIGH;
  if (MXT_I2C_Probe(MXT_I2C_ADDR_APP_LOW) == 0U) return MXT_I2C_ADDR_APP_LOW;
  return 0U;
}

static uint8_t ENC_PollStatus(uint8_t *status, uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  while ((HAL_GetTick() - start) < timeout_ms) {
    if (MXT_I2C_ReadNoReg(g_enc_bl_addr, status, 1U) == 0U) {
      return 0U;
    }
    HAL_Delay(BL_POLL_DELAY_MS);
  }
  return STATUS_ADDR_NACK;
}

/* enc.txt：大帧按 ~61B 分笔 I2C 续传，小帧一笔写完 */
static uint8_t ENC_WriteFrameChunked(const uint8_t *frame, uint16_t len)
{
  uint16_t off = 0U;

  while (off < len) {
    uint16_t chunk = (uint16_t)(len - off);
    if (chunk > ENC_BL_I2C_WRITE_CHUNK) {
      chunk = ENC_BL_I2C_WRITE_CHUNK;
    }
    if (MXT_I2C_WriteNoReg(g_enc_bl_addr, frame + off, chunk) != 0U) {
      return STATUS_ADDR_NACK;
    }
    off = (uint16_t)(off + chunk);
  }
  return 0U;
}

static uint8_t ENC_WaitStatus(uint8_t expected, uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint8_t st = 0U;

  while ((HAL_GetTick() - start) < timeout_ms) {
    if (ENC_PollStatus(&st, 50U) != 0U) {
      continue;
    }
    if (st == expected) {
      return 0U;
    }
    if (expected == BL_STATUS_WAIT_DATA && st == BL_STATUS_CRC_FAIL) {
      return BL_STATUS_CRC_FAIL;
    }
  }
  return STATUS_ADDR_NACK;
}

/* 写帧后轮询至 0x04（与 mxt-app send_frames 一致，不要求回到 0xA0） */
static uint8_t ENC_WaitCrcPassAfterWrite(uint8_t allow_reset_nack)
{
  uint32_t start = HAL_GetTick();
  uint8_t st = 0U;
  uint8_t saw_crc_check = 0U;

  while ((HAL_GetTick() - start) < BL_FRAME_TIMEOUT_MS) {
    if (ENC_PollStatus(&st, 50U) != 0U) {
      /* enc.txt 末帧：0x02→0x04 后芯片复位，0x27 开始 NACK */
      if (allow_reset_nack != 0U && saw_crc_check != 0U) {
        return 0U;
      }
      continue;
    }
    if (st == BL_STATUS_CRC_FAIL) {
      return BL_STATUS_CRC_FAIL;
    }
    if (st == BL_STATUS_CRC_CHECK) {
      saw_crc_check = 1U;
      continue;
    }
    if (st == BL_STATUS_CRC_PASS) {
      return 0U;
    }
    if (st == BL_STATUS_WAIT_DATA) {
      return 0U;
    }
  }
  if (allow_reset_nack != 0U && saw_crc_check != 0U) {
    return 0U;
  }
  return STATUS_ADDR_NACK;
}

static uint8_t ENC_ForceT6BootloaderReset(uint8_t *out_app, uint16_t *out_t6, uint8_t *out_bl, uint8_t *out_status)
{
  uint8_t app = ENC_ProbeAppAddr();
  if (out_app != NULL) {
    *out_app = app;
  }
  if (out_t6 != NULL) {
    *out_t6 = 0U;
  }
  if (out_bl != NULL) {
    *out_bl = 0U;
  }
  if (out_status != NULL) {
    *out_status = 0U;
  }

  if (app == 0U) {
    return STATUS_NO_DEVICE;
  }

  g_mxt_i2c_addr = app;

  /* mode1 已读过对象表时复用 T6 地址，避免复位前再次全表扫描 */
  if (g_t6_addr == 0U || !g_touch_inited) {
    g_touch_inited = 0U;
    MXT_InitTouchScreen();
  }
  if (g_t6_addr == 0U) {
    return STATUS_NO_DEVICE;
  }
  if (out_t6 != NULL) {
    *out_t6 = g_t6_addr;
  }

  uint8_t chg_was = g_chg_process_enabled;
  g_chg_process_enabled = 0U;

  uint8_t reset = 0xA5U;
  if (MXT_I2C_Write(app, g_t6_addr, &reset, 1U) != 0U) {
    g_chg_process_enabled = chg_was;
    return STATUS_ADDR_NACK;
  }

  ENC_WaitChgAfterReset();
  HAL_Delay(BL_ENTER_DELAY_MS);
  MXT_I2C_RecoverBus();
  HAL_Delay(50U);

  g_touch_inited = 0U;
  g_chg_process_enabled = chg_was;

  uint8_t bl_hint = ENC_ComputeBootloaderHint(app);
  uint8_t st = 0U;
  uint8_t bl = ENC_ProbeBootloaderAddrWithRetry(bl_hint, BL_PROBE_RETRY_MS, &st);
  if (bl != 0U) {
    g_enc_bl_addr = bl;
    g_mxt_i2c_addr = bl;
    if (out_bl != NULL) {
      *out_bl = bl;
    }
    if (out_status != NULL) {
      *out_status = st;
    }
    return 0U;
  }

  if (MXT_I2C_Probe(app) == 0U) {
    return STATUS_STILL_IN_APP;
  }
  return STATUS_NO_DEVICE;
}

uint8_t MXT_ENC_IsPrepared(void)
{
  return g_enc_prepared;
}

void MXT_ENC_ClearPrepared(void)
{
  g_enc_prepared = 0U;
}

uint8_t MXT_ENC_ForceResetToBootloader(uint8_t *out_app, uint16_t *out_t6, uint8_t *out_bl, uint8_t *out_status)
{
  g_enc_prepared = 0U;
  return ENC_ForceT6BootloaderReset(out_app, out_t6, out_bl, out_status);
}

uint8_t MXT_ENC_FindBootloader(uint8_t bl_hint, uint8_t *out_bl, uint8_t *out_status)
{
  uint8_t st = 0U;
  uint8_t bl = ENC_ProbeBootloaderAddrWithRetry(bl_hint, BL_PROBE_RETRY_MS, &st);
  if (bl == 0U) {
    return STATUS_NO_DEVICE;
  }

  g_enc_bl_addr = bl;
  g_mxt_i2c_addr = bl;
  g_enc_prepared = 0U;

  if (out_bl != NULL) {
    *out_bl = bl;
  }
  if (out_status != NULL) {
    *out_status = st;
  }
  return 0U;
}

uint8_t MXT_ENC_PrepareEnterBootloader(uint8_t bl_hint, uint8_t *out_app, uint8_t *out_bl, uint8_t *out_status)
{
  if (out_app != NULL) {
    *out_app = 0U;
  }
  if (out_bl != NULL) {
    *out_bl = 0U;
  }
  if (out_status != NULL) {
    *out_status = 0U;
  }

  g_enc_prepared = 0U;
  g_enc_bl_addr = 0U;

  uint8_t app = 0U;
  uint16_t t6 = 0U;
  uint8_t r = ENC_ForceT6BootloaderReset(&app, &t6, out_bl, out_status);
  if (r == STATUS_NO_DEVICE && app == 0U) {
    return MXT_ENC_FindBootloader(bl_hint, out_bl, out_status);
  }
  if (r != 0U) {
    return r;
  }

  if (out_app != NULL) {
    *out_app = app;
  }
  return 0U;
}

uint8_t MXT_ENC_PrepareUnlock(uint8_t *out_status)
{
  if (g_enc_bl_addr == 0U) {
    return STATUS_NO_DEVICE;
  }

  uint8_t unlock[2] = {0xDCU, 0xAAU};
  if (MXT_I2C_WriteNoReg(g_enc_bl_addr, unlock, 2U) != 0U) {
    return STATUS_ADDR_NACK;
  }

  uint8_t st = 0U;
  if (ENC_WaitStatus(BL_STATUS_WAIT_DATA, 5000U) != 0U) {
    return STATUS_ADDR_NACK;
  }
  if (ENC_PollStatus(&st, 500U) == 0U && out_status != NULL) {
    *out_status = st;
  }
  g_enc_prepared = 1U;
  return 0U;
}

uint8_t MXT_ENC_Start(uint8_t bl_addr_hint, uint8_t flags, uint16_t total_frames)
{
  if (g_cfgwrite_active) {
    return STATUS_ADDR_NACK;
  }
  if (total_frames == 0U) {
    return STATUS_ADDR_NACK;
  }

  const uint8_t skip_enter = flags & 0x01U;
  if (skip_enter && g_enc_prepared && g_enc_bl_addr != 0U) {
    g_encwrite_active = 1U;
    g_enc_total_frames = total_frames;
    g_enc_next_seq = 1U;
    g_mxt_i2c_addr = g_enc_bl_addr;
    return 0U;
  }

  g_encwrite_active = 1U;
  g_enc_total_frames = total_frames;
  g_enc_next_seq = 1U;
  g_enc_bl_addr = 0U;
  g_enc_prepared = 0U;

  uint8_t st = 0U;
  uint8_t bl = ENC_ProbeBootloaderAddr(bl_addr_hint, &st);

  if (bl == 0U || !skip_enter) {
    uint8_t app = 0U;
    uint16_t t6 = 0U;
    uint8_t bl_tmp = 0U;
    uint8_t r = ENC_ForceT6BootloaderReset(&app, &t6, &bl_tmp, &st);
    if (r != 0U && r != STATUS_NO_DEVICE) {
      g_encwrite_active = 0U;
      return r;
    }
    if (bl_tmp != 0U) {
      bl = bl_tmp;
    } else {
      bl = ENC_ProbeBootloaderAddrWithRetry(bl_addr_hint, BL_PROBE_RETRY_MS, &st);
    }
    if (bl == 0U) {
      g_encwrite_active = 0U;
      return STATUS_NO_DEVICE;
    }
  }

  g_enc_bl_addr = bl;
  g_mxt_i2c_addr = bl;

  ENC_PollStatus(&st, 3000U);

  uint8_t unlock[2] = {0xDCU, 0xAAU};
  if (MXT_I2C_WriteNoReg(g_enc_bl_addr, unlock, 2U) != 0U) {
    g_encwrite_active = 0U;
    return STATUS_ADDR_NACK;
  }

  if (ENC_WaitStatus(BL_STATUS_WAIT_DATA, 5000U) != 0U) {
    g_encwrite_active = 0U;
    return STATUS_ADDR_NACK;
  }

  g_enc_prepared = 1U;
  return 0U;
}

uint8_t MXT_ENC_SendFrame(const uint8_t *frame, uint16_t len, uint16_t seq)
{
  if (!g_encwrite_active || frame == NULL || len < 2U || g_enc_bl_addr == 0U) {
    return STATUS_ADDR_NACK;
  }
  if (len > ENC_MAX_FRAME_BYTES) {
    return STATUS_ADDR_NACK;
  }

  const uint8_t is_last_frame =
      (g_enc_total_frames != 0U && seq >= g_enc_total_frames) ? 1U : 0U;

  for (uint8_t attempt = 0U; attempt < 3U; attempt++) {
    uint8_t st = 0U;
    if (ENC_PollStatus(&st, BL_FRAME_TIMEOUT_MS) != 0U) {
      continue;
    }
    if (st == BL_STATUS_CRC_FAIL) {
      ENC_WaitStatus(BL_STATUS_WAIT_DATA, 2000U);
      continue;
    }
    if (st != BL_STATUS_WAIT_DATA) {
      continue;
    }

    if (ENC_WriteFrameChunked(frame, len) != 0U) {
      return STATUS_ADDR_NACK;
    }

    st = ENC_WaitCrcPassAfterWrite(is_last_frame);
    if (st == BL_STATUS_CRC_FAIL) {
      ENC_WaitStatus(BL_STATUS_WAIT_DATA, 2000U);
      continue;
    }
    if (st == 0U) {
      if (is_last_frame != 0U) {
        /* enc.txt 阶段 F：末帧 CRC 0x04 后芯片复位，Host 可能收不到 ENC END ACK；
         * 在此结束 ENC 会话并回到字符串桥，避免 g_encwrite_active 卡住模式切换。 */
        g_encwrite_active = 0U;
        g_enc_rx_len = 0U;
        g_enc_prepared = 0U;
        g_bridge_mode = BRIDGE_MODE_STRING;
        g_menu_state = MENU_IDLE;
      }
      return 0U;
    }
  }

  return STATUS_ADDR_NACK;
}

uint8_t MXT_ENC_End(uint16_t end_seq)
{
  (void)end_seq;
  if (!g_encwrite_active) {
    return STATUS_ADDR_NACK;
  }

  g_encwrite_active = 0U;
  g_enc_rx_len = 0U;
  g_enc_prepared = 0U;
  HAL_Delay(500U);

  g_bridge_mode = BRIDGE_MODE_STRING;
  g_menu_state = MENU_IDLE;

  return 0U;
}

void MXT_ENC_Abort(void)
{
  g_encwrite_active = 0U;
  g_enc_rx_len = 0U;
  g_enc_bl_addr = 0U;
  g_enc_prepared = 0U;
}
