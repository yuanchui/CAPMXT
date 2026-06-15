#include "mxt_enc.h"
#include "mxt_config.h"
#include "mxt_state.h"
#include "mxt_i2c.h"
#include "mxt_touch.h"
#include "stm32f1xx_hal.h"

#define BL_POLL_DELAY_MS       5U
#define BL_FRAME_TIMEOUT_MS    15000U
#define BL_ENTER_DELAY_MS      400U

#define BL_STATUS_WAIT_CMD     0xE0U
#define BL_STATUS_WAIT_DATA    0xA0U
#define BL_STATUS_CRC_CHECK    0x02U
#define BL_STATUS_CRC_PASS     0x04U
#define BL_STATUS_CRC_FAIL     0x03U

static uint8_t g_enc_bl_addr = 0;

static uint8_t ENC_ProbeBootloaderAddr(uint8_t hint)
{
  if (hint >= 0x24U && hint <= 0x4BU && MXT_I2C_Probe(hint) == 0U) {
    return hint;
  }
  static const uint8_t addrs[] = {
    MXT_I2C_ADDR_BL_MXT640,
    MXT_I2C_ADDR_BL_HIGH,
    MXT_I2C_ADDR_BL_LOW,
    MXT_I2C_ADDR_BL_ALT
  };
  for (uint8_t i = 0; i < sizeof(addrs); i++) {
    if (MXT_I2C_Probe(addrs[i]) == 0U) {
      return addrs[i];
    }
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

uint8_t MXT_ENC_Start(uint8_t bl_addr_hint, uint8_t flags, uint16_t total_frames)
{
  if (g_cfgwrite_active) {
    return STATUS_ADDR_NACK;
  }
  if (total_frames == 0U) {
    return STATUS_ADDR_NACK;
  }

  g_encwrite_active = 1U;
  g_enc_total_frames = total_frames;
  g_enc_next_seq = 1U;
  g_enc_bl_addr = 0U;

  uint8_t bl = ENC_ProbeBootloaderAddr(bl_addr_hint);
  const uint8_t skip_enter = flags & 0x01U;

  if (bl == 0U || !skip_enter) {
    uint8_t app = ENC_ProbeAppAddr();
    if (app != 0U) {
      g_mxt_i2c_addr = app;
      g_touch_inited = 0U;
      MXT_InitTouchScreen();
      if (g_t6_addr == 0U) {
        g_encwrite_active = 0U;
        return STATUS_NO_DEVICE;
      }
      uint8_t reset = 0xA5U;
      if (MXT_I2C_Write(app, g_t6_addr, &reset, 1U) != 0U) {
        g_encwrite_active = 0U;
        return STATUS_ADDR_NACK;
      }
      HAL_Delay(BL_ENTER_DELAY_MS);
    }
    bl = ENC_ProbeBootloaderAddr(bl_addr_hint);
    if (bl == 0U) {
      g_encwrite_active = 0U;
      return STATUS_NO_DEVICE;
    }
  }

  g_enc_bl_addr = bl;
  g_mxt_i2c_addr = bl;

  uint8_t st = 0U;
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
  (void)seq;

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

    if (MXT_I2C_WriteNoReg(g_enc_bl_addr, frame, len) != 0U) {
      return STATUS_ADDR_NACK;
    }

    if (ENC_WaitStatus(BL_STATUS_CRC_CHECK, BL_FRAME_TIMEOUT_MS) != 0U) {
      continue;
    }
    if (ENC_WaitStatus(BL_STATUS_CRC_PASS, BL_FRAME_TIMEOUT_MS) != 0U) {
      continue;
    }
    if (ENC_WaitStatus(BL_STATUS_WAIT_DATA, BL_FRAME_TIMEOUT_MS) != 0U) {
      continue;
    }
    return 0U;
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
}
