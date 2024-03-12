#ifndef __UTIL_SCANDIR_H_
#define __UTIL_SCANDIR_H_

#ifdef WIN32
#include "crt_dbg.h"
#include <windows.h>
#include <stdio.h>
#include <aclapi.h>
#endif

#include "extern_c.h"
#include <sys/stat.h>

typedef struct scan_file_t
{
	TCHAR *name;
	DWORD dwVolumeSerialNumber;
	LARGE_INTEGER FileIndex;
}scan_file_t;

EXTERN_C_START
int scandir(TCHAR *dir, scan_file_t **files);
int str_dep_cmp(TCHAR *a, TCHAR *b);
EXTERN_C_END

#endif