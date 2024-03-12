#ifndef __MDP_CONTROLLER_H_
#define __MDP_CONTROLLER_H_

#include "crt_dbg.h"
#include "extern_c.h"
#include "type_defines.h"
#include "list.h"
//#include "node.h"
#include "hashtable.h"

#define MAX_CTRL_MSG 16
#define MDP_CTRL_TIME_OUT 5000

#define INSIDE_INTEREST_DIR 0
#define OUTSIDE_INTEREST_DIR 1

typedef enum mdp_ctrl_type_t{
	MDP_CTRL_MSG_REGIST_INITSYNC_PID,
	MDP_CTRL_MSG_REGIST_INITSYNC_PID_ACK,
	MDP_CTRL_MSG_WAIT,
	MDP_CTRL_MSG_WAIT_ACK,
	MDP_CTRL_MSG_READY,
	MDP_CTRL_MSG_READY_ACK,
	MDP_CTRL_MSG_REGIST_SEGMENT,
	MDP_CTRL_MSG_REGIST_SEGMENT_ACK,
	MDP_CTRL_MSG_UNREGIST_SEGMENT,
	MDP_CTRL_MSG_UNREGIST_SEGMENT_ACK,
	MDP_CTRL_MSG_REGIST_CUR_SEGMENT,
	MDP_CTRL_MSG_REGIST_CUR_SEGMENT_ACK,
	MDP_CTRL_MSG_GET_CUR_SEGMENT,
	MDP_CTRL_MSG_GET_CUR_SEGMENT_ACK,
	MDP_CTRL_MSG_PAUSE,
	MDP_CTRL_MSG_PAUSE_ACK,
	MDP_CTRL_MSG_STOP,
	MDP_CTRL_MSG_STOP_ACK,
	MDP_CTRL_MSG_RESUME,
	MDP_CTRL_MSG_RESUME_ACK,
	MDP_CTRL_MSG_COMPLETE,
	MDP_CTRL_MSG_COMPLETE_ACK,
	MDP_CTRL_MSG_RENAME_DIR,
	MDP_CTRL_MSG_RENAME_FILE,
	MDP_CTRL_MSG_DELETE_DIR,
	MDP_CTRL_MSG_DELETE_FILE,
	MDP_CTRL_MSG_REQ_CURRENT_CN,
	MDP_CTRL_MSG_CURRENT_CHANNEL,
	MDP_CTRL_MSG_CHANNEL_FAIL,
	MDP_CTRL_MSG_REMOTE_FAIL,
	MDP_CTRL_MSG_NETWORK_FAIL
}mdp_ctrl_type_t;

#define NEED_ACK_MSG(type) \
	((type < MDP_CTRL_MSG_RENAME_DIR) && type%2 == 0)?1:0

#define GET_INITSYNC_RPIPE_NAME(a, b) sprintf(a, "\\\\.\\pipe\\Initsync_%d_M", b)
#define GET_INITSYNC_WPIPE_NAME(a, b) sprintf(a, "\\\\.\\pipe\\Initsync_%d_I", b)

typedef struct mdp_ctrl_msg_hdr_t{
	mdp_ctrl_type_t type;
	int host_index;
	int seq;
	int length;
}mdp_ctrl_msg_hdr_t;

typedef struct mdp_ctrl_queue_t{
	list_node_t lnode;
	mdp_ctrl_type_t type;
	int seq;
	int rtv;
	HANDLE mdp_event;
}mdp_ctrl_queue_t;

typedef struct mdp_controller_t{
	/* recv thread */
	HANDLE mdp_ctrl_recver;
	unsigned int mdp_ctrl_recver_id;

	/* sync */
	CRITICAL_SECTION pipe_cs;
	CRITICAL_SECTION queue_cs;
	CRITICAL_SECTION rename_event_cs;

	/* wait queue */
	list_node_t queue_list;
	list_node_t init_list;

	int nr_queue;
	int nr_init;
	int nr_total;

	/* pipe */
	HANDLE hPipeW;		/* write pipe */
	HANDLE hPipeR;		/* read pipe */

	char pipename[16];
	char pipenameR[16];

	OVERLAPPED read_event;
	OVERLAPPED send_event;
	int destroy_flag;
	HANDLE destroy_recver_event;

	/* rename history file */
	HANDLE hHistory;

	char history[256];

	int seq;

	/* rename hash */
	hashtable_t *old_path_tbl;
	hashtable_t *new_path_tbl;

	/* rename list */
	list_node_t rename_event_list;
	int nr_rename_event;

	/* dir info */
	int nr_dir;
	list_node_t dir_list;

	void *node;
} mdp_controller_t;

EXTERN_C_START
mdp_controller_t *init_mdp_controller(void *node);
int mdp_ctrl(mdp_controller_t *mc, mdp_ctrl_type_t type, int host_index, int len, char *data);
#if _ALL_RENAME_HISTORY_
int get_new_path(mdp_controller_t *mc, TCHAR *old_path, TCHAR *new_path);
#else
int get_new_path(mdp_controller_t *mc, int is_dir, TCHAR *old_path, TCHAR **new_path);
#endif
int add_dir(mdp_controller_t *mc, TCHAR *dir, int length, int depth);
void destroy_mdp_controller(mdp_controller_t *mc);

int mdp_ctrl_set_segment(mdp_controller_t *mc, int host_index, TCHAR *fkey, TCHAR *lkey, int seg_id, int exception);
int mdp_ctrl_set_cur_segment(mdp_controller_t *mc, int host_index, int seg_id);
int mdp_ctrl_regist_initsync_pid(mdp_controller_t *mc);
int mdp_ctrl_pause(mdp_controller_t *mc, int host_index, int seg_id, TCHAR *last_path);
int mdp_ctrl_stop(mdp_controller_t *mc, int host_index, int seg_id, TCHAR *last_path, int stop_flag);
int mdp_ctrl_resume(mdp_controller_t *mc, int host_index);
int mdp_ctrl_get_cur_segment(mdp_controller_t *mc, int host_index);
EXTERN_C_END

#endif