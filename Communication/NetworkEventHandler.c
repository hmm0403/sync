#include <winsock2.h>
#include <string.h>
#include "type_defines.h"
#include "network_api.h"
#include "NetworkEventHandler.h"
#include "initsync_time.h"
#include "defines.h"

static void event_null_handler(void *dispatcher, SOCKET sock, void *data, int err) {}

dispatcher_t *init_dispatcher(int nr_event)
{
	int idx;
	dispatcher_t *dis;

	dis = (dispatcher_t *)malloc(sizeof(dispatcher_t));
	if (!dis)
		return NULL;

	memset(dis, 0, sizeof(dispatcher_t));

	FD_ZERO(&dis->rset);
	FD_ZERO(&dis->wset);
	FD_ZERO(&dis->eset);

	dis->max_event = FD_SETSIZE;
	dis->event_map = malloc(sizeof(event_t) * dis->max_event);
	if (!dis->event_map)
		return NULL;

	for (idx = 0; idx < dis->max_event; idx++) {
		dis->event_map[idx].sock = INVALID_SOCKET;
		dis->event_map[idx].state = EVENT_IDLE_STATE;
		dis->event_map[idx].data = NULL;
		dis->event_map[idx].send_handler = event_null_handler;
		dis->event_map[idx].recv_handler = event_null_handler;
		dis->event_map[idx].conn_handler = event_null_handler;
	}

	dis->max_entry_idx = -1;
	dis->freq = EVENT_FREQUENCY;

	/* mutex init */
	dis->mutex = CreateMutex(NULL, FALSE, NULL);
	if(!dis->mutex)
	{
		SAFE_FREE(dis->event_map);
		return NULL;
	}

	dis->time_event_map = time_event_init(dis);
	if (!dis->time_event_map) {
		SAFE_FREE(dis->event_map);
		return NULL;
	}

	dis->exit = 0;

	return dis;

}

int destroy_dispatcher(dispatcher_t *dis)
{
	if (dis) {
		if (dis->event_map)
			SAFE_FREE(dis->event_map);
		if (dis->time_event_map)
			time_event_destroy(dis);
		SAFE_FREE(dis);
	}
	return 0;
}

static int get_free_event_map(dispatcher_t *dis)
{
	int idx;

	for (idx = 0; idx < dis->max_event; idx++) {
		if (dis->event_map[idx].sock == INVALID_SOCKET)
			break;
	}
	if (idx == dis->max_event)
		return -1;

	return idx;
}

static int search_event_map(dispatcher_t *dis, SOCKET sock, int state)
{
	int idx;

	for (idx = 0; idx <= dis->max_entry_idx; idx++) {
		if (dis->event_map[idx].sock == sock && dis->event_map[idx].state == state)
			return idx;
	}
	return -1;
}

static int search_max_map(dispatcher_t *dis, int pos)
{
	int idx;

	for (idx = pos - 1; idx >= 0; idx--) {
		if (dis->event_map[idx].sock != INVALID_SOCKET)
			break;
	}
	return idx;
}

static int reset_event_etnry(event_t *event_entry)
{
	if (event_entry == NULL)
		return -1;

	event_entry->sock = INVALID_SOCKET;
	event_entry->state = EVENT_IDLE_STATE;
	event_entry->data = NULL;

	event_entry->recv_handler = event_null_handler;
	event_entry->send_handler = event_null_handler;
	event_entry->conn_handler = event_null_handler;
	return 0;
}

/* recv_regist */
int recv_regist(dispatcher_t *dis, SOCKET sock, void *data, void (*handler)(void *dis, SOCKET sock, void *data, int err))
{
	int idx;

	if (dis == NULL || sock == INVALID_SOCKET || data == NULL || handler == NULL)
		return -1;

	/* LOCK */
	WaitForSingleObject(dis->mutex, INFINITE);
	idx = get_free_event_map(dis);
	if (idx < 0) {
		/* UNLOCK */
		ReleaseMutex(dis->mutex);
		return -1;
	}
	
	if (dis->max_entry_idx < idx)
		dis->max_entry_idx = idx;
		
	dis->event_map[idx].sock = sock;
	/* UNLOCK for fast unlocking */
	dis->event_map[idx].state = EVENT_RECV_STATE;
	dis->event_map[idx].data = data;
	dis->event_map[idx].recv_handler = handler;
	/* or UNLOCK here */
	ReleaseMutex(dis->mutex);
	
	return 0;
}

int recv_unregist(dispatcher_t *dis, SOCKET sock)
{
	int idx;

	if (dis == NULL || sock == INVALID_SOCKET)
		return -1;
	
	/* LOCK */
	WaitForSingleObject(dis->mutex, INFINITE);
	idx = search_event_map(dis, sock, EVENT_RECV_STATE);
	if (idx < 0){
		/* UNLOCK */
		ReleaseMutex(dis->mutex);
		return -1;
	}

	if (dis->max_entry_idx <= idx) {
		dis->max_entry_idx = search_max_map(dis, idx);
	}

	reset_event_etnry(&dis->event_map[idx]);
	/* UNLOCK */
	ReleaseMutex(dis->mutex);

	return 0;
}

int send_regist(dispatcher_t *dis, SOCKET sock, void *data, void (*handler)(void *dispatcher, SOCKET sock, void *data, int err))
{
	int idx;

	if (dis == NULL || sock == INVALID_SOCKET || data == NULL || handler == NULL)
		return -1;

	/* LOCK */
	WaitForSingleObject(dis->mutex, INFINITE);
	idx = get_free_event_map(dis);
	if (idx < 0){
		/* UNLOCK */
		ReleaseMutex(dis->mutex);
		return -1;
	}

	if (dis->max_entry_idx < idx)
		dis->max_entry_idx = idx;

	dis->event_map[idx].sock = sock;
	dis->event_map[idx].state = EVENT_SEND_STATE;
	dis->event_map[idx].data = data;
	dis->event_map[idx].send_handler = handler;
	/* UNLOCK */
	ReleaseMutex(dis->mutex);

	return 0;
}

int send_unregist(dispatcher_t *dis, SOCKET sock)
{
	int idx;

	if (dis == NULL || sock == INVALID_SOCKET)
		return -1;
	
	/* LOCK */
	WaitForSingleObject(dis->mutex, INFINITE);

	do{
		idx = search_event_map(dis, sock, EVENT_SEND_STATE);
		if (idx < 0){
			/* UNLOCK */
			ReleaseMutex(dis->mutex);
			return -1;
		}

		if (dis->max_entry_idx <= idx) {
			dis->max_entry_idx = search_max_map(dis, idx);
		}

		reset_event_etnry(&dis->event_map[idx]);
	}while(idx != -1);

	/* UNLOCK */
	ReleaseMutex(dis->mutex);

	return 0;
}

int conn_regist(dispatcher_t *dis, SOCKET sock, void *data, void (*handler)(void *dispatcher, SOCKET sock, void *data, int err), char *addr, uint16 port)
{
	int ret;
	int idx;
	int error;

	if (dis == NULL || sock == INVALID_SOCKET || data == NULL || handler == NULL || addr == NULL)
		return -1;

	ret = net_connect(sock, addr, port);
	if (ret == 0) {
		handler(dis, sock, data, 0);
		return 0;
	} else if (ret == SOCKET_ERROR) {
		error =  WSAGetLastError();
		if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY) {
		
			/* LOCK */
			WaitForSingleObject(dis->mutex, INFINITE);
			idx = get_free_event_map(dis);
			if (idx < 0)
				return -1;

			if (dis->max_entry_idx < idx)
				dis->max_entry_idx = idx;

			dis->event_map[idx].sock = sock;
			dis->event_map[idx].conn_handler = handler;
			dis->event_map[idx].state = EVENT_CONN_STATE;
			dis->event_map[idx].data = data;
			/* UNLOCK */
			ReleaseMutex(dis->mutex);

		} else {
			return -1;
		}

	} else {
		return -1;
	}

	return 0;
}


static int setup_events(dispatcher_t *dis)
{
	int idx;
	int max_idx;

	/* LOCK */
	WaitForSingleObject(dis->mutex, INFINITE);
	max_idx = dis->max_entry_idx;
	/* UNLOCK */
	ReleaseMutex(dis->mutex);

	for (idx = 0; idx <= max_idx; idx++) {
		if (dis->event_map[idx].state & EVENT_DELETE_STATE) {
			/* LOCK */
			WaitForSingleObject(dis->mutex, INFINITE);
			reset_event_etnry(&dis->event_map[idx]);
			if (idx == dis->max_entry_idx)
				dis->max_entry_idx = search_max_map(dis, idx);
			/* UNLOCK */
			ReleaseMutex(dis->mutex);
		} else if (dis->event_map[idx].state & EVENT_RECV_STATE) {
			FD_SET(dis->event_map[idx].sock, &dis->rset);

		} else if (dis->event_map[idx].state & EVENT_SEND_STATE) {
			FD_SET(dis->event_map[idx].sock, &dis->wset);

		} else if (dis->event_map[idx].state & EVENT_CONN_STATE) {
			FD_SET(dis->event_map[idx].sock, &dis->wset);
			FD_SET(dis->event_map[idx].sock, &dis->eset);

		} else if (dis->event_map[idx].state & EVENT_IDLE_STATE) {
			/* no-op */
		} else {
			return -1;
		}
	}
	
	return 0;
}

static int process_events(dispatcher_t *dis, int nr_events)
{
	int idx;
	int nr_loop;
	event_t *event_entry;

	/* LOCK */
	WaitForSingleObject(dis->mutex, INFINITE);
	nr_loop = dis->max_entry_idx;
	ReleaseMutex(dis->mutex);
	/* UNLOCK */
	for (idx = 0; idx <= nr_loop; idx++) {
		event_entry = &dis->event_map[idx];
		if (event_entry->sock == INVALID_SOCKET)
			continue;
		if (event_entry->state & EVENT_DELETE_STATE)
			continue;
		if (event_entry->state & EVENT_RECV_STATE) {
			if (FD_ISSET(event_entry->sock, &dis->rset))
				event_entry->recv_handler(dis, event_entry->sock, event_entry->data, 0);

		} else if (event_entry->state & EVENT_SEND_STATE) {
			if (FD_ISSET(event_entry->sock, &dis->wset)) {
				event_entry->send_handler(dis, event_entry->sock, event_entry->data, 0);
				event_entry->state |= EVENT_DELETE_STATE;
			}

		} else if (event_entry->state & EVENT_CONN_STATE) {
			if (FD_ISSET(event_entry->sock, &dis->wset)) {
				event_entry->state |= EVENT_DELETE_STATE;
				event_entry->conn_handler(dis, event_entry->sock, event_entry->data, 0);
			} else if (FD_ISSET(event_entry->sock, &dis->eset)) {
				event_entry->state |= EVENT_DELETE_STATE;
				event_entry->conn_handler(dis, event_entry->sock, event_entry->data, -1);
			}

		} else if (event_entry->state & EVENT_IDLE_STATE) {
			/* no-op */
		} else {
			return -1;
		}
	}
	return 0;
}

int start_dispatcher(dispatcher_t *dis)
{
	int ret;
	int nr_events = 0;
	uint64 prev, now, next, tick;
	

	now = get_unix_time_ms();
	prev = now;
	next = now + dis->freq;

	for (;;) {
		if(dis->exit == 1){
			break;
		}

		FD_ZERO(&dis->rset);
		FD_ZERO(&dis->wset);
		FD_ZERO(&dis->eset);
		ret = setup_events(dis);
		if (ret < 0)
			return -1;
		nr_events = net_select(0, &dis->rset, &dis->wset, &dis->eset, dis->freq);
		if (nr_events > 0)
			process_events(dis, nr_events);

		// tick: elapsed time in ms
		now = get_unix_time_ms();
		tick = now - prev;
		prev = now;

		if (next <= now) {
			time_event_handler_invoker(dis, tick);
			next += dis->freq;

			while (next <= now) {
				now = get_unix_time_ms();
				tick = now - prev;
				prev = now;
				time_event_handler_invoker(dis, tick);
				next += dis->freq;
			}

		}
	}
	return -1;
}

