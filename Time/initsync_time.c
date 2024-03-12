#include <windows.h>
#include <tchar.h>
#include <time.h>
#include <stdio.h>
#include "initsync_time.h"
#include "type_defines.h"

#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS) || defined(__WATCOMC__)
#define DELTA_EPOCH_IN_USEC  11644473600000000Ui64
#else
#define DELTA_EPOCH_IN_USEC  11644473600000000ULL
#endif

static int win_gettimeofday(struct timeval *tv)
{
	FILETIME ft;
	uint64 now;

	if (tv) {
		GetSystemTimeAsFileTime(&ft);
		now = (uint64)ft.dwHighDateTime << 32;
		now |= ft.dwLowDateTime;
		now /= 10;
		now -= DELTA_EPOCH_IN_USEC;

		tv->tv_sec = (long)(now / 1000000L);
		tv->tv_usec = (long)(now % 1000000L);
	} else {
		return -1;
	}

	return 0;
}

uint64 get_unix_time_ms(void)
{
	struct timeval tv;
	uint64 unix_time;

	win_gettimeofday(&tv);
	unix_time = ((uint64)(tv.tv_sec) * (uint64)1000) + (uint64)(tv.tv_usec / 1000);

	return unix_time;
}

int get_gmt_time(char *buf, int len)
{
	struct tm cur;
	__time64_t time;

	_time64(&time);

	if (!_gmtime64_s(&cur, &time)) {
		strftime(buf, len, "%a, %d %b %Y %H:%M:%S GMT", &cur);
		return 0;
	}
	return -1;
}

int get_local_time(char *buf, int len)
{
	struct tm cur;
	__time64_t time;

	_time64(&time);

	if (!_gmtime64_s(&cur, &time)) {
		cur.tm_hour += 9;
		
		if(cur.tm_hour > 23)
			cur.tm_hour -= 24;

		strftime(buf, len, "%a, %d %b %Y %H:%M:%S GMT", &cur);
		return 0;
	}
	return -1;
}
