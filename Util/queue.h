#ifndef __UTIL_QUEUE_H_
#define __UTIL_QUEUE_H_

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
	QUEUE_ENTRY_STATE_FREE,
	QUEUE_ENTRY_STATE_QUEUE
} queue_entry_state_t;

#define QUEUE_NAGLE_OFF	0
#define QUEUE_NAGLE_ON	1

typedef struct queue_t
{
	int nr_total;
	int nr_free;
	int nr_queue;

	int nagle;

	list_node_t free_list;
	list_node_t queue_list;

	HANDLE queue_mutex;
	HANDLE push_event;
	HANDLE pop_event;
}queue_t;

typedef struct queue_entry_t
{
	list_node_t lnode;

	queue_entry_state_t state;

	int len;

	void *data;
}queue_entry_t;

typedef void (*FQUEUEFREE)(const void*, const void*);

EXTERN_C_START
queue_t *init_queue(int nr_entry);
int destroy_queue(queue_t *q, void *pool, FQUEUEFREE ffree);
int flush_queue(queue_t *q, void *pool, FQUEUEFREE ffree);
int clear_queue(queue_t *q, void *pool, FQUEUEFREE ffree);
int clear_N_push_queue(queue_t *q, void *data, int len, void *pool, FQUEUEFREE ffree);
void wakeup_queue(queue_t *q);
//entry_data_type_t get_first_entry_type(queue_t *q, int blocking);
char *popQ(queue_t *q, int *len, int blocking);
int pushQ(queue_t *q, void *data, int len);//, entry_data_type_t type);
void set_queue_nagle(queue_t *q, int onoff);
void queue_pop_wakeup(queue_t *q);
void queue_push_wakeup(queue_t *q);
//queue_entry_t *get_first_entry(queue_t *q, int blocking_flag);
EXTERN_C_END

#endif