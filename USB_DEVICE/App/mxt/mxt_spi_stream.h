#ifndef MXT_SPI_STREAM_H
#define MXT_SPI_STREAM_H
#include <stdint.h>
void SPIUSB_ResetState(uint8_t mode);
void SPIUSB_EndRawFrame(void);
void SPIUSB_TryFlush(void);
void SPIUSB_OnTxComplete(void);
uint8_t SPIUSB_RawFlushPending(void);
uint8_t SPIUSB_RawHasPending(void);
void SPIUSB_RawStop(void);
void MXT_SPI_QueueGapMarker(void);
void MXT_SPI_QueueStartMarker(void);
void MXT_SPI_OnSsnActive(void);
void MXT_SPI_OnSsnGap(void);
void MXT_SPI_StartIT(void);
void MXT_SPI_StopIT(void);
uint16_t MXT_SPI_GetDmaWritePos(void);
void MXT_SPI_OnDmaProgress(void);
void MXT_ProcessSPICheck(void);
void MXT_USB_ServiceTx(void);
#endif
