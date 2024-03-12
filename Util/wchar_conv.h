#ifndef __UTIL_WCHAR_CONV_H_
#define __UTIL_WCHAR_CONV_H_

#include <sys/types.h>
#include <stdio.h>
#include <tchar.h>
#include "extern_c.h"
#include "crt_dbg.h"

EXTERN_C_START
wchar_t* CharToWChar(const char* pstrSrc);
wchar_t* CharToWChar2(const char* pstrSrc);
char* WCharToChar(const wchar_t* pwstrSrc);
EXTERN_C_END

#endif