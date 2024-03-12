#ifndef __UTIL_BUF_POOL_H_
#define __UTIL_BUF_POOL_H_

#ifdef WIN32
#include "crt_dbg.h"
#ifdef _MT
#include <windows.h>
#include <process.h>
#endif
#endif
#include "extern_c.h"
#include "list.h"


typedef enum {
	BUF_POOL_ENTRY_STATE_FREE,
	BUF_POOL_ENTRY_STATE_ALLOC
} buf_pool_entry_state_t;

typedef struct buf_pool_t
{
	int nr_total;
	int nr_free;
	int nr_alloc;

	int fixed_data_size;

	list_node_t free_list;
	list_node_t alloc_list;

	HANDLE pool_mutex;
	HANDLE pool_event;
}buf_pool_t;

typedef struct buf_pool_entry_t
{
	list_node_t lnode;

	buf_pool_entry_state_t state;

	char *buf;
}buf_pool_entry_t;

EXTERN_C_START
buf_pool_t *init_buf_pool(int nr_entry, int data_size);
int destroy_buf_pool(buf_pool_t *bp);
char *alloc_buf(buf_pool_t *bp, int *size);
void free_buf(const void *bp, const void *buf);
EXTERN_C_END

#endif