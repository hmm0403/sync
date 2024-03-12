#ifndef __INITSYNC_NODE_H_
#define __INITSYNC_NODE_H_

#include "crt_dbg.h"
#include "extern_c.h"
#include "type_defines.h"
#include "winsock.h"
#include "sizeof.h"

typedef enum {
	NODE_STATUS_WAIT,
	NODE_STATUS_RUN,
	NODE_STATUS_FAILOVER,
	NODE_STATUS_PAUSE
} node_status_t;

typedef enum {
	CH_INIT = POW_2_0,			// Init
	CH_READY = POW_2_1,			// HB Sets Cur CH
	CH_NODE_CONNECTED = POW_2_2,	// Node set Re-Connected
	CH_SESSION_CONNECTED = POW_2_3, // Session set Re-Connected
	CH_NODE_FAIL = POW_2_4		// Network or Remote Node Fail
}channel_status_t;

typedef struct channel_t{
	int status;
	int cur_ch;
	SOCKET conn;

	HANDLE ch_event;
	HANDLE conn_event;

	HANDLE ch_lock;
}channel_t;

typedef struct node_t{
	/* basic node configuration */
	char *group_name;
	int group_index;
	int initsync_id;
	int mode;
	int local_rep;
	int index;
	int port;

	char *name;
	int status;

	int nr_nics;
	channel_t *ch_info;
	char **nic;
	SOCKET *socks;

	/* io dispatcher */
	void *dis;

	/* session manager */
	int nr_remote;
	int nr_session;
	void *session_manager;

	/* file namager */
	int nr_dir;
	void *file_manager;

	/* mDP controller */
	void *mdp_controller;

	/* HB */
	void *hb;

	/* SD Notifier*/
	void *sd_notifier;

	/* SD cmd controller */
	void *sd_cmd_cntl;

	/* stop flag */
	int complete;

	/* config file */
	char *conf_path;
	void *ini_conf;

	int resume;
} node_t;

EXTERN_C_START
node_t *init_node();
int init_component(void);
int init_service(node_t *node);
void destroy_node(node_t *node);
int pause_node_handler(node_t *node, int flag);
int stop_node_handler(node_t *node, int flag);
int resume_node_handler(node_t *node);
int node_failover_process(int nodeFailFlag, int channel_id);

int get_current_channel(int block);
int get_failover_channel(DWORD timeout);
SOCKET get_failover_connect(DWORD timeout);
int set_failover_conn(SOCKET conn);
void set_session_connected(SOCKET sock);
void set_failover_complete();

EXTERN_C_END

#endif