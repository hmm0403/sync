#ifndef __TIME_TIME_H_
#define __TIME_TIME_H_

#include "crt_dbg.h"
#include "extern_c.h"
#include "type_defines.h"

EXTERN_C_START
uint64 get_unix_time_ms(void);
int get_gmt_time(char *buf, int len);
int get_local_time(char *buf, int len);
EXTERN_C_END

#endif
