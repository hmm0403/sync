#ifndef __INCLUDE_CRT_DBG_H_
#define __INCLUDE_CRT_DBG_H_

#ifdef _DEBUG
#define CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#define malloc(a) _malloc_dbg(a,_NORMAL_BLOCK,__FILE__,__LINE__)
#endif

#endif
