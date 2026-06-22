#ifndef MXT_SPI_STREAM_H
#define MXT_SPI_STREAM_H

#include <stdint.h>

void SPIUSB_ResetState(uint8_t mode);
void SPIUSB_TryFlush(void);
void MXT_SPI_PrepareStream(uint8_t mode);
void MXT_SPI_StartIT(void);
void MXT_SPI_StopIT(void);
void MXT_ProcessSPICheck(void);
void MXT_USB_ServiceTx(void);

#endif /* MXT_SPI_STREAM_H */
