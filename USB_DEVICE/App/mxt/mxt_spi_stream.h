#ifndef MXT_SPI_STREAM_H
#define MXT_SPI_STREAM_H
#include <stdint.h>
void SPIUSB_ResetState(uint8_t mode);
void SPIUSB_EndRawFrame(void);
void SPIUSB_TryFlush(void);
void SPIUSB_OnTxComplete(void);
void MXT_SPI_QueueGapMarker(void);
void MXT_SPI_OnSsnActive(void);
void MXT_SPI_StartIT(void);
void MXT_SPI_StopIT(void);
void MXT_ProcessSPICheck(void);
#endif
