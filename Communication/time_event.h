#ifndef __COMMUNICATION_TIME_EVENT_H_
#define __COMMUNICATION_TIME_EVENT_H_

#include "crt_dbg.h"
#include "extern_c.h"
#include "type_defines.h"

typedef enum {
	TIME_EVENT_TYPE_NOTUSE,
	TIME_EVENT_TYPE_TIMEOUT,
	TIME_EVENT_TYPE_TIMER
} time_event_type_t;

typedef struct time_event_t {
	int index;

	/* timer/timeout에 설정 가능한 timeout 값은 uint32이다.
	 * time 값을 이용한 계산에서 boundary overflow를 방지하기 위해 int64로 다룬다.
	 */
	int64 timeout;
	int64 count;

	time_event_type_t type;

	void *data;
	void (*time_event_handler)(void *dis, void *handle, void *data, uint32 timeout);
} time_event_t;

EXTERN_C_START
time_event_t *time_event_init(void *dis);
void time_event_destroy(void *dis);
int time_event_handler_invoker(void *dis, uint64 tick);
time_event_t *time_event_set_timer(void *dis, uint32 timeout, void *data, void (*handler)(void *dis, void *handle, void *data, uint32 to));
time_event_t *time_event_set_timeout(void *dis, uint32 timeout, void *data, void (*handler)(void *dis, void *handle, void *data, uint32 to));
int time_event_del_timer(void *dis, time_event_t *te);
EXTERN_C_END


#endif
