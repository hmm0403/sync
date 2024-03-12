#ifndef __COMMUNICATION_NETWORK_EVENT_HANDLER_H_
#define __COMMUNICATION_NETWORK_EVENT_HANDLER_H_

#include "crt_dbg.h"
#include "extern_c.h"
#include "type_defines.h"
#include "time_event.h"
#include "winsock.h"

#define EVENT_FREQUENCY (10)

enum {
	/* Not Use		= 0x00     0000 0000 */
	EVENT_IDLE_STATE	= 0x01,	/* 0000 0001 */
	EVENT_RECV_STATE	= 0x02, /* 0000 0010 */
	EVENT_SEND_STATE	= 0x04, /* 0000 0100 */
	EVENT_CONN_STATE	= 0x08, /* 0000 1000 */
	EVENT_DELETE_STATE	= 0x10	/* 0001 0000 */
};

/* event map's entity */
typedef struct event_t {
	SOCKET sock;
	int state;
	void *data;

	void (*send_handler)(void *dis, SOCKET sock, void *data, int err);
	void (*recv_handler)(void *dis, SOCKET sock, void *data, int err);
	void (*conn_handler)(void *dis, SOCKET sock, void *data, int err);
} event_t;

typedef struct dispatcher_t {
	/* select fd sets */
	fd_set rset;
	fd_set wset;
	fd_set eset;

	/* event map */
	int max_event;
	event_t *event_map;
	int max_entry_idx;

	/* event handler loop's frequency */
	int freq;

	/* mutex */
	HANDLE mutex;

	/* time event */
	int max_time_event;
	int max_time_event_idx;
	time_event_t *time_event_map;

	int exit;
} dispatcher_t;


EXTERN_C_START
dispatcher_t *init_dispatcher(int nr_event);
int destroy_dispatcher(dispatcher_t *dis);
int start_dispatcher(dispatcher_t *dis);
int recv_regist(dispatcher_t *dis, SOCKET sock, void *data, void (*handler)(void *dis, SOCKET sock, void *data, int err));
int recv_unregist(dispatcher_t *dis, SOCKET sock);
int send_regist(dispatcher_t *dis, SOCKET sock, void *data, void (*handler)(void *dispatcher, SOCKET sock, void *data, int err));
int send_unregist(dispatcher_t *dis, SOCKET sock);
int conn_regist(dispatcher_t *dis, SOCKET sock, void *data, void (*handler)(void *dispatcher, SOCKET sock, void *data, int err), char *addr, uint16 port);
EXTERN_C_END


#endif
