#include <winsock2.h>
#include <stdio.h>
#include "node.h"
#include "ini_reader.h"
#include "defines.h"
#include "session_manager.h"
#include "session.h"
#include "file_manager.h"
#include "NetworkEventHandler.h"
#include "network_api.h"
#include "debug_console.h"
#include "report.h"
#include "privilege.c"
#include <signal.h>
#include "mdp_controller.h"
#include "sd_notifier.h"
#include "sd_cmd_cntl.h"
#include "wchar_conv.h"
#include "hb.h"
#include "sizeof.h"

extern int g_stopFlag;
extern int g_nodeChannelInfo;
//extern int g_pauseFlag;
extern int g_logLevel;
extern int g_logMod;
extern int g_target_delete;
extern int g_alone;
extern int g_resume;
extern int g_group_id;
extern int g_initsync_id;
extern char *g_conf_path;

extern int g_file_buf_size;
extern int g_send_buf_size;
extern int g_recv_buf_size;
extern int g_sock_buf_size;

extern node_t *g_node;
extern int g_exit;

static int read_ini_config(node_t *node, int *err)
{
	char *str;
	char tmp[32], tmp2[32];
	int i, j;

	*err = ERROR_INITSYNC_SUCCESS;

	LOG_INFO(LOG_MOD_MAIN, "Read ini conf file[%s]", node->conf_path);
	if(node->conf_path){
		node->ini_conf = ini_read(node->conf_path, err);
	}else{
		node->ini_conf = ini_read(INITSYNC_CONFIG_PATH, err);
	}
	if (!node->ini_conf) {
		return -1;
	}

	str = ini_get_data_ex(node->ini_conf, "BASIC_INFO", "GROUP_NAME");
	if (str) {
		node->group_name = malloc(strlen(str) + 1);
		if (!node->group_name) {
			*err = ERROR_SYS_ALLOC;
			return -1;
		}
		strcpy(node->group_name, str);
	} else {
		*err = ERROR_INI_NO_LINE;
		node->group_name = NULL;
	}

	node->mode = atoi(ini_get_data_ex(node->ini_conf, "BASIC_INFO", "MDP_MODE"));

	node->index = atoi(ini_get_data_ex(node->ini_conf, "BASIC_INFO", "NODE_INDEX"));

	node->port = atoi(ini_get_data_ex(node->ini_conf, "BASIC_INFO", "PORT"));

	if(node->mode == INITSYNC_MODE_SOURCE){

		/* Local(Source) node 설정 */
		memset(tmp, 0, 16);

		sprintf(tmp, "S_Node%d", node->index);

		str = ini_get_data_ex(node->ini_conf, tmp, "NAME");
		if (str) {
			node->name = malloc(strlen(str) + 1);
			if (!node->name) {
				*err = ERROR_SYS_ALLOC;
				return -1;
			}
			strcpy(node->name, str);
		} else {
			*err = ERROR_INI_NO_LINE;
			node->name = NULL;
		}

		node->local_rep = atoi(ini_get_data_ex(node->ini_conf, "BASIC_INFO", "LOCAL_REP_ON"));
		if(node->local_rep){
			TCHAR *local_dir;

			str = ini_get_data_ex(node->ini_conf, "BASIC_INFO", "LOCAL_REP_DIR");
#ifdef _UNICODE
			local_dir = CharToWChar(str);
#else
			local_dir = str;
#endif
			flist_regist_local_dir((file_manager_t*) node->file_manager, node->name, local_dir);
		}

		node->nr_nics = atoi(ini_get_data_ex(node->ini_conf, tmp, "NUMBER_OF_NICS"));

		if(node->nr_nics > 0){
			node->nic = malloc(node->nr_nics * sizeof(char **));
			if(!node->nic){
				*err = ERROR_SYS_ALLOC;
				return -1;
			}

			for(i = 0; i < node->nr_nics; i++)
			{
				memset(tmp2, 0, 16);

				sprintf(tmp2, "NIC%d", i);
				str = ini_get_data_ex(node->ini_conf, tmp, tmp2);
				if (str) {
					node->nic[i] = malloc(strlen(str) + 1);
					if (!node->nic[i]) {
						*err = ERROR_SYS_ALLOC;
						return -1;
					}
					strcpy(node->nic[i], str);
				} else {
					*err = ERROR_INI_NO_LINE;
					node->nic[i] = NULL;
				}
			}
		}

		/* Remote(Target) node 설정 */
		node->nr_remote = atoi(ini_get_data_ex(node->ini_conf, "TARGET_NODES", "NUMBER_OF_NODES"));
		if(node->nr_remote > 0){
			memset(tmp, 0,16);

			for(i = 0; i < node->nr_remote; i++){
				char *name, *ip;
				int nr_nic, sync;

				sprintf(tmp, "T_Node%d", i);

				name = ini_get_data_ex(node->ini_conf, tmp, "NAME");
				if (name) {
					nr_nic = atoi(ini_get_data_ex(node->ini_conf, tmp, "NUMBER_OF_NICS"));

					for(j = 0; j < nr_nic; j++){
						sprintf(tmp2, "NIC%d", j);
						ip = ini_get_data_ex(node->ini_conf, tmp, tmp2);
						if(ip)
						{
							sync = atoi(ini_get_data_ex(node->ini_conf, tmp, "SYNC"));
							if(sync)
								add_remote_host_table((session_manager_t*) node->session_manager, name, ip, i);
						}else{
							/* fail */
							*err = ERROR_INI_NO_LINE;
						}
					}
				}else{
					/* fail */
					*err = ERROR_INI_NO_LINE;
				}
			}
		}

		/* init directory */
		node->nr_dir = atoi(ini_get_data_ex(node->ini_conf, "DIRECTORY", "NUMBER_OF_DIRECTORIES"));
		if(node->nr_dir > 0){
			memset(tmp, 0,16);

			for(i = 0; i < node->nr_dir; i++){
				char *name, *src, *tgt;
				TCHAR *tname = NULL;
				int nr_tgt;

				sprintf(tmp, "Dir%d", i);
				name = ini_get_data_ex(node->ini_conf, tmp, "NAME");
				src = ini_get_data_ex(node->ini_conf, tmp, "SOURCE_NAME");
				if (name && src) {
					if(!strcmp(node->name, src)){
						nr_tgt = atoi(ini_get_data_ex(node->ini_conf, tmp, "NUMBER_OF_CORRESPONDENT_HOSTS"));
						for(j = 0; j < nr_tgt; j++){
							sprintf(tmp2, "CORRESPONDENT_HOST%d", j);
							tgt = ini_get_data_ex(node->ini_conf, tmp, tmp2);
							if(tgt)
							{
#ifdef _UNICODE
								tname = CharToWChar(name);
#else
								tname = name;
#endif
								LOG_DEBUG(LOG_MOD_FILE, "add flist src:%s, tgt:%s, dir:%s", src, tgt, name);
								add_file_list((file_manager_t*) node->file_manager, tname, src, tgt);
								SAFE_FREE(tname);
							}else{
								/* fail */
								*err = ERROR_INI_NO_LINE;
							}
						}
					}
				}else{
					/* fail */
					*err = ERROR_INI_NO_LINE;
				}
			}
		}
	}else if(node->mode == INITSYNC_MODE_TARGET){
		/* Local(Target) node 설정 */
		int sync;

		memset(tmp, 0, 16);

		sprintf(tmp, "T_Node%d", node->index);
	
		//sync = atoi(ini_get_data_ex(node->ini_conf, tmp, "SYNC"));
		str = ini_get_data_ex(node->ini_conf, tmp, "SYNC");
		if(!str){
			*err = ERROR_INI_NOT_RELEVANT;
			return -1;
		}

		sync = atoi(str);
		if(sync == 0){
			*err = ERROR_INI_NO_LINE;
			return -1;
		}

		str = ini_get_data_ex(node->ini_conf, tmp, "NAME");
		if (str) {
			node->name = malloc(strlen(str) + 1);
			if (!node->name) {
				*err = ERROR_SYS_ALLOC;
				return -1;
			}
			strcpy(node->name, str);
		} else {
			*err = ERROR_INI_NO_LINE;
			node->name = NULL;
		}

		node->nr_nics = atoi(ini_get_data_ex(node->ini_conf, tmp, "NUMBER_OF_NICS"));

		if(node->nr_nics > 0){
			node->nic = malloc(node->nr_nics * sizeof(char **));
			if(!node->nic){
				*err = ERROR_SYS_ALLOC;
				return -1;
			}

			for(i = 0; i < node->nr_nics; i++)
			{
				memset(tmp2, 0, 16);

				sprintf(tmp2, "NIC%d", i);
				str = ini_get_data_ex(node->ini_conf, tmp, tmp2);
				if (str) {
					node->nic[i] = malloc(strlen(str) + 1);
					if (!node->nic[i]) {
						*err = ERROR_SYS_ALLOC;
						return -1;
					}
					strcpy(node->nic[i], str);
				} else {
					*err = ERROR_INI_NO_LINE;
					node->nic[i] = NULL;
				}
			}
		}

		/* Remote(Source) nade 설정 */
		node->nr_remote = atoi(ini_get_data_ex(node->ini_conf, "SOURCE_NODES", "NUMBER_OF_NODES"));
		if(node->nr_remote > 0){
			memset(tmp, 0,16);

			for(i = 0; i < node->nr_remote; i++){
				char *name, *ip;
				int nr_nic;

				sprintf(tmp, "S_Node%d", i);

				name = ini_get_data_ex(node->ini_conf, tmp, "NAME");
				if (name) {
					nr_nic = atoi(ini_get_data_ex(node->ini_conf, tmp, "NUMBER_OF_NICS"));

					for(j = 0; j < nr_nic; j++){
						sprintf(tmp2, "NIC%d", j);
						ip = ini_get_data_ex(node->ini_conf, tmp, tmp2);
						if(ip)
						{
							add_remote_host_table((session_manager_t*) node->session_manager, name, ip, i);
						}else{
							/* fail */
							*err = ERROR_INI_NO_LINE;
						}
					}
				}else{
					/* fail */
					*err = ERROR_INI_NO_LINE;
				}
			}
		}

		/* init directory */
		node->nr_dir = atoi(ini_get_data_ex(node->ini_conf, "DIRECTORY", "NUMBER_OF_DIRECTORIES"));
		if(node->nr_dir > 0){
			memset(tmp, 0,16);

			for(i = 0; i < node->nr_dir; i++){
				char *name, *src, *tgt;
				TCHAR *tname = NULL;
				int nr_tgt;

				sprintf(tmp, "Dir%d", i);
				name = ini_get_data_ex(node->ini_conf, tmp, "NAME");
				src = ini_get_data_ex(node->ini_conf, tmp, "SOURCE_NAME");
				if (name && src) {
					nr_tgt = atoi(ini_get_data_ex(node->ini_conf, tmp, "NUMBER_OF_CORRESPONDENT_HOSTS"));
					for(j = 0; j < nr_tgt; j++){
						sprintf(tmp2, "CORRESPONDENT_HOST%d", j);
						tgt = ini_get_data_ex(node->ini_conf, tmp, tmp2);
						if(tgt)
						{
							if(!strcmp(node->name, tgt)){
#ifdef _UNICODE
								tname = CharToWChar(name);
#else
								tname = name;
#endif
								add_file_list((file_manager_t*) node->file_manager, tname, src, tgt);
								SAFE_FREE(tname);
							}
						}else{
							/* fail */
							*err = ERROR_INI_NO_LINE;
						}
					}
				}else{
					/* fail */
					*err = ERROR_INI_NO_LINE;
				}
			}
		}else{
			/* unknown mode */
			*err = ERROR_INI_WRONG_FORMAT;
		}
	}

	str = ini_get_data_ex(node->ini_conf, "LOG_INFO", "LEVEL");
	if (str) {
		g_logLevel = atoi(str);
	}else{
		g_logLevel = LOG_LEVEL_INFO;
	}

	str = ini_get_data_ex(node->ini_conf, "LOG_INFO", "MODULE");
	if (str) {
		g_logMod = SetLogModule(str);
	}

	str = ini_get_data_ex(node->ini_conf, "BUF_INFO", "FILE");
	if (str) {
		g_file_buf_size = atoi(str);
	}else{
		g_file_buf_size = FILE_BUF_SIZE;
	}

	str = ini_get_data_ex(node->ini_conf, "BUF_INFO", "SEND");
	if (str) {
		g_send_buf_size = atoi(str);
	}else{
		g_send_buf_size = TCP_BUF_SIZE;
	}

	str = ini_get_data_ex(node->ini_conf, "BUF_INFO", "RECV");
	if (str) {
		g_send_buf_size = atoi(str);
	}else{
		g_recv_buf_size = FILE_BUF_SIZE;
	}

	str = ini_get_data_ex(node->ini_conf, "BUF_INFO", "SOCK");
	if (str) {
		g_sock_buf_size = atoi(str);
	}else{
		g_sock_buf_size = SOCK_BUF_SIZE;
	}

	ini_destroy(node->ini_conf);

	return 0;
}


int connect_to_all_source(node_t *node)
{
	list_node_t *pos, *nxt;
	remote_host_table_entry_t *remote;
	session_manager_t *sm = node->session_manager;
	session_t *new_s;
	int ret;

	/* create session thread */
	list_for_each_safe(pos, nxt, &sm->remote_table_list)
	{
		remote = list_entry(pos, remote_host_table_entry_t, lnode);
		if(!remote)
		{
			LOG_ERROR(LOG_MOD_MAIN, "there are no remote host info");
			return -1;
		}
		new_s = session_alloc(sm);
		if(!new_s)
		{
			LOG_ERROR(LOG_MOD_MAIN, "session alloc fail");
			return -1;
		}
		new_s->name = malloc(strlen(remote->name)+1);
		if(!new_s->name)
		{
#ifdef _DEBUG
			LOG_ERROR(LOG_MOD_MAIN, "out of memory %s:%d", __FILE__, __LINE__);
#endif
			return -1;
		}
		ret = strncpy_s(new_s->name, strlen(remote->name)+1, remote->name, _TRUNCATE);
		if(ret != 0){
			LOG_ERROR(LOG_MOD_MAIN, "strcpy fail %s:%d", __FILE__, __LINE__);
			SAFE_FREE(new_s->name);
			return -1;
		}

		/* connect */
		ret = session_connect(new_s);
		if(ret == SOCKET_ERROR){
			LOG_ERROR(LOG_MOD_MAIN, "connect fail");
			return -1;
		}
	}

	return 0;
}


void destroy_node(node_t *node)
{
	LOG_INFO(LOG_MOD_MAIN, "+-   Destoroy Node    -+");

	if(node->hb)
		hb_fin(node->hb);

	if(node->session_manager)
		destroy_session_manager(node->session_manager);;

	if(node->file_manager)
		destroy_file_manager(node->file_manager);
	
	if(node->mdp_controller)
		destroy_mdp_controller(node->mdp_controller);

	if(node->sd_notifier)
		destroy_sd_notifier(node->sd_notifier);
	
	SAFE_FREE(node);

	LOG_INFO(LOG_MOD_MAIN, "+- Destoroy Node Done -+");
}

channel_t *init_ch_info()
{
	channel_t *ch;

	ch = malloc(sizeof(channel_t));

	memset(ch, 0, sizeof(channel_t));

	ch->ch_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	ch->conn_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	ch->ch_lock = CreateMutex(NULL, FALSE, NULL);

	ch->cur_ch = -1;
	ch->conn = SOCKET_ERROR;
	ch->status = CH_INIT;

	return ch;
}

node_t *init_node()
{
	int ret, err;

	node_t *node;

	node = malloc(sizeof(node_t));
	if(!node)
		return NULL;
	memset(node, 0, sizeof(node_t));

	node->resume = g_resume;
	node->conf_path = g_conf_path;
	node->group_index = g_group_id;
	node->initsync_id = g_initsync_id;

	if(g_alone)
		LOG_INFO(LOG_MOD_MAIN, "Initsync works alone");
	else
		LOG_INFO(LOG_MOD_MAIN, "Incremental Initsync works with mDP");

	/* init session manager */
	node->session_manager = init_session_manager(node);
	if(!node->session_manager){
		LOG_ERROR(LOG_MOD_MAIN, "init session manager fail");
		destroy_node(node);
		return NULL;
	}

	/* init file manager */
	node->file_manager = init_file_manager(node);
	if(!node->file_manager){
		LOG_ERROR(LOG_MOD_MAIN, "init file manager fail");
		destroy_node(node);
		return NULL;
	}

	/* read ini config file */
	ret = read_ini_config(node, &err);
	if(ret < 0){
		LOG_ERROR(LOG_MOD_MAIN, "init configure fail, error code : %d", err);

		destroy_node(node);
		return NULL;
	}
	LOG_INFO(LOG_MOD_MAIN, "initsync configuration complete");

	/* channel */
	node->ch_info = init_ch_info();

	/* SD Notifier */
#ifdef _WITHOUT_SD_
	node->sd_notifier = NULL;
#else
	node->sd_notifier = init_sd_notifier(node);
	if(!node->sd_notifier){
		LOG_ERROR(LOG_MOD_MAIN, "init sd notifier fail");
		destroy_node(node);
		return NULL;
	}
	LOG_INFO(LOG_MOD_MAIN, "init sd notifier success");
#endif

	/* SD cmd controller */
//#ifdef _WITHOUT_SD_
//	node->sd_cmd_cntl = NULL;
//#else
	node->sd_cmd_cntl = init_sd_cmd_cntl(node);
	if(!node->sd_cmd_cntl){
		LOG_ERROR(LOG_MOD_MAIN, "init sd controller fail");
		destroy_node(node);
		return NULL;
	}
	LOG_INFO(LOG_MOD_MAIN, "init sd cmd cntl success");
//#endif

	/* init nDPcontroller */
	if(g_alone){
		node->mdp_controller = NULL;

		/* init hb */
		if(!node->local_rep){
			remote_host_table_entry_t *remote;

			list_node_t *pos, *nxt;
			ip_t *ip_entry;
			int i = 0;
			char **ips;

			/* 1:1 가정인듯, hb 에 대해선 n:n 지원 안하는듯 */
			remote = list_first_entry(&((session_manager_t *)(node->session_manager))->remote_table_list, remote_host_table_entry_t, lnode);
			ips = (char **)malloc(sizeof(char*) * remote->nr_ips);

			list_for_each_safe(pos, nxt, &remote->ip_list){
				ip_entry = list_entry(pos, ip_t, lnode);

				ips[i] = (char *)malloc(strlen(ip_entry->ip) + 1);

				strcpy(ips[i], ip_entry->ip);
				i++;
			}
				
			node->hb = hb_init(node->group_index, node->nr_nics, node->nic, ips, node->port+HB_PORT_OFFSET, 500, 5000);
			
			for(i = 0; i < remote->nr_ips; i++){
				free(ips[i]);
			}
			free(ips);
		}
	}else{
		node->hb = NULL;

		node->mdp_controller = init_mdp_controller(node);
		if(!node->mdp_controller){
			LOG_ERROR(LOG_MOD_MAIN, "init mdp controller fail");
			destroy_node(node);
			return NULL;
		}
		LOG_INFO(LOG_MOD_MAIN, "init mdp controller success");

		if(node->mode == INITSYNC_MODE_SOURCE || node->mode == INITSYNC_MODE_LOCAL_REPLICATION){
			ret = mdp_ctrl_regist_initsync_pid(node->mdp_controller);
			if(ret < 0){
				LOG_ERROR(LOG_MOD_MAIN, "regist initsync pid to mDP Fail");
				destroy_node(node);
				return NULL;
			}
		}
	}

	/* initialize network & timer event handler */
	node->dis = init_dispatcher(0);
	if (!node->dis) {
		LOG_ERROR(LOG_MOD_MAIN, "init dispatcher fail");
		destroy_node(node);
		return NULL;
	}
	LOG_INFO(LOG_MOD_MAIN, "init dispatcher success");

	node->status = NODE_STATUS_WAIT;

	return node;
}

int init_component()
{
	int ret;

	//init_debug_console();
	ret = RPT_Initialize();
	if(ret < 0)
		return -1;

	//권한 설정 -> 보안설정을 위한 privilege획득
	if (!AddPriviliges())
	{
		LOG_ERROR(LOG_MOD_MAIN, "AddPriviliges fail");
		return -1;
	}

	ret = init_network_api();
	if (ret < 0){
		LOG_ERROR(LOG_MOD_MAIN, "Init_network_api fail");
		return -1;
	}

	return 0;
}

void destroy_service_handler(void *dis, void *handle, void *data, uint32 timeout)
{
	node_t *node;
	session_manager_t *sm;
	dispatcher_t *dispatcher = dis;

	node = data;
	
	if(node->complete){
		dispatcher->freq = 0;
	}

	sm = node->session_manager;
	if(sm){
		if(node->nr_remote == sm->nr_complete + sm->nr_fail){// || \
			//(node->mode == INITSYNC_MODE_TARGET && sm->nr_complete + sm->nr_fail == node->nr_remote)){
			//NOTIFY_FINISH(node->sd_notifier);
			LOG_DEBUG(LOG_MOD_MAIN, "Dispatcher Exit. All Session Complete or fail");
			dispatcher->exit = 1;
			g_exit = 1;
		}

		if((g_stopFlag & SD_CMD_STOP) && sm->nr_total == 0){
			//NOTIFY_FINISH(node->sd_notifier);
			LOG_DEBUG(LOG_MOD_MAIN, "Dispatcher Exit. Get Stop CMD");
			dispatcher->exit = 1;
			g_exit = 1;
		}

		if((g_stopFlag & SD_CMD_STOP) && (node->status == NODE_STATUS_WAIT)){
			//NOTIFY_FINISH(node->sd_notifier);
			LOG_DEBUG(LOG_MOD_MAIN, "Dispatcher Exit. Get Stop CMD While Wait State");
			dispatcher->exit = 1;
			g_exit = 1;
		}
	}
}

void log_level_handler(void *dis, void *handle, void *data, uint32 timeout)
{
	node_t *node;
	char *str;
	char tmp[32], tmp2[32];
	int i, j, err;

	node = data;

	if(node->conf_path){
		node->ini_conf = ini_read(node->conf_path, &err);
	}else{
		node->ini_conf = ini_read(INITSYNC_CONFIG_PATH, &err);
	}
	if (!node->ini_conf) {
		return;
	}

	str = ini_get_data_ex(node->ini_conf, "LOG_INFO", "LEVEL");
	if (str) {
		g_logLevel = atoi(str);
	}else{
		g_logLevel = LOG_LEVEL_INFO;
	}

	str = ini_get_data_ex(node->ini_conf, "LOG_INFO", "MODULE");
	if (str) {
		g_logMod = SetLogModule(str);
	}

	ini_destroy(node->ini_conf);
}

int get_current_channel(int block)
{
	int ret;

	channel_t *ch = g_node->ch_info;

	WaitForSingleObject(ch->ch_lock, INFINITE);

	if(ch->status == CH_INIT){
		if(block == BLOCKING_CALL){
			ReleaseMutex(ch->ch_lock);

			ret = get_failover_channel(INFINITE);

			WaitForSingleObject(ch->ch_lock, INFINITE);
		}else{
			ret = -1;
		}
	}else if(ch->status == CH_NODE_FAIL){
		ret = -1;
	}else{
		ret = ch->cur_ch;
	}

	ReleaseMutex(ch->ch_lock);

	return ret;
}

int get_failover_channel(DWORD timeout)
{
	int ret;

	channel_t *ch = g_node->ch_info;

	WaitForSingleObject(ch->ch_lock, INFINITE);

	LOG_DEBUG(LOG_MOD_MAIN, "get Channel Info try, %d:%d", ch->status, ch->conn);
	
	if((ch->status & CH_READY) || (ch->status & CH_NODE_CONNECTED) || ch->status == CH_NODE_FAIL){
		ResetEvent(ch->ch_event);
	}else if(timeout){
		ReleaseMutex(ch->ch_lock);
		
		LOG_DEBUG(LOG_MOD_MAIN, "Wait Channel Info");
		ret = WaitForSingleObject(ch->ch_event, timeout);
		if(ret == WAIT_TIMEOUT){
			LOG_DEBUG(LOG_MOD_MAIN, "get Channel Info Fail");
			return SOCKET_ERROR;
		}

		WaitForSingleObject(ch->ch_lock, INFINITE);
	}

	if(ch->status == CH_NODE_FAIL){
		ret = -1;
	}else{
		ret = ch->cur_ch;
	}

	ReleaseMutex(ch->ch_lock);

	return ret;
}

SOCKET get_failover_connect(DWORD timeout)
{
	int ret;
	SOCKET sock;

	channel_t *ch = g_node->ch_info;

	WaitForSingleObject(ch->ch_lock, INFINITE); 

	LOG_DEBUG(LOG_MOD_MAIN, "get Connect Info try, %d:%d", ch->status, ch->conn);

	if((ch->status & CH_NODE_CONNECTED) || ch->status == CH_NODE_FAIL){
		ResetEvent(ch->conn_event);
	}else{
		ReleaseMutex(ch->ch_lock);

		ret = WaitForSingleObject(ch->conn_event, timeout);
		if(ret == WAIT_TIMEOUT){
			LOG_DEBUG(LOG_MOD_MAIN, "get Connect Info Fail");
			return INVALID_SOCKET;
		}

		WaitForSingleObject(ch->ch_lock, INFINITE); 
	}

	if(ch->status == CH_NODE_FAIL){
		sock = INVALID_SOCKET;
	}else{
		sock = ch->conn;
	}

	ReleaseMutex(ch->ch_lock);

	return sock;
}

int set_failover_channel(int status, int ch_id)
{
	int ret = 0;

	channel_t *ch = g_node->ch_info;

	LOG_DEBUG(LOG_MOD_MAIN, "Set Channel Info");

	WaitForSingleObject(ch->ch_lock, INFINITE);

	switch(status){
		case MDP_CTRL_MSG_CURRENT_CHANNEL:
			status = CH_READY;
			break;
		case MDP_CTRL_MSG_REMOTE_FAIL:
		case MDP_CTRL_MSG_NETWORK_FAIL:
			status = CH_NODE_FAIL;
			ch_id = -1;
			break;
	}

	if(ch->status == CH_INIT){
		ch->cur_ch = ch_id;
	}else if(ch->status == CH_NODE_CONNECTED){
		ch->status &= status;
	}else{
		ch->status = status;
	}

	if(ch->cur_ch != ch_id){
		ch->cur_ch = ch_id;

		// Close session sock
		if(ch->conn != INVALID_SOCKET && (ch->status == CH_READY || ch->status == CH_NODE_FAIL)){
			LOG_DEBUG(LOG_MOD_MAIN, "Close Session socket : 0x%x", ch->conn);
			closesocket(ch->conn);
			ch->conn = INVALID_SOCKET;
			g_node->status = NODE_STATUS_FAILOVER;
		}
	}

	SetEvent(ch->ch_event);

	ReleaseMutex(ch->ch_lock);

	return ret;
}

int set_failover_conn(SOCKET conn)
{
	channel_t *ch = g_node->ch_info;

	LOG_DEBUG(LOG_MOD_MAIN, "Set Connect Info");

	WaitForSingleObject(ch->ch_lock, INFINITE); 

	ch->conn = conn;

	if(conn != SOCKET_ERROR){
		if(ch->status == CH_READY)
			ch->status &= CH_NODE_CONNECTED;
		else
			ch->status = CH_NODE_CONNECTED;
	}else{
		LOG_DEBUG(LOG_MOD_MAIN, "Set Connect Info : Node Fail");
		ch->status = CH_NODE_FAIL;
	}

	SetEvent(ch->conn_event);

	ReleaseMutex(ch->ch_lock);

	return 0;
}

void set_session_connected(SOCKET sock)
{
	channel_t *ch = g_node->ch_info;

	LOG_DEBUG(LOG_MOD_MAIN, "Set Connect Info");

	WaitForSingleObject(ch->ch_lock, INFINITE); 

	LOG_DEBUG(LOG_MOD_MAIN, "Set Connect Info : 0x%x", sock);

	ch->conn = sock;

	ch->status = CH_SESSION_CONNECTED;

	ReleaseMutex(ch->ch_lock);
}

void set_failover_complete()
{
	int ret = 0;

	channel_t *ch = g_node->ch_info;

	LOG_DEBUG(LOG_MOD_MAIN, "Set FailOver Complete");

	WaitForSingleObject(ch->ch_lock, INFINITE);

	ch->status = CH_SESSION_CONNECTED;

	ReleaseMutex(ch->ch_lock);
}

int init_service(node_t *node)
{
	int ret, i, opt;

	if(!node->local_rep){
		if(node->hb){
			hb_start(node->hb);
		}else{
			/* req init nic */
			mdp_ctrl(node->mdp_controller, MDP_CTRL_MSG_REQ_CURRENT_CN, -1, 0, NULL);
		}
	}

	if(node->mode == INITSYNC_MODE_SOURCE)
	{
		if(node->nr_nics > 0){
			node->socks = malloc(sizeof(SOCKET) * node->nr_nics);
			if(!node->socks){
				LOG_ERROR(LOG_MOD_MAIN, "out of memory at %s:%d", __FILE__, __LINE__);
				return -1;
			}
		}

		for(i = 0; i < node->nr_nics; i++)
		{
			node->socks[i] = net_tcp_socket();
			if (node->socks[i] == INVALID_SOCKET) {
				LOG_ERROR(LOG_MOD_MAIN, "Socket Create Fail");
				return -1;
			}

			ret = net_set_sendbuf_size(node->socks[i], g_sock_buf_size);
			if (ret == SOCKET_ERROR) {
				LOG_ERROR(LOG_MOD_MAIN, "setsockopt SO_SNDBUF[%d] fail", g_sock_buf_size);
				return -1;
			}

			ret = net_set_recvbuf_size(node->socks[i], g_sock_buf_size);
			if (ret == SOCKET_ERROR) {
				LOG_ERROR(LOG_MOD_MAIN, "setsockopt SO_RCVBUF[%d] fail", g_sock_buf_size);
				return -1;
			}

			ret = net_bind(node->socks[i], node->nic[i], node->port);
			if (ret == SOCKET_ERROR) {
				LOG_ERROR(LOG_MOD_MAIN, "Bind Fail %s:%d", node->nic[i], node->port);
				return -1;
			}

			ret = net_set_reuseaddr(node->socks[i]);
			if (ret < 0) {
				LOG_ERROR(LOG_MOD_MAIN, "set reuseaddr fail");
				return -1;
			}

			ret = net_set_nonblock(node->socks[i]);
			if (ret < 0) {
				LOG_ERROR(LOG_MOD_MAIN, "set nonblock fail");
				return -1;
			}

			ret = net_listen(node->socks[i]);
			if (ret == SOCKET_ERROR) {
				LOG_ERROR(LOG_MOD_MAIN, "listen fail");
				return -1;
			}

			LOG_INFO(LOG_MOD_MAIN, "Listen %s:%d", node->nic[i], node->port);

			ret = recv_regist((dispatcher_t *)node->dis, node->socks[i], node, session_accept_handler);
		}
	}else{ /* target node */
		ret = connect_to_all_source(node);
		if(ret < 0){
			LOG_ERROR(LOG_MOD_MAIN, "Connect to Source Node Fail");
			return -1;
		}

		// for SD
		if(node->nr_nics > 0){
			node->socks = malloc(sizeof(SOCKET) * node->nr_nics);
			if(!node->socks){
				LOG_ERROR(LOG_MOD_MAIN, "out of memory at %s:%d", __FILE__, __LINE__);
				return -1;
			}
		}

		for(i = 0; i < node->nr_nics; i++)
		{
			node->socks[i] = net_tcp_socket();
			if (node->socks[i] == INVALID_SOCKET) {
				LOG_ERROR(LOG_MOD_MAIN, "Socket Create Fail");
				return -1;
			}

			ret = net_set_sendbuf_size(node->socks[i], g_sock_buf_size);
			if (ret == SOCKET_ERROR) {
				LOG_ERROR(LOG_MOD_MAIN, "setsockopt SO_SNDBUF[%d] fail", g_sock_buf_size);
				return -1;
			}

			ret = net_set_recvbuf_size(node->socks[i], g_sock_buf_size);
			if (ret == SOCKET_ERROR) {
				LOG_ERROR(LOG_MOD_MAIN, "setsockopt SO_RCVBUF[%d] fail", g_sock_buf_size);
				return -1;
			}

			ret = net_bind(node->socks[i], node->nic[i], node->port);
			if (ret == SOCKET_ERROR) {
				LOG_ERROR(LOG_MOD_MAIN, "Bind Fail %s:%d", node->nic[i], node->port);
				return -1;
			}

			ret = net_set_reuseaddr(node->socks[i]);
			if (ret < 0) {
				LOG_ERROR(LOG_MOD_MAIN, "set reuseaddr fail");
				return -1;
			}

			ret = net_set_nonblock(node->socks[i]);
			if (ret < 0) {
				LOG_ERROR(LOG_MOD_MAIN, "set nonblock fail");
				return -1;
			}

			ret = net_listen(node->socks[i]);
			if (ret == SOCKET_ERROR) {
				LOG_ERROR(LOG_MOD_MAIN, "listen fail");
				return -1;
			}

			LOG_INFO(LOG_MOD_MAIN, "Listen For SD %s:%d", node->nic[i], node->port);

			ret = recv_regist((dispatcher_t *)node->dis, node->socks[i], node, session_accept_handler);
		}
	}

	time_event_set_timer(node->dis, 10, node, destroy_service_handler);
	time_event_set_timer(node->dis, 10000, node, log_level_handler);

	/* start local Initsync */
	if(node->local_rep)
		session_local_initsync(node);

	return 0;
}

int pause_node_handler(node_t *node, int flag)
{
	//if(node->status == NODE_STATUS_WAIT)
	//	NOTIFY_STOP(node->sd_notifier, 0);

	g_stopFlag = flag;

	return SD_CMD_SUCCESS;
}

int resume_node_handler(node_t *node)
{
	list_node_t *pos, *nxt;
	remote_host_table_entry_t *remote;
	session_manager_t *sm = node->session_manager;
	ip_t *ip;
	int ret, ret_info;

	//g_pauseFlag = 0;
	g_stopFlag = 0;
	node->resume = 1;

	//if(node->status == NODE_STATUS_PAUSE)
		//NOTIFY_WAIT(node->sd_notifier);

	node->status = NODE_STATUS_WAIT;

	/* create session thread */
	if(node->mode == INITSYNC_MODE_SOURCE){
		list_for_each_safe(pos, nxt, &sm->remote_table_list)
		{
			remote = list_entry(pos, remote_host_table_entry_t, lnode);
			if(!remote)
			{
				LOG_ERROR(LOG_MOD_MAIN, "there are no remote host info");
				return SD_CMD_FAIL;
			}

			ip = list_first_entry(&remote->ip_list, ip_t, lnode);
			LOG_INFO(LOG_MOD_MAIN, "Send Resume CMD to %s", ip->ip);
			ret = initsyncCmd(remote->id, ip->ip, node->port, SD_CMD_RESUME, &ret_info);
		}
	}else if(node->mode == INITSYNC_MODE_TARGET){
		ret = connect_to_all_source(node);
		if(ret < 0){
			LOG_ERROR(LOG_MOD_MAIN, "Resume Connect Fail");
		}
	}else if(node->mode == INITSYNC_MODE_LOCAL_REPLICATION){
		session_local_initsync(node);
	}

	return SD_CMD_SUCCESS;
}

int stop_to_target(node_t *node, int cn, int flag)
{
	list_node_t *pos, *nxt;
	remote_host_table_entry_t *remote;
	session_manager_t *sm = node->session_manager;
	ip_t *ip;
	int ret, ret_info, i;

	list_for_each_safe(pos, nxt, &sm->remote_table_list)
	{
		remote = list_entry(pos, remote_host_table_entry_t, lnode);
		if(!remote)
		{
			LOG_ERROR(LOG_MOD_MAIN, "there are no remote host info");
			return SD_CMD_FAIL;
		}
		
		ip = list_first_entry(&remote->ip_list, ip_t, lnode);

		for(i = 0; i < cn; i++){
			ip = list_next_entry(ip, ip_t, lnode);
		}

		LOG_INFO(LOG_MOD_MAIN, "Send Stop CMD to %s", ip->ip);
		ret = initsyncCmd(remote->id, ip->ip, node->port, flag, &ret_info);
		
		return ret;
	}

	return SD_CMD_SUCCESS;
}

int stop_node_handler(node_t *node, int flag)
{
	int ret = SD_CMD_SUCCESS;
	int cn;

	LOG_DEBUG(LOG_MOD_MAIN, "Get STOP CMD, node state : %d, flag : %d", node->status, flag);

	if(node->status == NODE_STATUS_WAIT || node->status == NODE_STATUS_PAUSE){
		
		if(node->mode == INITSYNC_MODE_SOURCE){
			cn = get_current_channel(NON_BLOCKING_CALL);
			
			if(cn > -1){
				LOG_DEBUG(LOG_MOD_MAIN, "Send Stop CMD to Target");
				ret = stop_to_target(node, cn, flag);
			}
		}
		mdp_ctrl_stop(node->mdp_controller, 0, -1, NULL, ((flag & SD_CMD_STOP_WITH_MDP) ? 1:0));
	
		NOTIFY_STOP(node->sd_notifier, 0);
	}

	g_stopFlag = flag;

	return ret;
}

void node_failover_handler(void *dis, void *handle, void *data, uint32 timeout)
{
	mdp_ctrl_stop(g_node->mdp_controller, 0, -1, NULL, 1);
}

int node_failover_process(int nodeFailFlag, int channel_id)
{
	LOG_DEBUG(LOG_MOD_MAIN, "GET NETOWKF INFO : %d:%d", nodeFailFlag, channel_id);

	set_failover_channel(nodeFailFlag, channel_id);

	if(nodeFailFlag == MDP_CTRL_MSG_REMOTE_FAIL || nodeFailFlag == MDP_CTRL_MSG_NETWORK_FAIL){
		LOG_INFO(LOG_MOD_MAIN, "All Network or Remote Node Fail");

		set_failover_conn(SOCKET_ERROR);

		g_stopFlag = POW_2_4|POW_2_5;
		if(g_node->status != NODE_STATUS_RUN){
			NOTIFY_FAIL(g_node->sd_notifier, 0);
			time_event_set_timeout(g_node->dis, 10, g_node, node_failover_handler);
		}	
	}

	LOG_DEBUG(LOG_MOD_MAIN, "GET NETOWKF INFO END");
	
	return 0;
}