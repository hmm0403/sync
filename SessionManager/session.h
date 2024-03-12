#ifndef __SESSION_SESSION_H_
#define __SESSION_SESSION_H_

#include "crt_dbg.h"
#include "extern_c.h"
#include "session_manager.h"
#ifdef _MT
#include <process.h>
#endif

#define SESSION_FAILOVER (1)
#define SESSION_CRASH (2)

#define SESSION_RECV(session, buf, bufSize, block, ret) \
	{ \
		int recvlen = 0; \
		while(recvlen < bufSize){ \
			ret = session_recv(session, (char *)(buf) + recvlen, bufSize - recvlen, block); \
			if(ret > 0){ \
				recvlen += ret; \
			}else{ \
				ret = -1; \
				break; \
			} \
		} \
	}

typedef struct SEND_FILE_HDR_T
{
	DWORD	File_attribute;
	DWORD	modified_type;
	int		path_size;
	int		secu_len;
	uint64	filesize;
	SECURITY_INFORMATION secu_info;
}SEND_FILE_HDR_T, *PSEND_FILE_HDR_T;
#define SIZEOF_SEND_FILE_HDR sizeof(SEND_FILE_HDR_T)

typedef struct SEND_FILE_DATA_HDR_T
{
	int length;
}SEND_FILE_DATA_HDR_T, *PSEND_FILE_DATA_HDR_T;
#define SIZEOF_SEND_FILE_DATA_HDR sizeof(SEND_FILE_DATA_HDR_T)

EXTERN_C_START
unsigned int WINAPI session_control_thread(LPVOID arg);
unsigned int WINAPI session_recv_thread(LPVOID arg);
unsigned int WINAPI session_send_thread(LPVOID arg);
void session_accept_handler(void *dis, SOCKET sock, void *data, int err);
void session_connect_error_handler(void *dis, SOCKET sock, void *data, int err);
void session_not_acceptable_handler(void *dis, SOCKET sock, void *data, int err);
int session_connect(session_t *s);
int session_send(session_t *s, char *msg, int len);//, entry_data_type_t type);
char *session_recv(session_t *s, int *len, int blocking);
void session_local_initsync(void *node);
int close_local_connect(session_t *s);

/* session handler functions */
int session_check_mode_handler(session_t *s);
int	session_req_afl_handler(session_t *s);
int	session_afl_handler(session_t *s);
int	session_resume_handler(session_t *s);
int	session_segment_list_handler(session_t *s);
int	session_target_ready_handler(session_t *s);
int	session_file_handler(session_t *s);
int	session_file_ack_handler(session_t *s);
int	session_file_end_handler(session_t *s);
int	session_complete_handler(session_t *s);
int	session_pause_handler(session_t *s);
int	session_pause_immediate_handler(session_t *s);
int	session_stop_handler(session_t *s);
int	session_stop_with_mdp_handler(session_t *s);
int session_fail_handler(session_t *s);
int	session_error_handler(session_t *s);

EXTERN_C_END

#endif