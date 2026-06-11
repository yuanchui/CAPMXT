#ifndef MXT_MSG_H
#define MXT_MSG_H
#include <stdint.h>
int MXT_ParseMessage(char *msg_str, int pos, int max_len, uint8_t report_id, uint8_t *msg_data);
void MXT_SetChgPending(void);
void MXT_CheckAndProcessMessages(void);
#endif
