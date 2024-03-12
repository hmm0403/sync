#include <winsock2.h>
#include "NetworkEventHandler.h"
#include "defines.h"

#define MAX_TIMER_EVENT (10000)


static void null_time_event_handler(void *dis, void  *handle, void *data, uint32 to) {}

static int time_event_get_free_event_map_idx(void *arg)
{
	int idx;

	dispatcher_t *dis = (dispatcher_t *)arg;

	for (idx = 0; idx < dis->max_time_event; ++idx) {
		if (dis->time_event_map[idx].type == TIME_EVENT_TYPE_NOTUSE)
			break;
	}

	if (idx == dis->max_time_event)
		return -1;

	return idx;
}

time_event_t *time_event_init(dispatcher_t *dis)
{
	int idx;

	if (!dis)
		return NULL;

	dis->max_time_event = MAX_TIMER_EVENT;
	dis->max_time_event_idx = -1;
	dis->time_event_map = malloc(sizeof(time_event_t) * dis->max_time_event);
	if (!dis->time_event_map)
		return NULL;

	for (idx = 0; idx < dis->max_time_event; ++idx) {
		dis->time_event_map[idx].index = idx;
		dis->time_event_map[idx].count = 0;
		dis->time_event_map[idx].timeout = -1;
		dis->time_event_map[idx].type = TIME_EVENT_TYPE_NOTUSE;
		dis->time_event_map[idx].data = NULL;
		dis->time_event_map[idx].time_event_handler = null_time_event_handler;
	}

	return dis->time_event_map;
}

void time_event_destroy(dispatcher_t *dis)
{
	if (dis->time_event_map) {
		SAFE_FREE(dis->time_event_map);
		dis->time_event_map = NULL;
	}
}

/* 
 * time_event_set_timeout
 *
 * retval: timer handler (Users DO NOT modify this object.)
 * param:
 * 	dis: dispatcher
 * 	to: timeout value
 * 	handler: callback handler
 */
time_event_t *time_event_set_timeout(dispatcher_t *dis, uint32 timeout, void *data, void (*handler)(void *dis, void *handle, void *data, uint32 to))
{
	int idx;

	idx = time_event_get_free_event_map_idx(dis);
	if (idx < 0) {
		return NULL;
	}

	if (idx > dis->max_time_event_idx)
		dis->max_time_event_idx = idx;

	dis->time_event_map[idx].time_event_handler = handler;	
	dis->time_event_map[idx].timeout = (int64)timeout;
	dis->time_event_map[idx].count = (int64)timeout;
	dis->time_event_map[idx].type = TIME_EVENT_TYPE_TIMEOUT;
	dis->time_event_map[idx].data = data;
	return &dis->time_event_map[idx];
}

time_event_t *time_event_set_timer(dispatcher_t *dis, uint32 timeout, void *data, void (*handler)(void *dis, void *handle, void *data, uint32 to))
{
	int idx;

	idx = time_event_get_free_event_map_idx(dis);
	if (idx < 0) {
		return NULL;
	}

	if (idx > dis->max_time_event_idx)
		dis->max_time_event_idx = idx;

	dis->time_event_map[idx].time_event_handler = handler;	
	dis->time_event_map[idx].timeout = (int64)timeout;
	dis->time_event_map[idx].count = (int64)timeout;
	dis->time_event_map[idx].type = TIME_EVENT_TYPE_TIMER;
	dis->time_event_map[idx].data = data;
	return &dis->time_event_map[idx];
}

int time_event_del_timer(void *dispatcher, time_event_t *te)
{
	int idx;
	dispatcher_t *dis;

	dis = dispatcher;

	idx = te->index;
	if (idx < 0)
		return -1;

	if (idx == dis->max_time_event_idx)
		dis->max_time_event_idx--;
	
	dis->time_event_map[idx].time_event_handler = null_time_event_handler;
	dis->time_event_map[idx].timeout = -1;
	dis->time_event_map[idx].count = 0;
	dis->time_event_map[idx].type = TIME_EVENT_TYPE_NOTUSE;
	dis->time_event_map[idx].data = NULL;

	return 0;
}

int time_event_handler_invoker(void *dispatcher, uint64 tick)
{
	int idx;
	time_event_t *te;
	dispatcher_t *dis;

	dis = dispatcher;
	te = dis->time_event_map;

	for (idx = 0; idx <= dis->max_time_event_idx; ++idx) {

		te = &dis->time_event_map[idx];

		if (te->type == TIME_EVENT_TYPE_TIMEOUT || te->type == TIME_EVENT_TYPE_TIMER)
			te->count -= tick;
		else
			continue;

		/* time value is expired */
		if (te->count <= 0) {
			/* invoke time event handler */
			te->time_event_handler(dis, te, te->data, (uint32)te->timeout);

			if (te->type == TIME_EVENT_TYPE_TIMEOUT) {
				/* oneshot */
				time_event_del_timer(dis, te);
			} else {
				/* periodic */
				te->count = te->timeout;
			}
		}
	}

	return 0;

}

