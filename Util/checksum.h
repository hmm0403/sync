#ifndef __UTIL_CHECKSUM_H__
#define __UTIL_CHECKSUM_H__

#include "type_defines.h"

/* a non-zero CHAR_OFFSET makes the rolling sum stronger */
#define CHAR_OFFSET 0

uint32 get_checksum(char *buf1, int32 len);

#endif