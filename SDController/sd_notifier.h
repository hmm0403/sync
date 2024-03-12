#ifndef __SDCONTROLLER_SD_NOTIFIER_H_
#define __SDCONTROLLER_SD_NOTIFIER_H_

#include "crt_dbg.h"
#include "extern_c.h"
#include "type_defines.h"
#include "list.h"
#include "node.h"
#include "queue.h"

#define SD_NOTIFY_STATE_WAIT		(11)
#define SD_NOTIFY_STATE_START		(12)
#define SD_NOTIFY_STATE_SENDING		(13)
#define SD_NOTIFY_STATE_COMPLETE	(14)
#define SD_NOTIFY_STATE_FINISH		(15)
#define SD_NOTIFY_STATE_STOP		(16)
#define SD_NOTIFY_STATE_PAUSE		(18)
#define SD_NOTIFY_STATE_FAIL		(19)
#define SD_NOTIFY_STATE_FAILED_FILE	(20)

#define SEC_INITSYNC_KMEM_KEY_BASE  9900

#define GET_SD_PIPE_NAME(a, b) sprintf(a, "\\\\.\\pipe\\INITSYNC_PIPE_%d", b)

#define NOTIFY_WAIT(a) notify(a, SD_NOTIFY_STATE_WAIT, 0, 0, 0)
#define NOTIFY_START(a,b) notify(a, SD_NOTIFY_STATE_START, b, 0, 0)
#define NOTIFY_PROGRESS(a, b, c, d, e) notify(a, SD_NOTIFY_STATE_SENDING, b, c, d, e)
#define NOTIFY_COMPLETE(a, b) notify(a, SD_NOTIFY_STATE_COMPLETE, b, 0, 0)
#define NOTIFY_FINISH(a) notify(a, SD_NOTIFY_STATE_FINISH, 0, 0, 0)
#define NOTIFY_PAUSE(a, b, c, d) notify(a, SD_NOTIFY_STATE_PAUSE, b, c, d)
#define NOTIFY_STOP(a, b) notify(a, SD_NOTIFY_STATE_STOP, b, 0, 0)
#define NOTIFY_FAIL(a, b) notify(a, SD_NOTIFY_STATE_FAIL, b, 0, 0)
#define NOTIFY_FAILED_FILE(a, b, c, d) notify_fail(a, b, c, d)

typedef enum {
	SD_MSG_TYPE_CLOSE = -1,
	SD_MSG_TYPE_STATUS,
	SD_MSG_TYPE_PROGRESS,
	SD_MSG_TYPE_FAIL
} sd_msg_type_t;


typedef struct sd_notify_msg_t{
	int group_index;
	int initsync_id;
	int state;
	int target_index;
	int err;
	int pad;
	uint64 total_size;
	uint64 sent_size;
}sd_notify_msg_t;

typedef struct km_message_tag {
	long	type;
	//sd_notify_msg_t msg;
	char msg[sizeof(sd_notify_msg_t)];
} KM_MESSAGE;

typedef struct sd_notifier_t{
	/* send thread */
	HANDLE sd_notify_snder;
	unsigned int sd_notify_snder_id;

	HANDLE destroy_snder_event;
	/* wait queue */
	queue_t *msg_Q;

	uint64 noti_sent_size;

	/* pipe */
	HANDLE hPipe;		/* write pipe */

	char pipename[32];

	void *node;
} sd_notifier_t;

sd_notifier_t *init_sd_notifier(node_t *node);
void destroy_sd_notifier(sd_notifier_t *sn);
//int notify(sd_notifier_t *sn, int type, int target_index);
int notify(sd_notifier_t *sn, int status, int t_idx, uint64 total_size, uint64 sent_size);
int notify_fail(sd_notifier_t *sn, int t_idx, int err, TCHAR *path);

#endif