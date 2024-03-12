#include "session_manager.h"
#include "session.h"
#include "file_manager.h"
#include "node.h"
#include "network_api.h"
#include "report.h"

extern node_t *g_node;

extern int g_file_buf_size;
extern int g_send_buf_size;
extern int g_recv_buf_size;
extern int g_sock_buf_size;

session_manager_t *init_session_manager(void *node)
{
	session_manager_t *sm;

	sm = malloc(sizeof(session_manager_t));
	if (!sm){
		LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		return NULL;
	}

	memset(sm, 0,sizeof(session_manager_t));

	sm->node = node;

	/* session list init */
	init_list_node(&sm->session_list);
	
	/* access table init */
	init_list_node(&sm->remote_table_list);

	/* mutex init */
	sm->sm_mutex = CreateMutex(NULL, FALSE, NULL);
	if(!sm->sm_mutex)
	{
		LOG_ERROR(LOG_MOD_SESSION, "mutex create fail %s:%d", __FILE__, __LINE__);
		goto out_error;
	}

	sm->local_rep_sync_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!sm->local_rep_sync_event)
	{
		LOG_ERROR(LOG_MOD_SESSION, "EVENT create fail %s:%d", __FILE__, __LINE__);
		goto out_error;
	}

	return sm;

out_error:
	destroy_session_manager(sm);
	return NULL;
}

int destroy_session(session_t *s)
{
	session_manager_t *sm = s->sm;
	file_list_t *fl = s->flist; 
	node_t *node = sm->node;
	int ret;

	if(s){
		if(s->sock != INVALID_SOCKET){
			net_closesocket(s->sock);
			s->sock = INVALID_SOCKET;
		}

		if(s->local_rep_mode != INITSYNC_MODE_UNKNOWN){
			close_local_connect(s);
		}
		
		ret = WaitForSingleObject(s->destroy_recv_th_event, 1000);
		if(ret == WAIT_TIMEOUT){
			// if timeout
			TerminateThread(s->recv_thread, -1);
		}
		LOG_DEBUG(LOG_MOD_SESSION, "Recv Th Terminate");

		ret = WaitForSingleObject(s->destroy_send_th_event, 1000);
		if(ret == WAIT_TIMEOUT){
			// if timeout
			TerminateThread(s->send_thread, -1);
		}
		LOG_DEBUG(LOG_MOD_SESSION, "Send Th Terminate");

		ret = WaitForSingleObject(s->destroy_send_file_th_event, 1000);
		if(ret == WAIT_TIMEOUT){
			// if timeout
			TerminateThread(s->send_file_thread, -1);
		}
		LOG_DEBUG(LOG_MOD_SESSION, "Send File Th Terminate");


		if(s->local_rep_mode == INITSYNC_MODE_UNKNOWN){
			destroy_queue(s->recv_Q, s->buf_pool, free_buf);
			s->recv_Q = NULL;
			destroy_queue(s->send_Q, s->buf_pool, free_buf);
			s->send_Q = NULL;
		}

		SAFE_CLOSE_HANDLE(s->destroy_recv_th_event);
		SAFE_CLOSE_HANDLE(s->destroy_send_th_event);
		SAFE_CLOSE_HANDLE(s->destroy_send_file_th_event);
		SAFE_FREE(s->name);
		SAFE_FREE(s->remote_ip);

		if(s->run_state == SESSION_RUN_STATE_COMPLETE){
			/* delete history files */
			flist_dealloc(fl->fm, fl, 1);
		}else{
			flist_dealloc(fl->fm, fl, 0);
		}

		s->flist = NULL;
		
		//if(s->run_state != SESSION_RUN_STATE_STOP || node->mode != INITSYNC_MODE_SOURCE)
		if(s->run_state == SESSION_RUN_STATE_COMPLETE)
			sm->nr_complete++;

		SAFE_FREE(s);
	}
	return 0;
}

int destroy_table_entry(remote_host_table_entry_t *te)
{
	list_node_t *pos, *next;
	ip_t *ip;

	if(te){
		/* ip list */
		if (!list_empty(&te->ip_list)) {
			list_for_each_safe(pos, next, &te->ip_list) {
				ip = list_entry(pos, ip_t, lnode);
				SAFE_FREE(ip->ip);
				SAFE_FREE(ip);
				te->nr_ips--;
			}
		}

		SAFE_FREE(te->name);

		SAFE_FREE(te);
	}

	return 0;
}

int destroy_session_manager(session_manager_t *sm)
{
	session_t *s;
	remote_host_table_entry_t *te;
	list_node_t *pos, *next;

	LOG_INFO(LOG_MOD_MAIN, "+- Destoroy Session Manager -+");

	if(sm){
#if 0
		/* init list */
		if (!list_empty(&sm->init_list)) {
			list_for_each_safe(pos, next, &sm->init_list) {
				s = list_entry(pos, session_t, lnode);
				destroy_session(s);
				sm->nr_init--;
			}
		}
		/* run list */
		if (!list_empty(&sm->run_list)) {
			list_for_each_safe(pos, next, &sm->run_list) {
				s = list_entry(pos, session_t, lnode);
				destroy_session(s);
				sm->nr_run--;
			}
		}
		/* failover list */
#endif

		/* Lock */
		WaitForSingleObject(sm->sm_mutex, INFINITE);

		/* accept table */
		if (!list_empty(&sm->remote_table_list)) {
			list_for_each_safe(pos, next, &sm->remote_table_list) {
				te = list_entry(pos, remote_host_table_entry_t, lnode);
				destroy_table_entry(te);
				sm->nr_remote_table--;
			}
		}

		/* Release */
		ReleaseMutex(sm->sm_mutex);

		SAFE_CLOSE_HANDLE(sm->sm_mutex);

		SAFE_FREE(sm);
	}
	return 0;
}

ack_list_t *init_ack_list(int total)
{
	ack_list_t *al;
	not_acked_file_t *nf;
	int i;

	al = malloc(sizeof(ack_list_t));
	if(!al){
		LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		return NULL;
	}

	memset(al, 0, sizeof(ack_list_t));

	al->nr_total = total;
	init_list_node(&al->ack_list);
	init_list_node(&al->free_list);
	
	for(i = 0; i < total; i++)
	{
		nf = malloc(sizeof(not_acked_file_t));
		if(!nf){
			LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
			SAFE_FREE(al);
			return NULL;
		}

		memset(nf, 0, sizeof(not_acked_file_t));

		list_add_tail(&nf->lnode, &al->free_list);
		al->nr_free++;
	}

	InitializeCriticalSection(&al->cs);

	return al;
}

session_t *session_alloc(session_manager_t *sm)
{
	session_t *s;
	node_t *node = sm->node;

	/* session 할당 -> init_list에 넣는다. */
	s = malloc(sizeof(session_t));
	if (!s) {
		LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		return NULL;
	}
	memset(s, 0, sizeof(session_t));
	s->sock = INVALID_SOCKET;
	s->state = SESSION_STATE_INIT;
	s->run_state = SESSION_RUN_STATE_INIT;
	s->flist = NULL;
	s->resume = 0;

	if(node->mode != INITSYNC_MODE_LOCAL_REPLICATION){
		if(node->mode == INITSYNC_MODE_SOURCE){
			s->recv_Q = init_queue(DEFAULT_QUEUE_SIZE);
			s->send_Q = init_queue(LARGE_QUEUE_SIZE);
		}else if(node->mode == INITSYNC_MODE_TARGET){
			s->recv_Q = init_queue(LARGE_QUEUE_SIZE);
			s->send_Q = init_queue(DEFAULT_QUEUE_SIZE);
		}
		set_queue_nagle(s->send_Q, QUEUE_NAGLE_OFF);

		s->buf_pool = init_buf_pool(LARGE_QUEUE_SIZE + DEFAULT_QUEUE_SIZE, g_file_buf_size);
		if(!s->buf_pool)
		{
			LOG_ERROR(LOG_MOD_SESSION, "Create buffer pool fail");
			SAFE_FREE(s);
			return NULL;
		}
	}

	s->ack_list = init_ack_list(DEFAULT_QUEUE_SIZE);
	if(!s->ack_list)
	{
		LOG_ERROR(LOG_MOD_SESSION, "Create not-acked list fail");
		SAFE_FREE(s);
		return NULL;
	}

	//s->destroy_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	//if(!s->destroy_event)
	//{
	//	LOG_ERROR("event create fail %s:%d", __FILE__, __LINE__);
	//	SAFE_FREE(s);
	//	return NULL;
	//}

	s->destroy_recv_th_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!s->destroy_recv_th_event)
	{
		LOG_ERROR(LOG_MOD_SESSION, "event create fail %s:%d", __FILE__, __LINE__);
		SAFE_FREE(s);
		return NULL;
	}

	s->destroy_send_th_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!s->destroy_send_th_event)
	{
		LOG_ERROR(LOG_MOD_SESSION, "event create fail %s:%d", __FILE__, __LINE__);
		SAFE_FREE(s);
		return NULL;
	}

	s->destroy_send_file_th_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!s->destroy_send_file_th_event)
	{
		LOG_ERROR(LOG_MOD_SESSION, "event create fail %s:%d", __FILE__, __LINE__);
		SAFE_FREE(s);
		return NULL;
	}

	//ResetEvent(s->destroy_event);
	ResetEvent(s->destroy_recv_th_event);
	ResetEvent(s->destroy_send_th_event);

	s->recv_failover_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!s->recv_failover_event)
	{
		LOG_ERROR(LOG_MOD_SESSION, "event create fail %s:%d", __FILE__, __LINE__);
		SAFE_FREE(s);
		return NULL;
	}

	s->send_failover_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!s->recv_failover_event)
	{
		LOG_ERROR(LOG_MOD_SESSION, "event create fail %s:%d", __FILE__, __LINE__);
		SAFE_FREE(s);
		return NULL;
	}

	// 생성시 Reset
	SetEvent(s->destroy_send_file_th_event);

	/* Lock */
	WaitForSingleObject(sm->sm_mutex, INFINITE);

	s->sm = sm;
	s->resume = node->resume;
	list_add_tail(&s->lnode, &sm->session_list);
	++sm->nr_total;
	//++sm->nr_wait;

	node->status = NODE_STATUS_RUN;

	/* Release */
	ReleaseMutex(sm->sm_mutex);

	return s;
}

#if 0
int session_sched_wait(session_t *s)
{
	session_manager_t *sm = s->sm;
	node_t *node = sm->node;

	/* Lock */
	WaitForSingleObject(sm->sm_mutex, INFINITE);

	switch(s->state){
		case SESSION_STATE_RUN:
			--sm->nr_run;
			break;
		case SESSION_STATE_PAUSE:
			--sm->nr_pause;
			break;
	}
	list_move_tail(&s->lnode, &sm->wait_list);
	s->state = SESSION_STATE_WAIT;
	++sm->nr_wait;

	/* Release */
	ReleaseMutex(sm->sm_mutex);

	return 0;
}

int session_sched_run(session_t *s)
{
	session_manager_t *sm = s->sm;
	node_t *node = sm->node;

	/* Lock */
	WaitForSingleObject(sm->sm_mutex, INFINITE);
	
	list_move_tail(&s->lnode, &sm->run_list);
	s->state = SESSION_STATE_RUN;
	--sm->nr_wait;
	++sm->nr_run;

	node->status = NODE_STATUS_RUN;

	/* Release */
	ReleaseMutex(sm->sm_mutex);

	return 0;
}

int session_sched_pause(session_t *s)
{
	session_manager_t *sm = s->sm;
	node_t *node = sm->node;

	/* Lock */
	WaitForSingleObject(sm->sm_mutex, INFINITE);
	
	list_move_tail(&s->lnode, &sm->pause_list);
	s->state = SESSION_STATE_PAUSE;
	--sm->nr_run;
	++sm->nr_pause;

	/* Release */
	ReleaseMutex(sm->sm_mutex);

	return 0;
}

int session_sched_complete(session_t *s)
{
	session_manager_t *sm = s->sm;
	node_t *node = sm->node;

	/* Lock */
	WaitForSingleObject(sm->sm_mutex, INFINITE);
	
	list_move_tail(&s->lnode, &sm->complete_list);
	s->state = SESSION_STATE_COMPLETE;
	--sm->nr_run;
	++sm->nr_complete;

	/* Release */
	ReleaseMutex(sm->sm_mutex);

	return 0;
}
#endif

int session_dealloc(session_t *s)
{
	session_manager_t *sm = s->sm;

	/* Lock */
	WaitForSingleObject(sm->sm_mutex, INFINITE);

	LOG_DEBUG(LOG_MOD_SESSION, "Session Dealloc");
	list_del(&s->lnode);
	destroy_session(s);
	sm->nr_total--;

	LOG_DEBUG(LOG_MOD_SESSION, "# of Active Session : %d", sm->nr_total);

	/* Release */
	ReleaseMutex(sm->sm_mutex);

	//SetEvent(s->destroy_event);
	return 0;
}

int add_remote_host_table(session_manager_t *sm, char *name, char *ip, int id)
{
	remote_host_table_entry_t *a;
	ip_t *ip_entry;

	/* name 으로 기존 table entry search */
	a = find_remote_host_table_by_name(sm, name);
	if(!a){
		/* table entry 할당 -> table_list에 넣는다. */
		a = malloc(sizeof(remote_host_table_entry_t));
		if (!a) {
			LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
			return -1;
		}

		memset(a, 0, sizeof(remote_host_table_entry_t));
		a->name = malloc(strlen(name) + 1);
		if(!a->name){
			LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
			SAFE_FREE(a);
			return -1;
		}
		strcpy(a->name, name);

		a->id = id;

		init_list_node(&a->ip_list);

		list_add_tail(&a->lnode, &sm->remote_table_list);
		++sm->nr_remote_table;
	}
	
	ip_entry = malloc(sizeof(ip_t));
	if(!ip_entry){
		LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		SAFE_FREE(a->name);
		SAFE_FREE(a);
		return -1;
	}

	ip_entry->ip = malloc(strlen(ip) + 1);
	if(!ip_entry->ip)
	{
		LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		SAFE_FREE(ip_entry);
		SAFE_FREE(a->name);
		SAFE_FREE(a);
		return -1;
	}
	strcpy(ip_entry->ip, ip);

	list_add_tail(&ip_entry->lnode, &a->ip_list);
	++a->nr_ips;
	
	return 0;
}

int delete_remote_host_table(session_manager_t *sm, char *ip)
{
	return 0;
}

remote_host_table_entry_t *session_accept_check(session_manager_t *sm, char *ip)
{
	return find_remote_host_table_by_ip(sm, ip);
}

remote_host_table_entry_t *find_remote_host_table_by_name(session_manager_t *sm, char *name)
{
	list_node_t *pos, *nxt;
	remote_host_table_entry_t *remote;

	list_for_each_safe(pos, nxt, &sm->remote_table_list)
	{
		remote = list_entry(pos, remote_host_table_entry_t, lnode);
		if(!strcmp(name, remote->name))
			return remote;
	}
	return NULL;
}

remote_host_table_entry_t *find_remote_host_table_by_ip(session_manager_t *sm, char *ip)
{
	list_node_t *pos, *pos2, *nxt, *nxt2;
	remote_host_table_entry_t *remote;
	ip_t *ip_entry;

	list_for_each_safe(pos, nxt, &sm->remote_table_list)
	{
		remote = list_entry(pos, remote_host_table_entry_t, lnode);
		
		list_for_each_safe(pos2, nxt2, &remote->ip_list)
		{
			ip_entry = list_entry(pos2, ip_t, lnode);
			if(!strcmp(ip, ip_entry->ip))
				return remote;
		}
	}
	return NULL;
}
