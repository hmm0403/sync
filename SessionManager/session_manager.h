#ifndef __SESSION_SESSION_MANAGER_H_
#define __SESSION_SESSION_MANAGER_H_

#ifdef WIN32
#include "crt_dbg.h"
#ifdef _MT
#include <windows.h>
#include <process.h>
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#endif

#include "extern_c.h"
#include "list.h"
#include "type_defines.h"
#include "defines.h"
#include "queue.h"
#include "buf_pool.h"

typedef enum {
	SESSION_STATE_INIT,
	SESSION_STATE_RUN,
	SESSION_STATE_TERM
} session_state_t;

typedef enum {
	SESSION_RUN_STATE_INIT,
	SESSION_RUN_STATE_MODE_CHECK,
	SESSION_RUN_STATE_RESUME,
	SESSION_RUN_STATE_AFL,
	SESSION_RUN_STATE_CMP,
	SESSION_RUN_STATE_TOL,
	SESSION_RUN_STATE_SOL,
	SESSION_RUN_STATE_MFL,
	SESSION_RUN_STATE_READY,
	SESSION_RUN_STATE_FILE,
	SESSION_RUN_STATE_PAUSE,
	SESSION_RUN_STATE_STOP,
	SESSION_RUN_STATE_COMPLETE,
	SESSION_RUN_STATE_FAIL
} session_run_state_t;

typedef struct ip_t
{
	list_node_t lnode;
	char *ip;
}ip_t;

typedef struct not_acked_file_t
{
	list_node_t lnode;
	char *filename;
	int len;
}not_acked_file_t;

typedef struct ack_list_t
{
	CRITICAL_SECTION cs;

	list_node_t ack_list;
	list_node_t free_list;

	int nr_ack;
	int nr_free;
	int nr_total;
}ack_list_t;

typedef struct remote_host_table_entry_t{
	list_node_t lnode;
	char *name;
	int id;
	int nr_ips;
	list_node_t ip_list;
}remote_host_table_entry_t;

typedef struct session_manager_t {
	/* the number of session */
	int nr_total;	
	int nr_complete;
	int nr_fail;

	/* remote host table */
	int nr_remote_table;
	list_node_t remote_table_list;

	/* session list */
	list_node_t session_list;

#ifdef WIN32
	HANDLE sm_mutex;
#else
	pthread_mutex_t sm_mutex;
#endif

	/* local rep 의 source/target session sync를 위해 사용 */
	HANDLE local_rep_sync_event;

	/* node */
	void *node;

} session_manager_t;


typedef struct sesssion_t {
	list_node_t lnode;	/* session list's entry */

#ifdef WIN32
	HANDLE session_thread;
	unsigned int session_thread_id;

	HANDLE recv_thread;
	unsigned int recv_thread_id;

	HANDLE send_thread;
	unsigned int send_thread_id;

	/* for send modified files */
	HANDLE send_file_thread;
	unsigned int send_file_thread_id;

	HANDLE destroy_recv_th_event;
	HANDLE destroy_send_th_event;
	HANDLE destroy_send_file_th_event;
#else
	pthread_t session_thread;
#endif

	char *name;
	int remote_id;
	char *remote_ip;
	SOCKET sock;

	SOCKADDR_IN remoteaddr;
#ifdef WIN32
	int addrlen;
#else
	socklen_t addrlen;
#endif

	/* session state: session이 속한 list와 일치 */
	session_state_t state;

	/* session 이 initsync 하는 도중의 작업 과정 */
	session_run_state_t run_state;

	/* session별 initsync 대상 dir */
	void *flist;

	queue_t *recv_Q;
	queue_t *send_Q;

	/* used for local replication */
	queue_t *connect_Q; 

	/* recv packet for processing */
	char *data;
	int data_len;

	/* buffer pool */
	buf_pool_t *buf_pool;

	/* current recv file info */
	TCHAR *cur_recv_path;
	TCHAR *cur_lr_recv_path;
	int cur_recv_path_len;
	HANDLE hCurFile;
	DWORD dwCurAttribute;
	char *cur_security;
	int cur_secu_len;
	int cur_secu_info;

	/* cur file info for ack */
	uint64 ack_offset;
	uint64 ack_idx;
	uint64 cur_file_size;

	/* resume mode */
	int resume;

	/* not acked file list */
	ack_list_t *ack_list;

	uint64 total_duration;
	uint64 send_duration;
	uint64 total_bytes;
	uint64 sent_bytes;

	int total_files;

	int nr_try;

	int local_rep_mode;

	/* failover */
	int failover;
	HANDLE recv_failover_event;
	HANDLE send_failover_event;

	/* session_manager_t */
	session_manager_t *sm;
} session_t;

EXTERN_C_START
session_manager_t *init_session_manager(void *node);
int destroy_session_manager(session_manager_t *sm);
session_t *session_alloc(session_manager_t *sm);
int session_dealloc(session_t *s);
int add_remote_host_table(session_manager_t *sm, char *name, char *ip, int id);
int delete_remote_host_table(session_manager_t *sm, char *ip);
remote_host_table_entry_t *session_accept_check(session_manager_t *sm, char *ip);
remote_host_table_entry_t *find_remote_host_table_by_name(session_manager_t *sm, char *name);
remote_host_table_entry_t *find_remote_host_table_by_ip(session_manager_t *sm, char *ip);
EXTERN_C_END;

#endif