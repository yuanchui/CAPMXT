#ifndef MXT_WORK_H
#define MXT_WORK_H

#include "mxt_config.h"
#include "mxt_state.h"

/* 单工作区别名：主循环单线程复用，勿在中断中使用 */
#define MXT_WORK_STR  ((char *)(void *)g_work_buf)
#define MXT_WORK_U8   (g_work_buf)

#endif /* MXT_WORK_H */
