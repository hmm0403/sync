#ifndef _SESSION_HANDLER_MANAGER_
#define _SESSION_HANDLER_MANAGER_

#include "session_manager.h"
#include "session.h"
#include "crt_dbg.h"

#define MAX_SESSION_HANDLER 16

static int (*session_handler[MAX_SESSION_HANDLER]) (session_t *session) = 
{
	session_check_mode_handler,
	session_req_afl_handler,
	session_afl_handler,
	session_resume_handler,
	session_segment_list_handler,
	session_target_ready_handler,
	session_file_handler,
	session_file_ack_handler,
	session_file_end_handler,
	session_complete_handler,
	session_pause_handler,
	session_pause_immediate_handler,
	session_stop_handler,
	session_stop_with_mdp_handler,
	session_error_handler,
	session_fail_handler
};

typedef enum session_handler_type {
	INITSYNC_CMD_CHECK_MODE,
	INITSYNC_CMD_REQ_AFL,
	INITSYNC_CMD_AFL,
	INITSYNC_CMD_RESUME,
	INITSYNC_CMD_SEGMENT_LIST,
	INITSYNC_CMD_TARGET_READY,
	INITSYNC_CMD_FILE,
	INITSYNC_CMD_FILE_ACK,
	INITSYNC_CMD_FILE_END,
	INITSYNC_CMD_COMPLETE,
	INITSYNC_CMD_PAUSE,
	INITSYNC_CMD_PAUSE_IMMEDIATE,
	INITSYNC_CMD_STOP,
	INITSYNC_CMD_STOP_WITH_MDP,
	INITSYNC_CMD_ERROR,
	INITSYNC_CMD_FAIL
}SESSION_HANDLER_TYPE;

typedef struct session_cmd_hdr_t {
	SESSION_HANDLER_TYPE type;
	unsigned int	length;
}SESSION_CMD_HDR_T, *PSESSION_CMD_HDR_T;
#define SIZEOF_SESSION_CMD_HDR (sizeof(SESSION_CMD_HDR_T))

typedef enum session_file_hdr_type {
	SESSION_FILE_HDR_TYPE_HDR,
	SESSION_FILE_HDR_TYPE_BODY
}SESSION_FILE_HDR_TYPE;

typedef struct session_check_mode_hdr_t{
	unsigned int mode;
}SESSION_CHECK_MODE_HDR_T, *PSESSION_CHECK_MODE_HDR_T;
#define SIZEOF_SESSION_CHECK_MODE_HDR (sizeof(SESSION_CHECK_MODE_HDR_T))

typedef struct session_ctrl_hdr_t{
	uint64 seq;
	unsigned int len;
}SESSION_CTRL_FILE_HDR_T, *PSESSION_CTRL_FILE_HDR_T;
#define SIZEOF_SESSION_CTRL_FILE_HDR (sizeof(SESSION_CTRL_FILE_HDR_T))

typedef struct session_file_hdr_t{
	SESSION_FILE_HDR_TYPE type; // hdr or body
	unsigned int seq;
	uint64 ack_offset;
	uint64 ack_idx;
}SESSION_FILE_HDR_T, *PSESSION_FILE_HDR_T;
#define SIZEOF_SESSION_FILE_HDR (sizeof(SESSION_FILE_HDR_T))

typedef struct session_file_ack_hdr_t{
	uint64 ack_offset;
	uint64 ack_idx;
	uint64 file_size;
}SESSION_FILE_ACK_HDR_T, *PSESSION_FILE_ACK_HDR_T;
#define SIZEOF_SESSION_FILE_ACK_HDR (sizeof(SESSION_FILE_ACK_HDR_T))

typedef struct session_error_hdr_t{
	int state;
	int err;
	uint64 ack_offset;
	uint64 ack_idx;
	int length;
	int pad;
}SESSION_ERROR_HDR_T, *PSESSION_ERROR_HDR_T;
#define SIZEOF_SESSION_ERROR_HDR (sizeof(SESSION_ERROR_HDR_T))

#endif