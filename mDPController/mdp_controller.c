#include "mdp_controller.h"
#include "report.h"
#include "node.h"
#include <errno.h>
#include <process.h>
#include "checksum.h"
#include <stdio.h>
#include <tchar.h>
#include "file_manager.h"
#include "node.h"
#include "mdp_rename_table.h"

extern int g_stopFlag;
extern int g_nodeChannelInfo;
extern int g_alone;

#define GET_INITSYNC_MDP_WRITE_PIPE_NAME(a) sprintf_s(a, MAX_PATH, "\\\\.\\pipe\\Initsync_0_I")
#define GET_INITSYNC_MDP_READ_PIPE_NAME(a) sprintf_s(a, MAX_PATH, "\\\\.\\pipe\\Initsync_0_M")

int init_mdp_connection(mdp_controller_t *mc, int index){
	int ret, i;

	/* init pipe */
	GET_INITSYNC_WPIPE_NAME(mc->pipename, index);
	LOG_INFO(LOG_MOD_MDP_CNTRL, "Create Pipe %s", mc->pipename);
	mc->hPipeW = CreateNamedPipeA(mc->pipename,
		//mc->hPipeW = CreateNamedPipeA("\\\\.\\pipe\\Initsync_0_I",
		PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,
		4096,
		4096,
		NMPWAIT_USE_DEFAULT_WAIT,
		NULL);
	if(mc->hPipeW == INVALID_HANDLE_VALUE)
	{
		LOG_ERROR(LOG_MOD_MDP_CNTRL, "CreateNamePipe Fail, error code : %d", GetLastError());
		goto out_error;
	}

	GET_INITSYNC_RPIPE_NAME(mc->pipenameR, index);
	LOG_INFO(LOG_MOD_MDP_CNTRL, "Create Pipe %s", mc->pipenameR);
	mc->hPipeR = CreateNamedPipeA(mc->pipenameR,
		//mc->hPipeR = CreateNamedPipeA("\\\\.\\pipe\\Initsync_0_M",
		PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_BYTE| PIPE_WAIT,
		1,
		4096,
		4096,
		NMPWAIT_USE_DEFAULT_WAIT,
		NULL);
	if(mc->hPipeR == INVALID_HANDLE_VALUE)
	{
		LOG_ERROR(LOG_MOD_MDP_CNTRL, "CreateNamePipe Fail, error code : %d", GetLastError());
		goto out_error;
	}

	DisconnectNamedPipe(mc->hPipeW);
	DisconnectNamedPipe(mc->hPipeR);

	memset(&mc->send_event, 0, sizeof(mc->send_event));

	mc->send_event.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	memset(&mc->read_event, 0, sizeof(mc->read_event));

	mc->read_event.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	LOG_INFO(LOG_MOD_MDP_CNTRL, "Wait Connect from mDP");
	if(ConnectNamedPipe(mc->hPipeW, &mc->send_event) == 0){
		/* connect fail */
		int err;// = GetLastError();

		do{
			err = GetLastError();

			if(err != ERROR_IO_PENDING && err != ERROR_IO_INCOMPLETE){
				SAFE_CLOSE_HANDLE(mc->send_event.hEvent);
				LOG_ERROR(LOG_MOD_MDP_CNTRL, "mDP PIPE Connection Fail, %d", err);
				goto out_error;	
			}

			ret = WaitForSingleObject(mc->send_event.hEvent, 5000);
			if(ret == WAIT_TIMEOUT){
				SAFE_CLOSE_HANDLE(mc->send_event.hEvent);
				LOG_ERROR(LOG_MOD_MDP_CNTRL, "mDP PIPE Connection Timeout");
				goto out_error;
			}


		}while(GetOverlappedResult(mc->hPipeW, &mc->send_event, &i, FALSE) == FALSE);
	}

	if(ConnectNamedPipe(mc->hPipeR, &mc->read_event) == 0){
		/* connect fail */
		int err;// = GetLastError();

		do{
			err = GetLastError();

			if(err != ERROR_IO_PENDING && err != ERROR_IO_INCOMPLETE){
				SAFE_CLOSE_HANDLE(mc->read_event.hEvent);
				LOG_ERROR(LOG_MOD_MDP_CNTRL, "mDP PIPE Connection Fail, %d", err);
				goto out_error;	
			}

			ret = WaitForSingleObject(mc->read_event.hEvent, 1000);
			if(ret == WAIT_TIMEOUT){
				SAFE_CLOSE_HANDLE(mc->read_event.hEvent);
				LOG_ERROR(LOG_MOD_MDP_CNTRL, "mDP PIPE Connection Timeout");
				goto out_error;
			}

		}while(GetOverlappedResult(mc->hPipeR, &mc->read_event, &i, FALSE) == FALSE);
	}

	return 0;

out_error:
	return -1;
}

void mdp_ctrl_delete_dir(mdp_controller_t *mc, TCHAR *old_path)
{
	list_node_t *pos, *nxt;
	hash_entry_t *entry;

	list_for_each_safe(pos, nxt, &mc->rename_event_list)
	{
		TCHAR *p;

		entry = list_entry(pos, hash_entry_t, lnode);
		if(entry){
			if(entry->type < MDP_CTRL_MSG_DELETE_DIR)
				continue;
			p = _tcsstr((TCHAR *)entry->data, old_path);
			if(p == NULL || p != (TCHAR *)entry->data){
				continue;
			}else{
				p += entry->datalen / sizeof(TCHAR) - 1;
				if(*p == '\\' || *p == 0){ /* Hit */
					list_del(&entry->lnode);
					SAFE_FREE(entry->data);
					SAFE_FREE(entry);

					mc->nr_rename_event--;
				}
			}
		}
	}
}

int mdp_ctrl_pathDepthCheck( TCHAR *pr_path, int pr_len )
{
	int  i     = 0;
	int  depth = 0;


	for( i = 0; i < pr_len; i++ )
	{
		if( pr_path[i] == _T('\\') )
			depth++;
	}

	return depth;
}

int dir_check(mdp_controller_t *mc, TCHAR *new_path)
{
	list_node_t *pos, *nxt;
	dir_t *dir;
	TCHAR *p;
	int length, depth;

	length = _tcslen(new_path);
	depth = mdp_ctrl_pathDepthCheck(new_path, length);

	list_for_each_safe(pos, nxt, &mc->dir_list){
		dir = list_entry(pos, dir_t, lnode);
		if(dir && !(dir->length > length) && !(dir->depth > depth) && !_tcsncmp(dir->name, new_path, dir->length)){
			return INSIDE_INTEREST_DIR;
		}
	}

	return OUTSIDE_INTEREST_DIR;
}

unsigned int WINAPI mdp_ctrl_recv_thread(LPVOID arg)
{
	mdp_controller_t *mc = arg;
	mdp_ctrl_msg_hdr_t msg_hdr;
	mdp_ctrl_queue_t *entry;
	node_t *node = (node_t *)mc->node;

	char data[2048];
	int ret, readn, written, pos;

	while(1){
		if(mc->destroy_flag)
			goto out_mdp_ctrl_recv; //return 0;

		pos = 0;
		while(pos < sizeof(mdp_ctrl_msg_hdr_t)){
			ret = ReadFile(mc->hPipeR, (char *)(&msg_hdr) + pos, sizeof(mdp_ctrl_msg_hdr_t) - pos, &readn, &mc->read_event);
			if((!ret) || readn == 0)
			{
				int err;

				do{
					if(mc->destroy_flag)
						goto out_mdp_ctrl_recv; //return 0;

					err = GetLastError();

					if(err != ERROR_IO_PENDING && err != ERROR_IO_INCOMPLETE){
						/* error */
						LOG_ERROR(LOG_MOD_MDP_CNTRL, "Read Pipe Fail, error code : %d", err);
						goto out_error_mdp_ctrl_recv;
					}

					WaitForSingleObject(mc->read_event.hEvent, 1000);

				}while(GetOverlappedResult(mc->hPipeR, &mc->read_event, &readn, FALSE) == FALSE);
			}
				
			pos += readn;
		}

		if(msg_hdr.type < MDP_CTRL_MSG_RENAME_DIR){
			/* Queue Critical Section for Processing Ack */
			EnterCriticalSection(&mc->queue_cs);

			LOG_DEBUG(LOG_MOD_MDP_CNTRL, "Read From Pipe (%d, %d), Queued Session num : %d", msg_hdr.type, msg_hdr.seq,  mc->nr_queue);
			if(mc->nr_queue > 0 && !list_empty(&mc->queue_list)){
				entry = list_first_entry(&mc->queue_list, mdp_ctrl_queue_t, lnode);
				if(msg_hdr.seq == entry->seq){
					if(entry->type + 1 == msg_hdr.type){
						entry->rtv = msg_hdr.length; // mdp_hdr의 length 를 return 값으로 사용한다.
						SetEvent(entry->mdp_event);
					}
				}else{
					/* error */
					LOG_ERROR(LOG_MOD_MDP_CNTRL, "mdp ctrl msg - Seq Number Fail queue : %d, mdp sends : %d", entry->seq, msg_hdr.seq);
				}
			}else{
				/* error */
				LOG_ERROR(LOG_MOD_MDP_CNTRL, "Not Acked File list is Empty");
			}

			LeaveCriticalSection(&mc->queue_cs);
		}else{ // rename, delete, Fail info
			TCHAR *old_path = NULL;
			TCHAR *new_path = NULL;
			int type;

			if(msg_hdr.length > 0){
				pos = 0;
				while(pos < msg_hdr.length){
					ret = ReadFile(mc->hPipeR, data + pos, msg_hdr.length - pos, &readn, &mc->read_event);
					if((!ret) || readn == 0)
					{
						int err;

						do{
							if(mc->destroy_flag)
								goto out_mdp_ctrl_recv; //return 0;

							err = GetLastError();

							if(err != ERROR_IO_PENDING && err != ERROR_IO_INCOMPLETE){
								/* error */
								LOG_ERROR(LOG_MOD_MDP_CNTRL, "Read Pipe Fail, error code : %d", err);
								
								node_failover_process(MDP_CTRL_MSG_NETWORK_FAIL, -1);

								goto out_error_mdp_ctrl_recv;
							}

							WaitForSingleObject(mc->read_event.hEvent, 1000);

						}while(GetOverlappedResult(mc->hPipeR, &mc->read_event, &readn, FALSE) == FALSE);
					}

					pos += readn;
				}

				data[msg_hdr.length] = 0;//'\0';
#ifdef _UNICODE
				data[msg_hdr.length+1] = 0;//'\0';
#endif

				old_path = (TCHAR *)data;
				if(msg_hdr.type == MDP_CTRL_MSG_RENAME_DIR || msg_hdr.type == MDP_CTRL_MSG_RENAME_FILE)
				{
					new_path = _tcsstr(old_path, _T("|"));

					if(new_path == NULL){
						LOG_WARN(LOG_MOD_MDP_CNTRL, "Wrong Rename Info");
						continue;
					}

					*new_path = 0;//'\0';
					new_path += 1;

					ret = dir_check(mc, new_path);
					if(ret != INSIDE_INTEREST_DIR){
						msg_hdr.type += 2;
						msg_hdr.length = _tcslen(old_path) * sizeof(TCHAR);
					}
				}

				EnterCriticalSection(&mc->rename_event_cs);

				type = 0;
				if(msg_hdr.type < MDP_CTRL_MSG_DELETE_DIR)
					type |= _RENAME_TBL_RENAME;
				else
					type |= _RENAME_TBL_REMOVE;

				if(msg_hdr.type%2 == 0)
					type |= _RENAME_TBL_DIR;
				else
					type |= _RENAME_TBL_FILE;

				if(type & _RENAME_TBL_RENAME)
					_rename_tbl_mergedInfoAdd(0, type, old_path, new_path, _tcslen(old_path), _tcslen(new_path));
				else
					_rename_tbl_mergedInfoAdd(0, type, old_path, NULL, _tcslen(old_path), 0);

				LeaveCriticalSection(&mc->rename_event_cs);

				if(type & _RENAME_TBL_RENAME){
					LOG_INFO_W(LOG_MOD_MDP_CNTRL, _T("RENAME %s \n\t\t\t\t-> %s"), old_path, new_path);
				}else{
					LOG_INFO_W(LOG_MOD_MDP_CNTRL, _T("DELETE %s(%d/%d)"), old_path, msg_hdr.length, _tcslen(old_path));
				}
			}else{ // Fail Info
				// use seq for channel ID
				node_failover_process(msg_hdr.type, msg_hdr.seq);
			}
		}
	}

out_mdp_ctrl_recv:
	LOG_INFO(LOG_MOD_MDP_CNTRL, "mDP Control Listener Thread Exit");
	SetEvent(mc->destroy_recver_event);
	return 0;

out_error_mdp_ctrl_recv:
	g_stopFlag = 0xffffff;
	SetEvent(mc->destroy_recver_event);

	EnterCriticalSection(&mc->queue_cs);
	if(mc->nr_queue > 0 && !list_empty(&mc->queue_list)){
		entry = list_first_entry(&mc->queue_list, mdp_ctrl_queue_t, lnode);
		entry->rtv = -1; 
		SetEvent(entry->mdp_event);
	}
	LeaveCriticalSection(&mc->queue_cs);

	return -1;
}

int init_mdp_history(mdp_controller_t *mc)
{
	node_t *node = mc->node;
	mdp_ctrl_msg_hdr_t msg_hdr;
	mdp_ctrl_queue_t *entry;
	char data[1024];
	int ret, readn;

	TCHAR *old_path, *new_path;
	uint32 old_key, new_key;
	hash_entry_t *hash_entry;
	hash_entry_t *old_entry, *new_entry;

	if(!mc)
		return 0;

	/* if exist, Read and add hash */
	sprintf(mc->history, "%s\\rename_history", INITSYNC_HOME_PATH);

	if(!node->resume){
		ret = DeleteFileA(mc->history);
	}

	mc->hHistory = CreateFileA(mc->history, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(mc->hHistory !=  INVALID_HANDLE_VALUE){	
		while(1){
			ret = ReadFile(mc->hHistory, &msg_hdr, sizeof(mdp_ctrl_msg_hdr_t), &readn, NULL);
			if((!ret) || readn == 0)
			{
				return 0;
			}

			ret = ReadFile(mc->hHistory, data, msg_hdr.length, &readn, NULL);
			if((!ret) || readn == 0)
			{
				return 0;
			}

			data[msg_hdr.length] = 0;//'\0';
#ifdef _UNICODE
			data[msg_hdr.length+1] = 0;//'\0';
#endif

			old_path = (TCHAR *)data;
			if(msg_hdr.type == MDP_CTRL_MSG_RENAME_DIR || msg_hdr.type == MDP_CTRL_MSG_RENAME_FILE)
			{
				new_path = _tcsstr(old_path, _T("|"));

				if(new_path == NULL){
					LOG_WARN(LOG_MOD_MDP_CNTRL, "Wrong Rename Info");
					continue;
				}

				*new_path = 0;//'\0';
				new_path += 1;
			}

			old_entry = malloc(sizeof(hash_entry_t));
			memset(old_entry, 0, sizeof(hash_entry_t));

			old_entry->key = 0;//old_key;
			old_entry->data = malloc((_tcslen(old_path)+1) * sizeof(TCHAR));
			memcpy(old_entry->data, old_path, (_tcslen(old_path)+1) * sizeof(TCHAR));
			old_entry->datalen = (_tcslen(old_path)+1) * sizeof(TCHAR);

			old_entry->opposite = NULL;

			old_entry->key = mc->nr_rename_event;

			if(msg_hdr.type == MDP_CTRL_MSG_RENAME_DIR || msg_hdr.type == MDP_CTRL_MSG_RENAME_FILE){
				new_entry = malloc(sizeof(hash_entry_t));
				memset(new_entry, 0, sizeof(hash_entry_t));

				new_entry->key = old_entry->key;//new_key;
				new_entry->data = malloc((_tcslen(new_path)+1) * sizeof(TCHAR));
				memcpy(new_entry->data, new_path, (_tcslen(new_path)+1) * sizeof(TCHAR));

				new_entry->datalen = (_tcslen(new_path)+1) * sizeof(TCHAR);

				old_entry->opposite = new_entry;
				new_entry->opposite = old_entry;

			}

			EnterCriticalSection(&mc->rename_event_cs);

			if(msg_hdr.type == MDP_CTRL_MSG_DELETE_DIR){
				mdp_ctrl_delete_dir(mc, old_entry->data);
			}

			list_add_tail(&old_entry->lnode, &mc->rename_event_list);
			mc->nr_rename_event++;

			LeaveCriticalSection(&mc->rename_event_cs);
		}
	}else{
		/* Create File */
		mc->hHistory = CreateFileA(mc->history, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if(mc->hHistory == INVALID_HANDLE_VALUE)
			return -1;
	}
	return 0;
}

mdp_controller_t *init_mdp_controller(void *n)
{
	mdp_controller_t *mc;
	mdp_ctrl_queue_t *mcq;
	node_t *node = n;
	file_manager_t *fm = node->file_manager;
	file_list_t *fl;
	dir_t *dir;
	list_node_t *fl_pos, *fl_nxt;
	list_node_t *dir_pos, *dir_nxt;
	int i, ret;

	mc = malloc(sizeof(mdp_controller_t));
	if(!mc){
		LOG_ERROR(LOG_MOD_MDP_CNTRL, "out of memory %s:%d", __FILE__, __LINE__);
		return NULL;
	}

	memset(mc, 0, sizeof(mdp_controller_t));

	mc->node = node;

	/* wait queue init */
	init_list_node(&mc->init_list);
	init_list_node(&mc->queue_list);

	for(i = 0; i < MAX_CTRL_MSG; i++){
		mcq = malloc(sizeof(mdp_ctrl_queue_t));
		if(!mcq){
			LOG_ERROR(LOG_MOD_MDP_CNTRL, "out of memory %s:%d", __FILE__, __LINE__);
			return NULL;
		}

		memset(mcq, 0, sizeof(mdp_ctrl_queue_t));
		mcq->mdp_event = CreateEvent(NULL, FALSE, FALSE, NULL);
		if(mcq->mdp_event == INVALID_HANDLE_VALUE)
		{
			LOG_ERROR(LOG_MOD_MDP_CNTRL, "CeateEvent Fail, error code : %d", GetLastError());
			return NULL;
		}

		list_add_tail(&mcq->lnode, &mc->init_list);
		mc->nr_init++;
		mc->nr_total++;
	}

	/* init critical section */
	InitializeCriticalSection(&mc->pipe_cs);
	InitializeCriticalSection(&mc->queue_cs);
	InitializeCriticalSection(&mc->rename_event_cs);

	mc->hPipeR = INVALID_HANDLE_VALUE;
	mc->hPipeW = INVALID_HANDLE_VALUE;

	ret = init_mdp_connection(mc, node->index);
	if(ret < 0)
	{
		LOG_ERROR(LOG_MOD_MDP_CNTRL, "create mdp Pipe fail %s:%d", __FILE__, __LINE__);
		return NULL;
	}

	_rename_tbl_Init();

	/* create recv thread */
	mc->destroy_recver_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!mc->destroy_recver_event)
	{
		LOG_ERROR(LOG_MOD_MDP_CNTRL, "event create fail %s:%d", __FILE__, __LINE__);
		return NULL;
	}

	mc->mdp_ctrl_recver = (HANDLE)_beginthreadex( 
		NULL, 0, mdp_ctrl_recv_thread, mc, 0, &(mc->mdp_ctrl_recver_id));
	if(mc->mdp_ctrl_recver == 0)
	{
		LOG_ERROR(LOG_MOD_MDP_CNTRL, "mDP ctrl recv thread create fail, error code : %d", errno);
		return NULL;
	}

	/* add dir */
	init_list_node(&mc->dir_list);
	mc->nr_dir = 0;

	list_for_each_safe(fl_pos, fl_nxt, &fm->init_list)
	{
		fl = list_entry(fl_pos, file_list_t, lnode);
		if(fl){
			list_for_each_safe(dir_pos, dir_nxt, &fl->dir_list)
			{
				dir = list_entry(dir_pos, dir_t, lnode);
				if(dir){
					add_dir(mc, dir->name, dir->length, dir->depth);
				}
			}
		}
	}

	return mc;
}

int mdp_ctrl(mdp_controller_t *mc, mdp_ctrl_type_t type, int host_index, int len, char *data)
{
	mdp_ctrl_msg_hdr_t msg_hdr;
	mdp_ctrl_queue_t *entry;
	int ret, mdp_returnValue;
	int written;

	if(g_alone)
		return 0;

	if(!mc)
		return -1;

	msg_hdr.type = type;
	msg_hdr.host_index = host_index;
	msg_hdr.length = len;

	/* Pipe Critical Section for Write ctrl msg*/
	EnterCriticalSection(&mc->pipe_cs);

	msg_hdr.seq = mc->seq;
	mc->seq = (mc->seq+1)%MAX_CTRL_MSG;

	/* Queue Critical Section for push Q */
	EnterCriticalSection(&mc->queue_cs);

	ret = WriteFile(mc->hPipeW, &msg_hdr, sizeof(mdp_ctrl_msg_hdr_t), &written, &mc->send_event);
	if(!ret)
	{
		do{
			int err = GetLastError();

			if(err != ERROR_IO_PENDING && err != ERROR_IO_INCOMPLETE){
				/* error */
				LOG_ERROR(LOG_MOD_MDP_CNTRL, "Write Pipe Fail, error code : %d", GetLastError());

				LeaveCriticalSection(&mc->queue_cs);
				LeaveCriticalSection(&mc->pipe_cs);
				return -1;
			}

			WaitForSingleObject(mc->send_event.hEvent, 1000);

		}while(GetOverlappedResult(mc->hPipeW, &mc->send_event, &written, FALSE) == FALSE);
		
		if(written != sizeof(mdp_ctrl_msg_hdr_t))
		{
			/* error */
			LOG_ERROR(LOG_MOD_MDP_CNTRL, "Write Pipe Fail, error code : %d", GetLastError());

			LeaveCriticalSection(&mc->queue_cs);
			LeaveCriticalSection(&mc->pipe_cs);
			return -1;
		}
	}

	if(data)
	{
		ret = WriteFile(mc->hPipeW, data, len, &written, &mc->send_event);
		if(!ret)
		{
			do{
				int err = GetLastError();

				if(err != ERROR_IO_PENDING && err != ERROR_IO_INCOMPLETE){
					/* error */
					LOG_ERROR(LOG_MOD_MDP_CNTRL, "Write Pipe Fail, error code : %d", GetLastError());

					LeaveCriticalSection(&mc->queue_cs);
					LeaveCriticalSection(&mc->pipe_cs);
					return -1;
				}

				WaitForSingleObject(mc->send_event.hEvent, 1000);

			}while(GetOverlappedResult(mc->hPipeW, &mc->send_event, &written, FALSE) == FALSE);
			if(written != len)
			{
				/* error */
				LOG_ERROR(LOG_MOD_MDP_CNTRL, "Write Pipe Fail, error code : %d", GetLastError());

				LeaveCriticalSection(&mc->queue_cs);
				LeaveCriticalSection(&mc->pipe_cs);
				return -1;
			}
		}
	}

	if(NEED_ACK_MSG(type)){
		if(mc->nr_init > 0 && !list_empty(&mc->init_list)){
			entry = list_first_entry(&mc->init_list, mdp_ctrl_queue_t, lnode);
			entry->seq = msg_hdr.seq;
			entry->type = msg_hdr.type;
			entry->rtv = 0;

			list_move_tail(&entry->lnode, &mc->queue_list);
			mc->nr_init--;
			mc->nr_queue++;
		}else{
			/* error */
			LOG_ERROR(LOG_MOD_MDP_CNTRL, "mDP Control Queue is empty");

			LeaveCriticalSection(&mc->queue_cs);
			LeaveCriticalSection(&mc->pipe_cs);
			return -1;
		}
		LOG_DEBUG(LOG_MOD_MDP_CNTRL, "mDP Control MSG(%d, %d) Queue, Number of Queue : %d", entry->type, entry->seq, mc->nr_queue);
	}

	LeaveCriticalSection(&mc->queue_cs);

	if(NEED_ACK_MSG(type)){
		/* wait ack */
		LOG_DEBUG(LOG_MOD_MDP_CNTRL, "Wait mDP Control MSG[%d:%d] Ack", type, msg_hdr.seq);
		ret = WaitForSingleObject(entry->mdp_event, /*MDP_CTRL_TIME_OUT*/ INFINITE);
		if(ret == WAIT_FAILED){		
			LOG_ERROR(LOG_MOD_MDP_CNTRL, "mDP wait fail");		
			return -1;
		}else if(ret == WAIT_ABANDONED){			
			LOG_ERROR(LOG_MOD_MDP_CNTRL, "mDP wait abandoned");	
			return -1;
		}else if(ret == WAIT_TIMEOUT){			
			LOG_ERROR(LOG_MOD_MDP_CNTRL, "mDP wait time out");		
			return -1;
		}

		mdp_returnValue = entry->rtv;

		LOG_DEBUG(LOG_MOD_MDP_CNTRL, "Recv mDP Control MSG[%d:%d] Ack", type, msg_hdr.seq);
		EnterCriticalSection(&mc->queue_cs);

		ResetEvent(entry->mdp_event);
		entry->type = -1;
		entry->seq = -1;
		list_move_tail(&entry->lnode, &mc->init_list);
		mc->nr_init++;
		mc->nr_queue--;

		LeaveCriticalSection(&mc->queue_cs);
	}

	LeaveCriticalSection(&mc->pipe_cs);


	return mdp_returnValue;
}

int tcsstr0(TCHAR *str1, TCHAR *str2)
{
	int len = _tcslen(str2);
	TCHAR tc;
	int ret;

	if(len > _tcslen(str1))
		return -1;

	tc = str1[len];
	str1[len] = 0;
	
	ret = _tcsicmp(str1, str2);

	str1[len] = tc;

	return ret;
}

#if _ALL_RENAME_HISTORY_
int get_new_path(mdp_controller_t *mc, TCHAR *old_path, TCHAR *new_path)
{
	hash_entry_t *entry;
	list_node_t *pos, *nxt;
	uint32 old_key;
	int len, i;

	len = 0;

#ifdef _USE_HASH_
	/* if table is empty, fast return */
	if(mc->old_path_tbl->nr_entry == 0)
		return 0;

	old_key = get_checksum((char *)old_path, _tcslen(old_path) * sizeof(TCHAR));

	entry = find_hashtable(mc->old_path_tbl, old_key);
	if(entry){
		hash_entry_t *new_entry = entry->opposite;

		if(new_entry){
			memcpy(new_path, new_entry->data, new_entry->datalen);
			new_path[new_entry->datalen / sizeof(TCHAR)] = 0;//'\0';
			return new_entry->datalen / sizeof(TCHAR);
		}else{
			return -1; // delete
		}
	}else{
		TCHAR *cur, *parent;

		/* path를 parent와 cur 로 나눈다. 나누어 지지 않는다면 즉 최상위까지 다 찾았다면 return 0 */
		parent = old_path;
		for(i = _tcslen(old_path)-1; i > 0; i--){
			if(parent[i] == '\\')
			{
				parent[i] = 0; //'\0';
				cur = parent + i + 1;
				break;
			}
		}
		if(i == 0){
			return 0;
		}

		len = get_new_path(mc, parent, new_path);
		parent[i] = '\\';
		if(len == 0){
			return 0;
		}else if(len > 0){
			//sprintf(new_path + len, "\\%s", cur);
			_stprintf(new_path + len, _T("\\%s"), cur);
			return len + 1 + _tcslen(cur);
		}else{
			return -1; // delete
		}
	}
#else

	if(!mc)
		return 0;

	if(mc->nr_rename_event == 0)
		return 0;

	EnterCriticalSection(&mc->rename_event_cs);

	list_for_each_safe(pos, nxt, &mc->rename_event_list)
	{
		TCHAR *p;

		entry = list_entry(pos, hash_entry_t, lnode);
		if(entry){
#if 0
			p = _tcsstr(old_path, (TCHAR *)entry->data);
			_tcsncmp(old_path, (TCHAR *)entry->data);
			if(p == NULL || p != old_path){
				continue;
			}else{
				p += entry->datalen / sizeof(TCHAR) - 1;
				if(*p == '\\' || *p == 0){ /* Hit */
					hash_entry_t *opposite = entry->opposite;
					if(opposite == NULL){ // delete event
						LeaveCriticalSection(&mc->rename_event_cs);
						return -1;
					}

					memcpy(new_path, opposite->data, opposite->datalen);
					_tcscpy(new_path + opposite->datalen/sizeof(TCHAR)-1, p);

					_tcscpy(old_path, new_path);

					len = 1;//_tcslen(new_path);
				}
			}
#endif
			if(_tcslen(old_path) < entry->datalen / sizeof(TCHAR) - 1)
				continue;

			p = old_path;
			if(!_tcsncmp(old_path, (TCHAR *)entry->data, entry->datalen / sizeof(TCHAR) - 1)){
				p += entry->datalen / sizeof(TCHAR) - 1;
				if(*p == '\\' || *p == 0){ /* Hit */
					hash_entry_t *opposite = entry->opposite;
					if(opposite == NULL){ // delete event
						LeaveCriticalSection(&mc->rename_event_cs);
						return -1;
					}

					memcpy(new_path, opposite->data, opposite->datalen);
					_tcscpy(new_path + opposite->datalen/sizeof(TCHAR)-1, p);

					_tcscpy(old_path, new_path);

					len = 1;//_tcslen(new_path);
				}
			}else{
				continue;
			}
		}
	}

	LeaveCriticalSection(&mc->rename_event_cs);

	return len;
#endif
}

#else
int get_new_path(mdp_controller_t *mc, int is_dir, TCHAR *old_path, TCHAR **new_path)
{
	int rtv;
	int type;

	EnterCriticalSection(&mc->rename_event_cs);

	if(is_dir)
		rtv = _rename_tbl_mergedInfoCheck(0, _RENAME_TBL_DIR, old_path, _tcslen(old_path), new_path);
	else
		rtv = _rename_tbl_mergedInfoCheck(0, _RENAME_TBL_FILE, old_path, _tcslen(old_path), new_path);
	
	LeaveCriticalSection(&mc->rename_event_cs);

	return rtv;
}

#endif

int add_dir(mdp_controller_t *mc, TCHAR *name, int length, int depth)
{
	dir_t *dir;
	int ret;

	dir = malloc(sizeof(dir_t));
	if(!dir){
		LOG_ERROR(LOG_MOD_MDP_CNTRL, "out of memory %s:%d", __FILE__, __LINE__);
		return -1;
	}

	dir->name = (TCHAR *)malloc(sizeof(TCHAR) * (_tcslen(name) + 1));
	if(!dir->name){
		LOG_ERROR(LOG_MOD_MDP_CNTRL, "out of memory %s:%d", __FILE__, __LINE__);
		SAFE_FREE(dir);
		return -1;
	}
	ret = _tcsncpy_s(dir->name, _tcslen(name) + 1, name, _TRUNCATE);
	if(ret != 0){
		LOG_ERROR(LOG_MOD_MDP_CNTRL, "strcpy %s:%d", __FILE__, __LINE__);
		SAFE_FREE(dir->name);
		SAFE_FREE(dir);
		return -1;
	}

	dir->length = length;
	dir->depth = depth;

	list_add_tail(&dir->lnode, &mc->dir_list);
	mc->nr_dir++;

	return 0;
}

void destroy_mdp_controller(mdp_controller_t *mc)
{
	int ret; 

	LOG_INFO(LOG_MOD_MAIN, "+- Destoroy mDP Controller -+");

	mc->destroy_flag = 1;
	SetEvent(mc->read_event.hEvent);

	ret = WaitForSingleObject(mc->destroy_recver_event, 1000);
	if(ret == WAIT_TIMEOUT){
		// if timeout
		TerminateThread(mc->mdp_ctrl_recver, -1);
	}
	
	SAFE_CLOSE_HANDLE(mc->read_event.hEvent);
	SAFE_CLOSE_HANDLE(mc->destroy_recver_event);
	SAFE_CLOSE_HANDLE(mc->hPipeR);
	SAFE_CLOSE_HANDLE(mc->hPipeW);

//	CloseHandle(mc->hHistory);
//	DeleteFileA(mc->history);

	/* destroy resource */
}

int mdp_ctrl_set_segment(mdp_controller_t *mc, int host_index, TCHAR *fkey, TCHAR *lkey, int seg_id, int exception)
{
	char data[2048] = {0};
	char *pos;
	int len;

	pos = data;

	memcpy(pos, &seg_id, sizeof(int));
	pos += sizeof(int);

	memcpy(pos, &exception, sizeof(int));
	pos += sizeof(int);

	len = (_tcslen(fkey)+1)*sizeof(TCHAR);
	memcpy(pos, &len, sizeof(int));
	pos += sizeof(int);

	memcpy(pos, fkey, len);
	pos += len;

	len = (_tcslen(lkey)+1)*sizeof(TCHAR);
	memcpy(pos, &len, sizeof(int));
	pos += sizeof(int);

	memcpy(pos, lkey, len);
	pos += len;

	len = pos - data;

	return mdp_ctrl(mc, MDP_CTRL_MSG_REGIST_SEGMENT, host_index, len, data);
}

int mdp_ctrl_set_cur_segment(mdp_controller_t *mc, int host_index, int seg_id)
{
	return mdp_ctrl(mc, MDP_CTRL_MSG_REGIST_CUR_SEGMENT, host_index, sizeof(int), (char *)&seg_id);
}

int mdp_ctrl_regist_initsync_pid(mdp_controller_t *mc)
{
	DWORD pid;

	pid = GetCurrentProcessId();

	return mdp_ctrl(mc, MDP_CTRL_MSG_REGIST_INITSYNC_PID, -1, sizeof(DWORD), (char *)&pid);
}

int mdp_ctrl_pause(mdp_controller_t *mc, int host_index, int seg_id, TCHAR *last_path)
{
	char data[2048] = {0};
	char *pos;
	int len;

	if(!mc)
		return 0;

	pos = data;

	memcpy(pos, &seg_id, sizeof(int));
	pos += sizeof(int);

	if(last_path && _tcslen(last_path) > 0){
		len = (_tcslen(last_path)+2)*sizeof(TCHAR);
		memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);

		memcpy(pos, last_path, len-(2*sizeof(TCHAR)));
		pos += (len-(2*sizeof(TCHAR)));

		*pos = 1;
		pos += (2*sizeof(TCHAR));
	}else{
		len = 0;
		memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
	}

	len = pos - data;

	return mdp_ctrl(mc, MDP_CTRL_MSG_PAUSE, host_index, len, data);
}


int mdp_ctrl_stop(mdp_controller_t *mc, int host_index, int seg_id, TCHAR *last_path, int stop_flag)
{
	char data[2048] = {0};
	char *pos;
	int len;

	if(!mc)
		return 0;

	pos = data;

	memcpy(pos, &stop_flag, sizeof(int));
	pos += sizeof(int);

	memcpy(pos, &seg_id, sizeof(int));
	pos += sizeof(int);

	if(last_path && _tcslen(last_path) > 0){
		len = (_tcslen(last_path)+2)*sizeof(TCHAR);
		memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);

		memcpy(pos, last_path, len-(2*sizeof(TCHAR)));
		pos += (len-(2*sizeof(TCHAR)));

		*pos = 1;
		pos += (2*sizeof(TCHAR));
	}else{
		len = 0;
		memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
	}

	len = pos - data;

	return mdp_ctrl(mc, MDP_CTRL_MSG_STOP, host_index, len, data);
}


int mdp_ctrl_get_cur_segment(mdp_controller_t *mc, int host_index)
{
	return mdp_ctrl(mc, MDP_CTRL_MSG_GET_CUR_SEGMENT, host_index, 0, NULL);
}

int mdp_ctrl_resume(mdp_controller_t *mc, int host_index)
{
	return mdp_ctrl(mc, MDP_CTRL_MSG_RESUME, host_index, 0, NULL);
}