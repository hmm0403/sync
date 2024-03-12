#include "session.h"
#include "session_manager.h"
#include "session_handler_manager.h"
#include "file_manager.h"
#include "file_list.h"
#include "node.h"
#include "NetworkEventHandler.h"
#include "network_api.h"
#include "report.h"
#pragma comment(lib, "mswsock.lib")
#include "wchar_conv.h"
#include "msg.h"
#include "initsync_time.h"
#include "queue.h"
#include <errno.h>
#include "mdp_controller.h"
#include "sd_notifier.h"
#include "sd_cmd_cntl.h"
#include "defines.h"
#include "mdp_rename_table.h"
#include "privilege.h"

extern node_t *g_node;
extern int g_stopFlag;
extern int g_nodeChannelInfo;
extern int g_alone;
//extern int g_pauseFlag;

extern int g_file_buf_size;
extern int g_send_buf_size;
extern int g_recv_buf_size;
extern int g_sock_buf_size;

int session_fail_process(session_t *s);
int session_error_process(session_t *s, int err);

int add_not_acked_file(session_t *s, char *name, int len)
{
	ack_list_t *al = s->ack_list;
	not_acked_file_t *nf;
	int ret;

	EnterCriticalSection(&al->cs);

	if(list_empty(&al->free_list)){
		LOG_ERROR(LOG_MOD_SESSION, "no free not-acked file list");
		return -1;
	}

	nf = list_first_entry(&al->free_list, not_acked_file_t, lnode);
	if(!nf){
		LOG_ERROR(LOG_MOD_SESSION, "Memory broken %s:%d", __FILE__, __LINE__);
		return -1;
	}

	nf->len = len;
	nf->filename = malloc(len+1);
	if(!nf->filename){
		LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		return -1;
	}
	ret = strncpy_s(nf->filename, len+1, name, _TRUNCATE);
	if(ret != 0){
		LOG_ERROR(LOG_MOD_SESSION, "strcpy fail %s:%d", __FILE__, __LINE__);
		return -1;
	}

	list_move_tail(&nf->lnode, &al->ack_list);
	al->nr_free--;
	al->nr_ack++;

	LeaveCriticalSection(&al->cs);

	return 0;
}

char *delete_first_not_acked_file(session_t *s)
{
	ack_list_t *al = s->ack_list;
	not_acked_file_t *nf;
	char *tmp = NULL;

	EnterCriticalSection(&al->cs);

	if(list_empty(&al->ack_list)){
		LOG_ERROR(LOG_MOD_SESSION, "no not-acked file list");
		return NULL;
	}

	nf = list_first_entry(&al->ack_list, not_acked_file_t, lnode);
	if(!nf){
		LOG_ERROR(LOG_MOD_SESSION, "Memory broken %s:%d", __FILE__, __LINE__);
		return NULL;
	}

	nf->len = 0;
	tmp = nf->filename;
	if(tmp == NULL){
		printf("binggo\n");
	}
	nf->filename = NULL;
	
	list_move_tail(&nf->lnode, &al->free_list);
	al->nr_free++;
	al->nr_ack--;

	LeaveCriticalSection(&al->cs);

	return tmp;
}

int session_failover(session_t *s, int thread_id)
{
	int ret;
	SOCKET sock;
	
	LOG_DEBUG(LOG_MOD_SESSION, "Session FailOver");

	if(s->failover == SESSION_CRASH){
		LOG_DEBUG(LOG_MOD_SESSION, "Session Crashed : TERM");
		return -1;
	}

	if(s->state == SESSION_STATE_TERM){
		LOG_DEBUG(LOG_MOD_SESSION, "Session Connect Close - Session State : TERM");
		return -1;
	}

	if((g_node->mode == INITSYNC_MODE_TARGET || s->local_rep_mode == INITSYNC_MODE_TARGET) && 
		(s->run_state == SESSION_RUN_STATE_STOP 
		|| s->run_state == SESSION_RUN_STATE_FAIL
		|| s->run_state == SESSION_RUN_STATE_PAUSE 
		|| s->run_state == SESSION_RUN_STATE_COMPLETE)){

		LOG_DEBUG(LOG_MOD_SESSION, "Session Connect Close - Session State : Waiting Remote FIN");
		return -1;
	}

	if(g_node->mode != INITSYNC_MODE_LOCAL_REPLICATION){
		g_node->status = NODE_STATUS_FAILOVER;

		if(thread_id == s->recv_thread_id){ // recv thread
			
			s->failover = SESSION_FAILOVER;

			queue_pop_wakeup(s->send_Q);

			WaitForSingleObject(s->send_failover_event, INFINITE);

			ret = get_failover_channel(INFINITE);
			if(ret< 0){
				LOG_DEBUG(LOG_MOD_SESSION, "ALL Session Expired. Session TERM");
				s->run_state = SESSION_RUN_STATE_FAIL;
				SetEvent(s->recv_failover_event);
				return -1;
			}
			LOG_DEBUG(LOG_MOD_MAIN, "FailOver Channel Set %d", ret);

			if(g_node->mode == INITSYNC_MODE_TARGET){
				LOG_DEBUG(LOG_MOD_SESSION, "Session Try Failover ReConnect");
				Sleep(2000);
				session_connect_failover(s);
			}

			sock = get_failover_connect(INFINITE);
			if(sock == SOCKET_ERROR){
				LOG_DEBUG(LOG_MOD_SESSION, "FailOver Connect Fail. Session TERM");
				s->run_state = SESSION_RUN_STATE_FAIL;
				SetEvent(s->recv_failover_event);
				return -1;
			}

			s->sock = sock;

			SetEvent(s->recv_failover_event);
		}else{ // send thread
			SetEvent(s->send_failover_event);

			WaitForSingleObject(s->recv_failover_event, INFINITE);

			s->failover = 0;

			if(s->sock != INVALID_SOCKET){
				set_failover_complete();
			}else{
				return -1;
			}
		}

		return 0;
	}

	return -1;
}

void session_crash(session_t *s)
{
	session_manager_t *sm = s->sm;
	node_t *node = sm->node;

#ifdef _USE_DISPACHER_TH_
	send_unregist((dispatcher_t *)node->dis, s->sock);
	recv_unregist((dispatcher_t *)node->dis, s->sock);
#endif

	s->failover = SESSION_CRASH;

	if(s->sock != INVALID_SOCKET)
	{
		net_closesocket(s->sock);
		s->sock = INVALID_SOCKET;
	}

	flush_queue(s->recv_Q, s->buf_pool, free_buf);
	flush_queue(s->send_Q, s->buf_pool, free_buf);

	//exit(0);
	//destroy_session(s);
}


int session_remote_node_fail_process(session_t *s)
{
	LOG_INFO(LOG_MOD_SESSION, "Initsync FailOver - TERM mDP");

	if(g_node->mode == INITSYNC_MODE_SOURCE){
		mdp_ctrl_stop(g_node->mdp_controller, s->remote_id, -1, NULL, 1);
	}else if(g_node->mode == INITSYNC_MODE_TARGET){
		if(s->hCurFile != INVALID_HANDLE_VALUE){
			SAFE_CLOSE_HANDLE(s->hCurFile);

			if(s->cur_recv_path){
				if(s->local_rep_mode == INITSYNC_MODE_TARGET){
					DeleteFile(s->cur_lr_recv_path);
				}else{
					DeleteFile(s->cur_recv_path);
				}
				SAFE_FREE(s->cur_lr_recv_path);
				SAFE_FREE(s->cur_recv_path);
			}
		}

		segment_mdp_stop((file_list_t *)s->flist, g_node->mdp_controller, 1);
	}else{ // local replication
		// some thing wrong
	}

	s->run_state = SESSION_RUN_STATE_FAIL;

	/* session을 close 한다 */
	s->state = SESSION_STATE_TERM;

	session_crash(s);
	// NOTIFY FailOver

	return 0;
}

int session_send(session_t *s, char *msg, int len)//, entry_data_type_t type)
{
	queue_t *send_Q;
	session_manager_t *sm = s->sm;
	node_t *node = sm->node;


	if(s->local_rep_mode == INITSYNC_MODE_UNKNOWN && s->sock == INVALID_SOCKET)
		return -1;

	send_Q = s->send_Q;

	return pushQ(send_Q, msg, len);//, type);
}

char *session_recv(session_t *s, int *len, int blocking)
{
	queue_t *recv_Q;

	if(s->local_rep_mode == INITSYNC_MODE_UNKNOWN && s->sock == INVALID_SOCKET)
		return NULL;

	recv_Q = s->recv_Q;

	return popQ(recv_Q, len, blocking);
}

int send_cmd(session_t *s, int fid, int *err)
{
	PSESSION_CMD_HDR_T hdr;
	char *buf;
	char *pos;
	int buf_size, len = 0;

	buf = alloc_buf(s->buf_pool, &buf_size);
	if(!buf){
	}

	pos = buf;

	hdr = (PSESSION_CMD_HDR_T)buf;
	pos += SIZEOF_SESSION_CMD_HDR;

	hdr->type = fid;
	hdr->length = 0;

	switch(fid){

	case INITSYNC_CMD_CHECK_MODE:
		{
			PSESSION_CHECK_MODE_HDR_T cmd_hdr = (PSESSION_CHECK_MODE_HDR_T)pos;
			pos += SIZEOF_SESSION_CHECK_MODE_HDR;

			if(s->resume)
				cmd_hdr->mode = INITSYNC_CNTL_RESUME;
			else
				cmd_hdr->mode = INITSYNC_CNTL_NORMAL;

			hdr->length = SIZEOF_SESSION_CHECK_MODE_HDR;
		}
		break;
	case INITSYNC_CMD_FILE_ACK:
		{
			PSESSION_FILE_ACK_HDR_T cmd_hdr = (PSESSION_FILE_ACK_HDR_T)pos;
			pos += SIZEOF_SESSION_FILE_ACK_HDR;

			cmd_hdr->ack_offset = s->ack_offset;
			cmd_hdr->ack_idx = s->ack_idx;
			cmd_hdr->file_size = s->cur_file_size;
			hdr->length = SIZEOF_SESSION_FILE_ACK_HDR;
		}
		break;
	case INITSYNC_CMD_REQ_AFL:
	case INITSYNC_CMD_FILE_END:
	case INITSYNC_CMD_COMPLETE:
	case INITSYNC_CMD_PAUSE:
	case INITSYNC_CMD_STOP:
	case INITSYNC_CMD_FAIL:
		/* do nothing */
		break;
	default:
		/* error */
		break;
	}

	len = (int)(pos - buf);
	session_send(s, buf, len);

	return 0;
}

int send_file(session_t *s, PMFL_FILE_T cur, TCHAR* new_path, uint64 offset, uint64 idx, int *err)
{
	file_list_t *fl = (file_list_t *)s->flist;

	PSESSION_CMD_HDR_T cmd_hdr;
	PSESSION_FILE_HDR_T hdr;
	PSEND_FILE_HDR_T fhdr;
	PSEND_FILE_DATA_HDR_T fbd;

	char *pos, *secu_data, *buf;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	int ret, buf_size, seq, recovery_secu_flag;
	
	PMFL_FILE_HDR_T cur_mhdr = &cur->hdr;

	DWORD readn = 0;

	recovery_secu_flag = 0;

	if(!new_path)
		new_path = cur->path;

	seq = 0;

	buf = alloc_buf(s->buf_pool, &buf_size);
	pos = buf;

	cmd_hdr = (PSESSION_CMD_HDR_T)pos;
	pos += SIZEOF_SESSION_CMD_HDR;

	hdr = (PSESSION_FILE_HDR_T)pos;
	pos += SIZEOF_SESSION_FILE_HDR;

	fhdr = (PSEND_FILE_HDR_T)pos;
	pos += SIZEOF_SEND_FILE_HDR;

	/* set cmd hdr */
	cmd_hdr->type = INITSYNC_CMD_FILE;

	/* set file cmd hdr */
	hdr->type = SESSION_FILE_HDR_TYPE_HDR;
	hdr->seq = seq;
	seq++;
	hdr->ack_offset = offset;
	hdr->ack_idx = idx;

	/* set file hdr */
	fhdr->filesize = cur_mhdr->filesize;
	fhdr->path_size = cur_mhdr->path_size;

	/* get modified type */
	fhdr->modified_type = cur_mhdr->modified_type;

	if(fhdr->modified_type != MODIFIED_TYPE_TO){
		/* Get File Security */
		secu_data = MakeSetSecuInfo(new_path, &fhdr->secu_len, &fhdr->secu_info, err);
		if(!secu_data){
			/* error */
			LOG_ERROR_W(LOG_MOD_SESSION, _T("Make Security Information Fail, file name : %s, error code : %d"), cur->path, *err);
			goto error_out;
		}

		/* Get File Attribute */
		fhdr->File_attribute = cur_mhdr->File_attribute; //GetFileAttributes(new_path);

		if(fhdr->File_attribute & FILE_ATTRIBUTE_DIRECTORY){
			fhdr->filesize = 0;
		}else{
			//hFile = CreateFile(cur->path, GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
			while(hFile == INVALID_HANDLE_VALUE){
				hFile = CreateFile(new_path, GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
				if(hFile == INVALID_HANDLE_VALUE){
					*err = GetLastError();
					LOG_ERROR_W(LOG_MOD_SESSION, _T("file[%s] open fail, error code : %d"), cur->path, *err);
					
					if(recovery_secu_flag)
						goto error_out;

					if(*err == ERROR_ACCESS_DENIED){
						if(ReleaseSecurity(new_path) == ERROR_SUCCESS){
							recovery_secu_flag = 1;
							continue;
						}
					}
					goto error_out;
				}
			}

			/* get file size */
			fhdr->filesize = cur_mhdr->filesize;
		}
	}else{
		/* tgt only */
		fhdr->secu_len = 0;
		secu_data = NULL;

		/* Get File Attribute */
		fhdr->File_attribute = 0;

		fhdr->filesize = 0;
	}

	cmd_hdr->length  = SIZEOF_SESSION_FILE_HDR + SIZEOF_SEND_FILE_HDR + fhdr->path_size + fhdr->secu_len;
	if(cmd_hdr->length + SIZEOF_SESSION_CMD_HDR > buf_size)
	{
		/* error */
		goto error_out;
	}
	
	memcpy(pos, cur->path, fhdr->path_size);
	pos += fhdr->path_size;

	if(cur_mhdr->modified_type != MODIFIED_TYPE_TO){
		/* send security data */

		memcpy(pos, secu_data, fhdr->secu_len);
		pos+= fhdr->secu_len;
	}
	/* Send HDR */
	session_send(s, buf, (int)(pos-buf));
	buf = NULL;

	if(cur_mhdr->modified_type != MODIFIED_TYPE_TO){

		/* send file */
		do{
			DWORD dwBytesRead;

			if(!(cur_mhdr->File_attribute & FILE_ATTRIBUTE_DIRECTORY)){
				buf = alloc_buf(s->buf_pool, &buf_size);
				pos = buf;

				cmd_hdr = (PSESSION_CMD_HDR_T)pos;
				pos += SIZEOF_SESSION_CMD_HDR;

				hdr = (PSESSION_FILE_HDR_T)pos;
				pos += SIZEOF_SESSION_FILE_HDR;

				fbd = (PSEND_FILE_DATA_HDR_T)pos;
				pos += SIZEOF_SEND_FILE_DATA_HDR;

				/* set cmd hdr */
				cmd_hdr->type = INITSYNC_CMD_FILE;

				/* set file cmd hdr */
				hdr->type = SESSION_FILE_HDR_TYPE_BODY;
				hdr->seq = seq;
				seq++;
				hdr->ack_offset = offset;
				hdr->ack_idx = idx;

				dwBytesRead = buf_size - SIZEOF_SESSION_CMD_HDR - SIZEOF_SESSION_FILE_HDR - SIZEOF_SEND_FILE_HDR;

				if(g_stopFlag & (SD_CMD_PAUSE_IMMEDIATE | SD_CMD_STOP)){
					LOG_DEBUG_W(LOG_MOD_SESSION, _T("Stop Send file[%s]") ,cur->path);

					readn = IGNORE_FILE_LEN;
				}else{
					ret = ReadFile(hFile, pos, dwBytesRead, &readn, NULL);
					if(ret == 0){
						if(GetLastError() == ERROR_HANDLE_EOF)
						{
							break;
						}else{
							*err = GetLastError();
							LOG_ERROR_W(LOG_MOD_SESSION, _T("Read file[%s] fail, error code : %d") ,cur->path, *err);
							SAFE_CLOSE_HANDLE(hFile);

							goto error_out;
						}
					}
				}

				fbd->length = readn;

				if(readn == 0 || readn == IGNORE_FILE_LEN){
					cmd_hdr->length  = SIZEOF_SESSION_FILE_HDR + SIZEOF_SEND_FILE_DATA_HDR;
					
				}else{
					pos += readn;
					cmd_hdr->length  = SIZEOF_SESSION_FILE_HDR + SIZEOF_SEND_FILE_DATA_HDR + readn;
				}

				ret = session_send(s, buf, (int)(pos-buf));
				if(ret == SOCKET_ERROR){
					LOG_ERROR(LOG_MOD_SESSION, "Session send file fail to %s" ,s->remote_ip);
					SAFE_CLOSE_HANDLE(hFile);

					goto error_out;
				}

				if(readn != IGNORE_FILE_LEN)
					s->sent_bytes += readn;

				if(s->total_bytes > s->sent_bytes)
				{
					NOTIFY_PROGRESS(g_node->sd_notifier, s->remote_id, s->total_bytes, s->sent_bytes, 0);
				}else{
					NOTIFY_PROGRESS(g_node->sd_notifier, s->remote_id, s->total_bytes, s->total_bytes - 10, 0);
				}

				buf = NULL;
			}else{
				readn = 0;
			}

			if(readn == 0)
			{
				break;
			}

		}while(readn != 0 && readn != IGNORE_FILE_LEN);

		if(!(cur->hdr.File_attribute & FILE_ATTRIBUTE_DIRECTORY))
			SAFE_CLOSE_HANDLE(hFile);
	}

	if(g_stopFlag){
		LOG_INFO(LOG_MOD_SESSION, "Stop Send File");
		goto error_out;
	}

	if(recovery_secu_flag){
		SetSecurity(new_path, secu_data, fhdr->secu_len, fhdr->secu_info);
	}
	SAFE_FREE(secu_data);
	return 0;

error_out:
	SAFE_CLOSE_HANDLE(hFile);
	if(recovery_secu_flag){
		SetSecurity(new_path, secu_data, fhdr->secu_len, fhdr->secu_info);
	}
	SAFE_FREE(secu_data);

	if(buf)
		free_buf(s->buf_pool, buf);

	return -1;
}


int send_files(session_t *s)
{
	file_list_t *fl = s->flist;
	int ret;
	int fid = INITSYNC_CNTL_FILE;
	uint64 offset, nxt_offset;
	int readn;
	PMFL_FILE_T cur;
	LARGE_INTEGER loffset;
	session_manager_t *sm = s->sm;
	node_t *node = sm->node;
	int err = ERROR_SUCCESS;
	int tries = 3;

	fl->hMFL = CreateFileA(fl->modified_files, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if(fl->hMFL ==  INVALID_HANDLE_VALUE){
		LOG_ERROR(LOG_MOD_SESSION, "modified file list open fail, error code : %d", GetLastError());
		
		goto out_error;
	}

	fl->hHistory = CreateFileA(fl->history, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(fl->hHistory ==  INVALID_HANDLE_VALUE){
		err = GetLastError();
		fl->hHistory = CreateFileA(fl->history, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if(fl->hHistory ==  INVALID_HANDLE_VALUE){
			LOG_ERROR(LOG_MOD_SESSION, "history file open fail, error code : %d", GetLastError());
			
			goto out_error;
		}	
	}

	/* read resume position */
	if(s->resume){
		ret = ReadFile(fl->hHistory, &offset, sizeof(uint64), &readn, NULL);
		if((!ret) || (0 == readn)){
			offset = 0;
		}
	}else{
		offset = 0;
	}

	loffset.QuadPart = offset;
	SetFilePointerEx(fl->hMFL, loffset, NULL, FILE_BEGIN);
	cur = get_curr_file_entry(fl, fl->hMFL);
	if(s->resume){
		uint64 tmp;
		//fl->idx_file_entry = (int)pWrite[1];
		ret = ReadFile(fl->hHistory, &tmp, sizeof(uint64), &readn, NULL);
		if((!ret) || (0 == readn)){
			fl->idx_file_entry = 0;
		}else{
			fl->idx_file_entry = tmp;
		}
		cur = get_curr_file_entry(fl, fl->hMFL);

		ret = ReadFile(fl->hHistory, &s->sent_bytes, sizeof(uint64), &readn, NULL);
		if((!ret) || (0 == readn)){
			s->sent_bytes = 0;
		}

		ret = ReadFile(fl->hHistory, &s->total_bytes, sizeof(uint64), &readn, NULL);
		if((!ret) || (0 == readn)){
			s->total_bytes = 0;
		}
	}else{
		s->sent_bytes = 0;
	}

	nxt_offset = fl->file_entry_offset;

	SAFE_CLOSE_HANDLE(fl->hHistory);

	NOTIFY_PROGRESS(node->sd_notifier, s->remote_id, s->total_bytes, s->sent_bytes, 1);

	while(cur){
		TCHAR *new_path = NULL;
		TCHAR *old_path = NULL;

		if(g_stopFlag){
			goto out_error;
		}

		if(!g_alone){
			old_path = cur->path;

			if(cur->hdr.File_attribute & FILE_ATTRIBUTE_DIRECTORY)
				ret = get_new_path(node->mdp_controller, 1, old_path, &new_path);
			else
				ret = get_new_path(node->mdp_controller, 0, old_path, &new_path);

			if(ret < 0){
				/* error */
				LOG_ERROR(LOG_MOD_SESSION, "Get New Path Fail");
				SAFE_FREE(new_path);
				
				goto out_error;
			}

			if(ret == RENAME_TBL_DELETED){
				LOG_D_DEBUG_W(LOG_MOD_SESSION, _T("Path[%s] is deleted"), cur->path);
				cur = get_next_file_entry(fl, fl->hMFL);
				continue;
			}else if(ret == RENAME_TBL_EXIST){
				LOG_D_DEBUG_W(LOG_MOD_SESSION, _T("Path[%s] is renamed %s"), cur->path, new_path);
			}
		}

		/* offset update */
		if(nxt_offset < fl->file_entry_offset){
			offset = nxt_offset;
			nxt_offset = fl->file_entry_offset;
		}
		
		ret = send_file(s, cur, new_path, offset, fl->idx_file_entry, &err);
		if(ret == -1){
			if(err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND || err == ERROR_SHARING_VIOLATION){
				/* search path again */
				if(tries > 0){
					tries--;
					SAFE_FREE(new_path);
					continue;
				}else{
					if(err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND){
						LOG_ERROR_W(LOG_MOD_SESSION, _T("Can't find Path[%s]"), cur->path);
					}else{
						LOG_ERROR_W(LOG_MOD_SESSION, _T("Can't Open Path[%s]"), cur->path);
					}
					/* Notify Sync Fail File */
					NOTIFY_FAILED_FILE(g_node->sd_notifier, s->remote_id, err, cur->path);

					/* Write Sync Fail File List */
					flist_sync_fail(s->flist, cur->path, new_path, err);

					tries = 3;
					SAFE_FREE(new_path);
					cur = get_next_file_entry(fl, fl->hMFL);
					continue;
				}
			}

			SAFE_FREE(new_path);

			goto out_error;
		}

		tries = 3;
		SAFE_FREE(new_path);
		cur = get_next_file_entry(fl, fl->hMFL);
	}

	/* send file end */
	LOG_INFO(LOG_MOD_SESSION, "Session send ctrl END to %s" ,s->remote_ip);

	ret = send_cmd(s, INITSYNC_CMD_FILE_END, &err);
	if(ret == -1){
		/* net error */
		LOG_INFO(LOG_MOD_SESSION, "Session send ctrl END fail to %s" ,s->remote_ip);

		goto out_error;
	}

	SAFE_CLOSE_HANDLE(fl->hMFL);

	return 0;

out_error:

	SAFE_CLOSE_HANDLE(fl->hMFL);

	return -1;
}

int send_ctrl_file(session_t *s, int cmd, int *err)
{
	file_list_t *fl = (file_list_t *)s->flist;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	char *buf, *pos, *filename;
	DWORD readn;
	int ret, seq, buf_size;
	uint64 filesize, sendlen;
	LARGE_INTEGER lFilesize;

	PSESSION_CMD_HDR_T hdr;
	PSESSION_CTRL_FILE_HDR_T fhdr;

	seq = 0;

	switch(cmd){
		case INITSYNC_CMD_AFL:
			filename = fl->all_files;
			break;
		case INITSYNC_CMD_SEGMENT_LIST:
			filename = fl->segment_files;
			break;
	}

	if(s->local_rep_mode == INITSYNC_MODE_UNKNOWN){
		hFile = CreateFileA(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,NULL);
		if(hFile == INVALID_HANDLE_VALUE){
			LOG_ERROR(LOG_MOD_SESSION, "file[%s] open fail, error code : %d", filename, GetLastError());
			return -1;
		}

		/* get file size */
		if(GetFileSizeEx(hFile, &lFilesize) == 0)
		{
			LOG_ERROR(LOG_MOD_SESSION, "Get File[%s] Size Fail\n", filename);
			SAFE_CLOSE_HANDLE(hFile);
		}

		filesize = lFilesize.QuadPart;
	}else{ // LOCAL REP
		filesize = 0;
	}

	LOG_DEBUG(LOG_MOD_SESSION, "File[%s] Send , Sizs : %d", filename, filesize);
	sendlen = filesize;
	do{
		DWORD dwBytesRead;

		buf = alloc_buf(s->buf_pool, &buf_size);
		if(!buf){
			LOG_ERROR(LOG_MOD_SESSION, "Alloc buf Fail");
			return -1;
		}
		pos = buf;

		hdr = (PSESSION_CMD_HDR_T)pos;
		pos += SIZEOF_SESSION_CMD_HDR;

		fhdr = (PSESSION_CTRL_FILE_HDR_T)pos;
		pos += SIZEOF_SESSION_CTRL_FILE_HDR;

		dwBytesRead = (sendlen > (uint64)(buf_size-(SIZEOF_SESSION_CMD_HDR + SIZEOF_SESSION_CTRL_FILE_HDR))) ? (buf_size-(SIZEOF_SESSION_CMD_HDR + SIZEOF_SESSION_CTRL_FILE_HDR)) : (int)sendlen;

		readn = 0;

		if(dwBytesRead > 0){
			ret = ReadFile(hFile, pos, dwBytesRead, &readn, NULL);
			if(ret == 0){
				if(GetLastError() != ERROR_HANDLE_EOF){
					LOG_ERROR(LOG_MOD_SESSION, "Read ctrl file[%s] fail, error code : %d" ,filename, GetLastError());
					SAFE_CLOSE_HANDLE(hFile);
					free_buf(s->buf_pool, buf);
					return -1;
				}
			}
		}

		fhdr->seq = seq;
		seq++;

		fhdr->len = readn;

		hdr->type = cmd;
		hdr->length = SIZEOF_SESSION_CTRL_FILE_HDR + fhdr->len;

		//ret = net_send(s->sock, buf, readn);
		ret = session_send(s, buf, SIZEOF_SESSION_CMD_HDR + hdr->length);
		if(ret == SOCKET_ERROR){
			LOG_INFO(LOG_MOD_SESSION, "Session send ctrl file[%s] fail to %s" ,filename, s->remote_ip);
			*err = GetLastError();

			SAFE_CLOSE_HANDLE(hFile);
			
			free_buf(s->buf_pool, buf);
			return -1;
		}
		sendlen -= readn;

		if(readn == 0)
		{
			break;
		}
	}while(readn != 0);

	SAFE_CLOSE_HANDLE(hFile);

	return 0;
}

/*
 * session_accept_handler
 * Target이 Source에 접속하게 되면 session_accept_handler가 호출된다.
 *
 * 1. accept()에 의해 nsock 반환
 * 2. accept table list에 Target이 존재하는지 검사 (session_accept_check)
 * 3. accept table list에 Target이 존재한다면 init_list로 이동 (session_alloc)
 * 4. thread 생성 후 session control function launch
 */
void session_accept_handler(void *dis, SOCKET sock, void *data, int err)
{
	session_t *new_s;
	remote_host_table_entry_t *ae;
	node_t *node;
	SOCKET nsock;
	char remoteip[16];
	int ret;

	int i;

	struct timeval tv_timeo={0,10000}; // 10ms

	node = data;

	nsock = net_accept(sock, NULL);
	if (nsock == INVALID_SOCKET) {
		LOG_ERROR(LOG_MOD_SESSION, "Session Accept fail, error code : %d", WSAGetLastError());
		return;
	}
	
	ret = net_set_reuseaddr(nsock);
	if (ret < 0) {
		LOG_ERROR(LOG_MOD_SESSION, "sock set reuseaddr fail, error code : %d", WSAGetLastError());
		return;
	}

	ret = net_setsockopt(nsock, SOL_SOCKET, SO_RCVTIMEO, &tv_timeo, sizeof(tv_timeo));
	if (ret < 0) {
		LOG_ERROR(LOG_MOD_SESSION, "sock set sockopt, error code : %d", WSAGetLastError());
		return;
	}

	ret = net_setsockopt(nsock, SOL_SOCKET, SO_SNDTIMEO, &tv_timeo, sizeof(tv_timeo));
	if (ret < 0) {
		LOG_ERROR(LOG_MOD_SESSION, "sock set sockopt, error code : %d", WSAGetLastError());
		return;
	}

#ifndef _USE_DISPACHER_TH_ 
	ret = net_set_block(nsock);
	if(ret < 0) {
		LOG_ERROR(LOG_MOD_SESSION, "sock set block fail, error code : %d", WSAGetLastError());
		return;
	}
#endif

#ifdef _NAGLE_OFF_
	/* Nagle off */
	opt_val = 1; // nagle off
	ret = net_setsockopt(nsock, IPPROTO_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val));
	if(ret < 0){
		return;
	}
#endif

	net_get_peer_ip(nsock, remoteip);

	LOG_INFO(LOG_MOD_SESSION, "new client comes from %s", remoteip);

	for(i = 0; i < node->nr_nics; i++)
	{
		if(!strcmp(remoteip, node->nic[i])){
			/* SD cmd msg */
			LOG_INFO(LOG_MOD_SESSION, "SD connect");
			ret = recv_regist((dispatcher_t *)node->dis, nsock, node->sd_cmd_cntl, sd_cmd_handler);
			return;
		}
	}

	if(node->status == NODE_STATUS_PAUSE){		
		/* SD cmd msg */
		LOG_INFO(LOG_MOD_SESSION, "remote connect for SD CMD");
		ret = recv_regist((dispatcher_t *)node->dis, nsock, node->sd_cmd_cntl, sd_cmd_handler);
		return;
	}

	if(g_stopFlag)
	{
		LOG_INFO(LOG_MOD_SESSION, "Initsync is Stoped, Now. %s is not acceptable", remoteip);
		send_regist(dis, nsock, node, session_not_acceptable_handler);
		return;
	}

	if(node->status == NODE_STATUS_FAILOVER){
		LOG_DEBUG(LOG_MOD_SESSION, "Failover Connect");
		
		set_failover_conn(nsock);
		node->status = NODE_STATUS_RUN;
		return;
	}

	ae = session_accept_check(node->session_manager, remoteip);
	if(!ae) {
		LOG_INFO(LOG_MOD_SESSION, "%s is not accepable", remoteip);
		send_regist(dis, nsock, node, session_not_acceptable_handler);
		return;
	}

	new_s = session_alloc(node->session_manager);
	if (!new_s) {
		/* out of free list */
		LOG_ERROR(LOG_MOD_SESSION, "out of free session");
		send_regist(dis, nsock, node, session_not_acceptable_handler);	
		return;
	}

	new_s->remote_id = ae->id;

	new_s->name = malloc(strlen(ae->name) + 1);
	if(!new_s->name)
	{
		LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		send_regist(dis, nsock, node, session_connect_error_handler);	
		return;
	}

	ret = strncpy_s(new_s->name, strlen(ae->name) + 1, ae->name, _TRUNCATE);
	if(ret != 0){
		LOG_ERROR(LOG_MOD_SESSION, "strcpy fail %s:%d", __FILE__, __LINE__);
		send_regist(dis, nsock, node, session_connect_error_handler);	
		return;
	}

	new_s->remote_ip = malloc(strlen(remoteip) + 1);
	if(!new_s->remote_ip)
	{
		LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		send_regist(dis, nsock, node, session_connect_error_handler);	
		return;
	}

	ret = strncpy_s(new_s->remote_ip, strlen(remoteip) + 1, remoteip, _TRUNCATE);
	if(ret != 0){
		LOG_ERROR(LOG_MOD_SESSION, "strcpy fail %s:%d", __FILE__, __LINE__);
		send_regist(dis, nsock, node, session_connect_error_handler);	
		return;
	}

	new_s->sock = nsock;
	new_s->addrlen = sizeof(new_s->remoteaddr);
	getpeername(new_s->sock, (SOCKADDR *)&(new_s->remoteaddr), &(new_s->addrlen));

#ifdef _USE_DISPACHER_TH_
	recv_regist(dis, new_s->sock, new_s, session_recv_handler);
	send_regist(dis, new_s->sock, new_s, session_send_handler);
#endif

	get_current_channel(BLOCKING_CALL);
	set_session_connected(new_s->sock);

	/* create thread */
	new_s->session_thread = (HANDLE)_beginthreadex( 
		NULL, 0, session_control_thread, new_s, 0, &(new_s->session_thread_id));
	if(new_s->session_thread == 0)
	{
		LOG_ERROR(LOG_MOD_SESSION, "work thread create fail, error code : %d", errno);
		send_regist(dis, nsock, node, session_connect_error_handler);	
		return;
	}

#ifndef _USE_DISPACHER_TH_
	new_s->recv_thread = (HANDLE)_beginthreadex( 
		NULL, 0, session_recv_thread, new_s, 0, &(new_s->recv_thread_id));
	if(new_s->recv_thread == 0)
	{
		LOG_ERROR(LOG_MOD_SESSION, "recv thread create fail, error code : %d", errno);
		send_regist(dis, nsock, node, session_connect_error_handler);	
		return;
	}

	new_s->send_thread = (HANDLE)_beginthreadex( 
		NULL, 0, session_send_thread, new_s, 0, &(new_s->send_thread_id));
	if(new_s->send_thread == 0)
	{
		LOG_ERROR(LOG_MOD_SESSION, "send thread create fail, error code : %d", errno);
		send_regist(dis, nsock, node, session_connect_error_handler);	
		return;
	}
#endif
}

void session_connect_error_handler(void *dis, SOCKET sock, void *data, int err)
{
	/* send errer message */
	LOG_DEBUG(LOG_MOD_SESSION, "ERROR Handler");

	/* session dealloc */
	session_dealloc(data);

	/* close */
	net_closesocket(sock);
}

void session_not_acceptable_handler(void *dis, SOCKET sock, void *data, int err)
{
	/* send errer message */

	/* close */
	net_closesocket(sock);
}


int session_stop_process(session_t *s)
{
	int ret, err;

	if(g_stopFlag & (SD_CMD_PAUSE_IMMEDIATE | SD_CMD_STOP)){
		/* Clear Queue */
		clear_queue(s->send_Q, s->buf_pool, free_buf);
		LOG_DEBUG(LOG_MOD_SESSION, "Clear Queue 0x%x : free : %d, queue : %d", s->send_Q, s->send_Q->nr_free, s->send_Q->nr_queue);
	}
		
	if(g_stopFlag & SD_CMD_STOP){
		LOG_INFO(LOG_MOD_SESSION, "Session send ctrl STOP to REMOTE Initsync");

		if(g_stopFlag & SD_CMD_STOP_WITH_MDP)
			ret = send_cmd(s, INITSYNC_CMD_STOP_WITH_MDP, &err);
		else
			ret = send_cmd(s, INITSYNC_CMD_STOP, &err);
	}else if(g_stopFlag & SD_CMD_PAUSE){
		LOG_INFO(LOG_MOD_SESSION, "Session send ctrl PAUSE to REMOTE Initsync");
		if(g_stopFlag == SD_CMD_PAUSE_IMMEDIATE)
			ret = send_cmd(s, INITSYNC_CMD_PAUSE_IMMEDIATE, &err);
		else
			ret = send_cmd(s, INITSYNC_CMD_PAUSE, &err);
	}

	LOG_DEBUG(LOG_MOD_SESSION, "Send Stop : free : %d, queue : %d", s->send_Q->nr_free, s->send_Q->nr_queue);
	return ret;
}

int msg_processing(session_t *s)
{
	PSESSION_CMD_HDR_T hdr;
	int ret;

	while(1){
		s->data = session_recv(s, &s->data_len, 1);
		if(!s->data){
			LOG_DEBUG(LOG_MOD_SESSION, "session recv returns NULL");

			if(s->run_state == SESSION_RUN_STATE_FAIL)
				NOTIFY_FAIL(g_node->sd_notifier, s->remote_id);

			if(s->state == SESSION_STATE_TERM){
				break;
			}

			/* target 은 종료시, source이 connection 을 끊기를 기다린다 */
			if((g_node->mode == INITSYNC_MODE_TARGET) && 
				(s->run_state == SESSION_RUN_STATE_STOP 
				|| s->run_state == SESSION_RUN_STATE_FAIL
				|| s->run_state == SESSION_RUN_STATE_PAUSE 
				|| s->run_state == SESSION_RUN_STATE_COMPLETE)){

				switch(s->run_state){
					case SESSION_RUN_STATE_STOP:
						NOTIFY_STOP(g_node->sd_notifier, s->remote_id);
						break;
					case SESSION_RUN_STATE_FAIL:
						//NOTIFY_FAIL(g_node->sd_notifier, s->remote_id);
						break;
					case SESSION_RUN_STATE_COMPLETE:
						NOTIFY_COMPLETE(g_node->sd_notifier, s->remote_id);
						break;
				}

				s->state = SESSION_STATE_TERM;

			}else{
				goto error;
			}
		}else{
			hdr = (PSESSION_CMD_HDR_T)s->data;

			ret = session_handler[hdr->type](s);
			if(ret < 0)
			{
				LOG_DEBUG(LOG_MOD_SESSION, "msg handler[%d] fail", hdr->type);

				if(g_stopFlag){
					if(g_stopFlag & SD_CMD_STOP){
						s->run_state = SESSION_RUN_STATE_STOP;
					}else if(g_stopFlag){
						s->run_state = SESSION_RUN_STATE_PAUSE;
					}

					if(g_node->status == NODE_STATUS_FAILOVER)
						s->run_state = SESSION_RUN_STATE_FAIL;

					if(g_node->mode == INITSYNC_MODE_SOURCE || 
						s->local_rep_mode == INITSYNC_MODE_SOURCE)
						session_stop_process(s);
				}else{
					/* Sync fail */
					session_fail_process(s);
				}
			}

			free_buf(s->buf_pool, s->data);
			s->data = NULL;
		}

		if(s->state == SESSION_STATE_TERM)
			break;
	}
	return 0;

error:
	LOG_DEBUG(LOG_MOD_SESSION, "MSG Processing Error");
	if(s->data){
		free_buf(s->buf_pool, s->data);
		s->data = NULL;
	}
	return -1;
}

int session_mode_check(session_t *s)
{
	int err;

	if(s->resume){
		s->run_state = SESSION_RUN_STATE_RESUME;
	}else{
		s->run_state = SESSION_RUN_STATE_AFL;
	}
	return send_cmd(s, INITSYNC_CMD_CHECK_MODE, &err);
}

/*
 *	session control thread - Init sync 진행
 *	1. file list alloc
 *	2. resume check
 *  3. call processing
 */
unsigned int WINAPI session_control_thread(LPVOID arg)
{
	session_t *s = (session_t *)arg;
	session_manager_t *sm = s->sm;
	node_t *node = (node_t *)sm->node;
	file_manager_t *fm = (file_manager_t *)node->file_manager;
	file_list_t *fl;
	int ret, err;

	LOG_DEBUG(LOG_MOD_SESSION, "session control thread start");

	if(s->local_rep_mode != INITSYNC_MODE_UNKNOWN){	// Local Replication
		
		if(s->local_rep_mode == INITSYNC_MODE_TARGET){
			WaitForSingleObject(sm->local_rep_sync_event, INFINITE);
		}

		fl = flist_alloc(fm, s->name, s->name);
		if(!fl)
		{
			goto out_no_file_list;
		}
		fl->src_index = node->index;

		s->flist = fl;

		if(s->local_rep_mode == INITSYNC_MODE_SOURCE){
			flist_duplicate(fm, fl);
			SetEvent(sm->local_rep_sync_event);
		}
	}else{
		if(node->mode == INITSYNC_MODE_SOURCE){
			fl = flist_alloc(fm, node->name, s->name);
			if(!fl)
			{
				goto out_no_file_list;
			}
			fl->src_index = node->index;

			s->flist = fl;
		}else{ /* TARGET */
			fl = flist_alloc(fm, s->name, node->name);
			if(!fl)
			{
				goto out_no_file_list;
			}
			fl->src_index = s->remote_id;

			s->flist = fl;
		}
	}

	s->state = SESSION_STATE_RUN;

	/* resume check */
	s->run_state = SESSION_RUN_STATE_MODE_CHECK;

	if(node->mode == INITSYNC_MODE_TARGET ||
		node->mode == INITSYNC_MODE_LOCAL_REPLICATION && s->local_rep_mode == INITSYNC_MODE_TARGET){
		/* mode check */
		session_mode_check(s);
	}

	//ret = processing(s);
	msg_processing(s);

	/* if target, wait for socket close from source */
#if 0
	if(node->mode == INITSYNC_MODE_TARGET && s->run_state == SESSION_RUN_STATE_COMPLETE){
		WaitForSingleObject(s->destroy_recv_th_event, INFINITE);
		WaitForSingleObject(s->destroy_send_th_event, INFINITE);
	}
#endif

	session_dealloc(s);

	_endthread();
	return 0;

out_no_file_list:
	/* send no file list error */
	ret = send_cmd(s, INITSYNC_CNTL_ERROR, &err);
	if(ret == -1){ /* error */
		/* destroy thread */
		LOG_DEBUG(LOG_MOD_SESSION, "ERROR No File List");
		session_dealloc(s);
	}

	_endthread();
	return -1;
}

#if 0
unsigned int WINAPI session_local_recv_thread(LPVOID arg)
{
	session_t *s = (session_t *)arg;
	session_manager_t *sm = s->sm;
	node_t *node = sm->node;
	file_list_t *fl;// = (file_list_t *)s->flist;
	COMM_MSG_STRUCT msg_hdr;
	int recvlen;
	HANDLE hMapFile = INVALID_HANDLE_VALUE;
	uint64 *pWrite = NULL;



	char *buf;
	int len;

	buf = malloc(g_recv_buf_size);
	if(!buf){

	};
	memset(buf, 0, g_recv_buf_size);

	while(1){
		fl = (file_list_t *)s->flist;

		if(s->local_rep_mode == INITSYNC_MODE_SOURCE){
			/* Source 가 받는 메시지는 모두 고정길이 메시지 이므로 recv thread 에서 메시지 header를 구분할 수 있다. */
			recvlen = 0;
			while(recvlen < SIZEOF_COMM_MSG_STRUCT){
				len = popQ(s->connect_Q, (char *)&msg_hdr + recvlen, SIZEOF_COMM_MSG_STRUCT - recvlen, 1);
				if(len <= 0){
					if(s->state == SESSION_STATE_TERM){
						LOG_INFO(LOG_MOD_SESSION, "Session[%s] Terminate. return RECV Thread", s->remote_ip);
					}else{
						LOG_ERROR(LOG_MOD_SESSION, "Session[%s] fail. Terminate RECV Thread", s->remote_ip);
					}

					if(pWrite){
						/* Close history file, mmap */
						UnmapViewOfFile(pWrite);
						pWrite = NULL;
					}

					if(hMapFile != INVALID_HANDLE_VALUE){
						CloseHandle(hMapFile);
						hMapFile = INVALID_HANDLE_VALUE;
					}

					if(fl->hHistory != INVALID_HANDLE_VALUE){
						CloseHandle(fl->hHistory);
						fl->hHistory = INVALID_HANDLE_VALUE;
					}

					session_crash(s);
					SetEvent(s->destroy_recv_th_event);
					return 0; //break;
				}

				recvlen += len;
			}

			if(msg_hdr.fid == INITSYNC_CNTL_FILE_ACK){
				/* FILE ACK */
				/* Send file 성능 향상을 위해 file ack 는 recv Th에서 처리한다. */
				if(fl->hHistory == INVALID_HANDLE_VALUE){
					/* file open, mmap open */
					fl->hHistory = CreateFileA(fl->history, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					if(fl->hHistory ==  INVALID_HANDLE_VALUE){
						fl->hHistory = CreateFileA(fl->history, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
						if(fl->hHistory ==  INVALID_HANDLE_VALUE){
							LOG_ERROR(LOG_MOD_SESSION, "history file open fail, error code : %d\n", GetLastError());
							session_crash(s);
							SetEvent(s->destroy_recv_th_event);
							return -1;
						}	
					}

					hMapFile = CreateFileMapping(fl->hHistory, NULL, PAGE_READWRITE, 0, sizeof(uint64) * 4, NULL);
					if(hMapFile == NULL){
						LOG_ERROR(LOG_MOD_SESSION, "Create file Map fail, error code : %d\n", GetLastError());
						session_crash(s);
						SetEvent(s->destroy_recv_th_event);
						return -1;
					}

					pWrite = (uint64 *)MapViewOfFile(hMapFile, FILE_MAP_WRITE, 0, 0, 0);
					if(pWrite == NULL){
						LOG_ERROR(LOG_MOD_SESSION, "Map View fail, error code : %d\n", GetLastError());
						session_crash(s);
						SetEvent(s->destroy_recv_th_event);
						return -1;
					}
				}

				/* history update */

				pWrite[0] = msg_hdr.gsid;
				pWrite[1] = msg_hdr.hsid;
				pWrite[2] = s->sent_bytes;
				pWrite[3] = s->total_bytes;

				/* notify to SD */
				NOTIFY_PROGRESS(node->sd_notifier, s->remote_id, s->total_bytes, s->sent_bytes);

				LOG_DEBUG(LOG_MOD_SESSION, "Recv Ack offset : %d, idx : %d - (%llu bytes send/%llu bytes)", (int)msg_hdr.gsid, (int)msg_hdr.hsid, s->sent_bytes, s->total_bytes);

			}else if(msg_hdr.fid == INITSYNC_CNTL_FILE_END){
				LOG_DEBUG(LOG_MOD_SESSION, "Recv File Send END");

				if(pWrite){
					/* Close history file, mmap */
					UnmapViewOfFile(pWrite);
					pWrite = NULL;
				}

				if(hMapFile != INVALID_HANDLE_VALUE){
					CloseHandle(hMapFile);
					hMapFile = INVALID_HANDLE_VALUE;
				}

				if(fl->hHistory != INVALID_HANDLE_VALUE){
					CloseHandle(fl->hHistory);
					fl->hHistory = INVALID_HANDLE_VALUE;
				}

				/* push recvQ */
				pushQ(s->recv_Q, &msg_hdr, SIZEOF_COMM_MSG_STRUCT);//, QUEUE_ENTRY_DATA_TYPE_BINARY);
			}else if(msg_hdr.fid == INITSYNC_CNTL_STOP && node->status != NODE_STATUS_PAUSE){
				LOG_DEBUG(LOG_MOD_SESSION, "Recv Pause, Disconnect Session");

				if(pWrite){
					/* Close history file, mmap */
					UnmapViewOfFile(pWrite);
					pWrite = NULL;
				}

				if(hMapFile != INVALID_HANDLE_VALUE){
					CloseHandle(hMapFile);
					hMapFile = INVALID_HANDLE_VALUE;
				}

				if(fl->hHistory != INVALID_HANDLE_VALUE){
					CloseHandle(fl->hHistory);
					fl->hHistory = INVALID_HANDLE_VALUE;
				}

				session_crash(s);
				SetEvent(s->destroy_recv_th_event);
				return 0; //break;

			}else{
				/* push Q */
				pushQ(s->recv_Q, &msg_hdr, SIZEOF_COMM_MSG_STRUCT);//, QUEUE_ENTRY_DATA_TYPE_BINARY);

				recvlen = msg_hdr.length - SIZEOF_COMM_MSG_STRUCT;
				while(recvlen > 0){
					DWORD dwBytesRead = (recvlen > g_recv_buf_size) ? g_recv_buf_size : recvlen;

					len = net_recv(s->sock, buf , dwBytesRead);

					len = popQ(s->connect_Q, buf, dwBytesRead, 1);
					if(len <= 0){
						if(s->state == SESSION_STATE_TERM){
							LOG_INFO(LOG_MOD_SESSION, "Session[%s] Terminate. return RECV Thread", s->remote_ip);
						}else{
							LOG_ERROR(LOG_MOD_SESSION, "Send Queue for Session[%s] fail. Terminate RECV Thread", s->remote_ip);
						}

						if(pWrite){
							/* Close history file, mmap */
							UnmapViewOfFile(pWrite);
							pWrite = NULL;
						}

						if(hMapFile != INVALID_HANDLE_VALUE){
							CloseHandle(hMapFile);
							hMapFile = INVALID_HANDLE_VALUE;
						}

						if(fl->hHistory != INVALID_HANDLE_VALUE){
							CloseHandle(fl->hHistory);
							fl->hHistory = INVALID_HANDLE_VALUE;
						}

						session_crash(s);
						SetEvent(s->destroy_recv_th_event);
						return 0; //break;
					}

					recvlen -= len;
					pushQ(s->recv_Q, buf, len);//, QUEUE_ENTRY_DATA_TYPE_BINARY);
				}
			}
		}else{ /* target */
			/* target 의 경우 길이가 가변적인 메시지를 처리해야 한다. (FILE 전송 메시지) */
			len = popQ(s->connect_Q, buf, g_recv_buf_size, 1);
			if(len > 0){
				//recvlen -= len;
				pushQ(s->recv_Q, buf, len);//, QUEUE_ENTRY_DATA_TYPE_BINARY);
			}else{
				if(s->state == SESSION_STATE_TERM){
					LOG_INFO(LOG_MOD_SESSION, "Session[%s] Terminate. return RECV Thread", s->remote_ip);
				}else{
					LOG_ERROR(LOG_MOD_SESSION, "Send Queue for Session[%s] fail. Terminate RECV Thread", s->remote_ip);
				}

				session_crash(s);
				SetEvent(s->destroy_recv_th_event);
				return 0; //break;
			} 
		}
	}

	SetEvent(s->destroy_recv_th_event);
}


#endif

#if 1
unsigned int WINAPI session_recv_thread(LPVOID arg)
{
	session_t *s = (session_t *)arg;
	char *buf, *pos;
	int buf_size, recvlen, len;
	PSESSION_CMD_HDR_T hdr;
	int ret;

	LOG_DEBUG(LOG_MOD_SESSION, "session recv thread start");

	while(1){
		buf = NULL;
		buf = alloc_buf(s->buf_pool, &buf_size);

recv_msg:
		pos = buf;
		recvlen = 0;
		while(recvlen < SIZEOF_SESSION_CMD_HDR){
			len = net_recv(s->sock, pos, SIZEOF_SESSION_CMD_HDR - recvlen);
			if(len > 0){
				recvlen += len;
				pos += len;
			}else{
				/* error */
				int err = WSAGetLastError();

				if(err != WSAETIMEDOUT){
					LOG_DEBUG(LOG_MOD_SESSION, "Session Recv Fail, error : %d", err);
					ret = session_failover(s, s->recv_thread_id);
					if(ret < 0){						
						goto error_out;
					}else{
						goto recv_msg;
					}
				}else{
					/* session dealloc check */
					if(s->state == SESSION_STATE_TERM){
						LOG_INFO(LOG_MOD_SESSION, "Session[%s] Terminate. return Recv Thread", s->remote_ip);						
						goto error_out;
					}
				}
			}
		}
		
		hdr = (PSESSION_CMD_HDR_T)buf;

		if(hdr->length > buf_size){
			/* error, wrong packet */
			LOG_DEBUG(LOG_MOD_SESSION, "Recved hdr MSG Parse Fail. Wrong Size");
			goto error_out;
		}

		if(g_node->mode == INITSYNC_MODE_TARGET &&
			(hdr->type == INITSYNC_CMD_PAUSE_IMMEDIATE || hdr->type == INITSYNC_CMD_STOP)){
			/* clear queue */
			//clear_queue(s->recv_Q, s->buf_pool, free_buf);
			clear_N_push_queue(s->recv_Q, buf, SIZEOF_SESSION_CMD_HDR, s->buf_pool, free_buf);
			continue;
		}

		recvlen = 0;
		while(recvlen < hdr->length){
			len = net_recv(s->sock, pos, hdr->length - recvlen);
			if(len > 0){
				recvlen += len;
				pos += len;
			}else{
				/* error */
				int err = WSAGetLastError();

				if(err != WSAETIMEDOUT){					
					LOG_DEBUG(LOG_MOD_SESSION, "Session Recv Fail, error : %d", err);
					ret = session_failover(s, s->recv_thread_id);
					if(ret < 0){						
						goto error_out;
					}else{
						goto recv_msg;
					}
				}else{
					/* session dealloc check */
					if(s->state == SESSION_STATE_TERM){
						LOG_INFO(LOG_MOD_SESSION, "Session[%s] Terminate. return Recv Thread", s->remote_ip);	
						
						goto error_out;
					}
				}
			}
		}

		pushQ(s->recv_Q, buf, SIZEOF_SESSION_CMD_HDR + hdr->length);
	}

	SetEvent(s->destroy_recv_th_event);

	return 0;

error_out:
	
	if(buf)
		free_buf(s->buf_pool, buf);

	session_crash(s);
	SetEvent(s->destroy_recv_th_event);

	LOG_INFO(LOG_MOD_SESSION,"Session Recv Thread Terminate");
	return 0;
}

unsigned int WINAPI session_local_recv_thread(LPVOID arg)
{
	session_t *s = (session_t *)arg;
	char *buf, *pos;
	int buf_size, recvlen, len;
	PSESSION_CMD_HDR_T hdr;

	while(1){

		len = 0;

		buf = popQ(s->connect_Q, &len, 1);
		if(buf != NULL){
			hdr = (PSESSION_CMD_HDR_T)buf;
			if(hdr->type < 0){
				LOG_ERROR(LOG_MOD_SESSION, "Session Connect FIN from %s", s->remote_ip);
				
				free_buf(s->buf_pool, buf);

				session_crash(s);
				SetEvent(s->destroy_recv_th_event);
				return 0; //break;
			}else if( ( g_node->mode == INITSYNC_MODE_LOCAL_REPLICATION && s->local_rep_mode == INITSYNC_MODE_TARGET )
				&& (hdr->type == INITSYNC_CMD_PAUSE_IMMEDIATE || hdr->type == INITSYNC_CMD_STOP)){
					/* clear queue */
					//clear_queue(s->recv_Q, s->buf_pool, free_buf);
					clear_N_push_queue(s->recv_Q, buf, SIZEOF_SESSION_CMD_HDR, s->buf_pool, free_buf);
					
					continue;
			}

			pushQ(s->recv_Q, buf, SIZEOF_SESSION_CMD_HDR + hdr->length);
		}
	}
	SetEvent(s->destroy_recv_th_event);

	return 0;
}

unsigned int WINAPI session_send_thread(LPVOID arg)
{
	session_t *s = (session_t *)arg;

	char *data;
	int len, ret, sendlen;

	LOG_DEBUG(LOG_MOD_SESSION, "session send thread start");

	while(1){
#ifdef _USE_TRANSMITFILE_
		entry_type = get_first_entry_type(s->send_Q, 1);
		if(entry_type == QUEUE_ENTRY_DATA_TYPE_BINARY){
#endif
		data = popQ(s->send_Q, &len, 1);
		if(len < 0){
			if(s->state == SESSION_STATE_TERM){
				LOG_INFO(LOG_MOD_SESSION, "Session[%s] Terminate. return Send Thread", s->remote_ip);
			}else{
				LOG_ERROR(LOG_MOD_SESSION, "Send Queue for Session[%s] fail. Terminate Send Thread", s->remote_ip);
			}
			goto send_th_out;
		}
#ifdef _USE_TRANSMITFILE_
			}else{
				len = popQ(s->send_Q, (char *)&hFile, sizeof(HANDLE), 1);
				break;
			}
#endif
		
		if(len > 0 && data){
send_msg:
			sendlen = 0;
			while(len - sendlen > 0){
				ret = net_send(s->sock, data + sendlen, len - sendlen);
				if(ret == SOCKET_ERROR){ /* error 처리 */
					int err = WSAGetLastError();
					if(err != WSAETIMEDOUT){
						LOG_DEBUG(LOG_MOD_SESSION, "Session Send Fail, error : %d", err);
						ret = session_failover(s, s->send_thread_id);
						if(ret < 0){
							goto send_th_out;
						}else{
							goto send_msg;
						}
					}else{
						/* session dealloc check */
						if(s->state == SESSION_STATE_TERM){
							LOG_INFO(LOG_MOD_SESSION, "Session[%s] Terminate. return Send Thread", s->remote_ip);

							goto send_th_out;
						}
					}
				}else{
					sendlen += ret;
				}
			}
		}else if(s->failover == SESSION_FAILOVER){ // recv th wakeup popQ
			ret = session_failover(s, s->send_thread_id);
			if(ret < 0){

				goto send_th_out;
			}else{
				continue;
			}
		}

		if(data){
			free_buf(s->buf_pool, data);
			data = NULL;
		}

#ifdef _USE_TRANSMITFILE_
		if(hFile != INVALID_HANDLE_VALUE){
			ret = TransmitFile(s->sock, hFile, 0, 4096, NULL, NULL, 0);
			if(ret == SOCKET_ERROR)
			{
				LOG_ERROR("SessionConnect Fail, send %d to %s, error code : %d", len, s->remote_ip, WSAGetLastError());

				net_closesocket(s->sock);
				s->sock = INVALID_SOCKET;

				flush_queue(s->recv_Q);
				flush_queue(s->send_Q);

				CloseHandle(hFile);
				return 0;//break;
			}
			CloseHandle(hFile);
		}
#endif
	}

send_th_out:
	session_crash(s);
	SetEvent(s->destroy_send_th_event);
	if(data){
		free_buf(s->buf_pool, data);
		data = NULL;
	}

	LOG_INFO(LOG_MOD_SESSION, "Session Send Thread Terminate");

	return 0;
}
#endif

static void session_connect_failover_complete(void *dis, SOCKET sock, void *data, int err)
{
	session_t *s = (session_t *)data;
	session_manager_t *sm = s->sm;
	remote_host_table_entry_t *remote;
	node_t *node = sm->node;
	char remoteip[16];
	ip_t *ip;
	int ret, ch_id, i;

	if(err == 0)
	{
		net_get_peer_ip(sock, remoteip);

		LOG_INFO(LOG_MOD_SESSION, "connect success to %s", remoteip);

		ret = net_set_block(sock);
		if (ret < 0) {
			LOG_INFO(LOG_MOD_SESSION, "Set Blocking Sock Fail");
			return;
		}

		SAFE_FREE(s->remote_ip);

		s->remote_ip = malloc(strlen(remoteip) + 1);
		if(!s->remote_ip)
		{
			LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
			//send_regist(dis, sock, node, session_connect_error_handler);	
			return;
		}
		strcpy(s->remote_ip, remoteip);
		
		set_failover_conn(sock);
	}else{
		LOG_DEBUG(LOG_MOD_SESSION, "FailOver Connect Fail. Connect Re Try..");
		remote = find_remote_host_table_by_name(sm, s->name);
		if(!remote)
		{
			/* fail */
			LOG_ERROR(LOG_MOD_SESSION, "There are no remote host info");
			return;
		}

		ch_id = get_failover_channel(INFINITE);

		if(ch_id < 0){
			LOG_ERROR(LOG_MOD_SESSION, "Remote Node Fail");
			return;
		}

		/* get remote ip */
		ip = list_first_entry(&remote->ip_list, ip_t, lnode);
		for(i = 0; i < ch_id; i++){
			ip = list_next_entry(ip, ip_t, lnode);
		}

		/* connect to source */
		LOG_INFO(LOG_MOD_SESSION, "Connect to %s:%d try...", ip->ip, node->port);
		ret = conn_regist(dis, sock, s, session_connect_failover_complete, ip->ip, node->port);
	}
}

/* create thread */
static void session_connect_complete(void *dis, SOCKET sock, void *data, int err)
{
	session_t *s = (session_t *)data;
	session_manager_t *sm = s->sm;
	remote_host_table_entry_t *remote;
	node_t *node = sm->node;
	char remoteip[16];
	ip_t *ip;
	int ret, ch_id, i;

	s->nr_try++;

	if(err == 0)
	{
		net_get_peer_ip(s->sock, remoteip);

		LOG_INFO(LOG_MOD_SESSION, "connect success to %s", remoteip);

#if 1
		ret = net_set_block(s->sock);
		if (ret < 0) {
			return;
		}
#endif

		//recv_regist(dis, sock, s, session_recv_handler);

		s->remote_ip = malloc(strlen(remoteip) + 1);
		if(!s->remote_ip)
		{
			LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
			//send_regist(dis, sock, node, session_connect_error_handler);	
			return;
		}
		strcpy(s->remote_ip, remoteip);

		set_session_connected(s->sock);

		s->session_thread = (HANDLE)_beginthreadex( 
			NULL, 0, session_control_thread, s, 0, &(s->session_thread_id));
		if(s->session_thread == 0)
		{
			LOG_ERROR(LOG_MOD_SESSION, "work thread create fail, error code : %d", errno);
			return;
		}

		s->recv_thread = (HANDLE)_beginthreadex( 
			NULL, 0, session_recv_thread, s, 0, &(s->recv_thread_id));
		if(s->recv_thread == 0)
		{
			LOG_ERROR(LOG_MOD_SESSION, "recv thread create fail, error code : %d", errno);
			return;
		}

		s->send_thread = (HANDLE)_beginthreadex( 
			NULL, 0, session_send_thread, s, 0, &(s->send_thread_id));
		if(s->send_thread == 0)
		{
			LOG_ERROR(LOG_MOD_SESSION, "send thread create fail, error code : %d", errno);
			return;
		}

	}else{

		if(s->nr_try > MAX_CONN_TRY){
			LOG_INFO(LOG_MOD_SESSION, "Connect failed %d times. Check Source[%d] Node", s->nr_try, s->name);
			sm->nr_fail++;
			return;
		}
		LOG_INFO(LOG_MOD_SESSION, "connect fail. connect retry");

		remote = find_remote_host_table_by_name(sm, s->name);
		if(!remote)
		{
			/* fail */
			LOG_ERROR(LOG_MOD_SESSION, "There are no remote host info");
			return;
		}

		ch_id = get_failover_channel(INFINITE);

		if(ch_id < 0){
			LOG_ERROR(LOG_MOD_SESSION, "Remote Node Fail");
			return;
		}

		/* get remote ip */
		ip = list_first_entry(&remote->ip_list, ip_t, lnode);
		for(i = 0; i < ch_id; i++){
			ip = list_next_entry(ip, ip_t, lnode);
		}

		/* connect to source */
		LOG_INFO(LOG_MOD_SESSION, "Connect to %s:%d try...", ip->ip, node->port);
		ret = conn_regist(dis, s->sock, s, session_connect_complete, ip->ip, node->port);
	}
}

/* connect to remote */
int session_connect(session_t *s)
{
	session_manager_t *sm = s->sm;
	remote_host_table_entry_t *remote;
	node_t *node = sm->node;
	ip_t *ip;
	int ret, opt, i, ch_id;
	
	remote = find_remote_host_table_by_name(sm, s->name);
	if(!remote)
	{
		/* fail */
		LOG_ERROR(LOG_MOD_SESSION, "There are no remote host info");
		return -1;
	}

	s->remote_id = remote->id;

	/* init sock */
	s->sock = net_tcp_socket();
	if (s->sock == INVALID_SOCKET) {
		LOG_ERROR(LOG_MOD_SESSION, "Socket Create Fail");
		return -1;
	}

	ret = net_set_reuseaddr(s->sock);
	if (ret < 0) {
		LOG_ERROR(LOG_MOD_SESSION, "Socket set reuseaddr Fail");
		return -1;
	}

	ret = net_set_nonblock(s->sock);
	if (ret < 0) {
		LOG_ERROR(LOG_MOD_SESSION, "Socket set nonblock Fail");
		return -1;
	}

	ret = net_set_sendbuf_size(s->sock, g_sock_buf_size);
	if (ret == SOCKET_ERROR) {
		LOG_ERROR(LOG_MOD_SESSION, "setsockopt SO_SNDBUF[%d] fail", g_sock_buf_size);
		return -1;
	}

	ret = net_set_recvbuf_size(s->sock, g_sock_buf_size);
	if (ret == SOCKET_ERROR) {
		LOG_ERROR(LOG_MOD_SESSION, "setsockopt SO_RCVBUF[%d] fail", g_sock_buf_size);
		return -1;
	}

	ch_id = get_current_channel(BLOCKING_CALL);

	if(ch_id < 0){
		LOG_ERROR(LOG_MOD_SESSION, "Remote Node Fail");
		return -1;
	}

	/* get remote ip */
	ip = list_first_entry(&remote->ip_list, ip_t, lnode);
	for(i = 0; i < ch_id; i++){
		ip = list_next_entry(ip, ip_t, lnode);
	}
	
	/* connect to source */
	LOG_INFO(LOG_MOD_SESSION, "Connect to %s:%d try...", ip->ip, node->port);
	ret = conn_regist(node->dis, s->sock, s, session_connect_complete, ip->ip, node->port);

	return 0;
}


int session_connect_failover(session_t *s)
{
	session_manager_t *sm = s->sm;
	remote_host_table_entry_t *remote;
	node_t *node = sm->node;
	ip_t *ip;
	int ret, opt, i, ch_id;
	SOCKET sock;

	remote = find_remote_host_table_by_name(sm, s->name);
	if(!remote)
	{
		/* fail */
		LOG_ERROR(LOG_MOD_SESSION, "There are no remote host info");
		return -1;
	}
	
	/* init sock */
	sock = net_tcp_socket();
	if (sock == INVALID_SOCKET) {
		LOG_ERROR(LOG_MOD_SESSION, "Socket Create Fail");
		return -1;
	}

	ret = net_set_reuseaddr(sock);
	if (ret < 0) {
		LOG_ERROR(LOG_MOD_SESSION, "Socket set reuseaddr Fail");
		return -1;
	}

	ret = net_set_nonblock(sock);
	if (ret < 0) {
		LOG_ERROR(LOG_MOD_SESSION, "Socket set nonblock Fail");
		return -1;
	}

	ret = net_set_sendbuf_size(sock, g_sock_buf_size);
	if (ret == SOCKET_ERROR) {
		LOG_ERROR(LOG_MOD_SESSION, "setsockopt SO_SNDBUF[%d] fail", g_sock_buf_size);
		return -1;
	}

	ret = net_set_recvbuf_size(sock, g_sock_buf_size);
	if (ret == SOCKET_ERROR) {
		LOG_ERROR(LOG_MOD_SESSION, "setsockopt SO_RCVBUF[%d] fail", g_sock_buf_size);
		return -1;
	}

	/* get remote ip */
	ch_id = get_current_channel(NON_BLOCKING_CALL);

	if(ch_id < 0){
		LOG_ERROR(LOG_MOD_SESSION, "Remote Node Fail");
		return -1;
	}

	ip = list_first_entry(&remote->ip_list, ip_t, lnode);
	for(i = 0; i < ch_id; i++){
		ip = list_next_entry(ip, ip_t, lnode);
	}
	
	ret = net_bind(sock, g_node->nic[ch_id], 0);
	if (ret == SOCKET_ERROR) {
		int err = GetLastError();
		LOG_ERROR(LOG_MOD_MAIN, "Bind Fail %s, err : %d", g_node->nic[ch_id], err);
		return -1;
	}

	/* connect to source */
	LOG_INFO(LOG_MOD_SESSION, "Connect to %s:%d try...", ip->ip, node->port);
	ret = conn_regist(node->dis, sock, s, session_connect_failover_complete, ip->ip, node->port);

	return 0;
}


void session_local_connect(session_t *src, session_t *tgt)
{
	src->recv_Q = init_queue(DEFAULT_QUEUE_SIZE);
	src->send_Q = init_queue(LARGE_QUEUE_SIZE);

	set_queue_nagle(src->send_Q, QUEUE_NAGLE_OFF);

	tgt->recv_Q = init_queue(LARGE_QUEUE_SIZE);
	tgt->send_Q = init_queue(DEFAULT_QUEUE_SIZE);

	set_queue_nagle(tgt->send_Q, QUEUE_NAGLE_OFF);

	src->connect_Q = tgt->send_Q;
	tgt->connect_Q = src->send_Q;

	src->buf_pool = init_buf_pool(2*(LARGE_QUEUE_SIZE + DEFAULT_QUEUE_SIZE), g_file_buf_size);
	if(!src->buf_pool)
	{
		LOG_ERROR(LOG_MOD_SESSION, "Create buffer pool fail");
	}

	tgt->buf_pool = src->buf_pool;
}

int close_local_connect(session_t *s)
{
	int err;

	send_cmd(s, -1, &err);

	return 0;
}

void session_local_initsync(void *arg)
{
	session_t *src, *tgt;
	node_t *node = (node_t *)arg;
	
	int ret;

	node->mode = INITSYNC_MODE_LOCAL_REPLICATION;

	LOG_INFO(LOG_MOD_SESSION, "Init Local Initsync");

	src = session_alloc(node->session_manager);
	if (!src) {
		/* out of free list */
		LOG_ERROR(LOG_MOD_SESSION, "out of free session");
		return;
	}

	src->name = malloc(strlen(node->name) + 1);
	if(!src->name)
	{
		LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		return;
	}

	ret = strncpy_s(src->name, strlen(node->name) + 1, node->name, _TRUNCATE);
	if(ret != 0){
		LOG_ERROR(LOG_MOD_SESSION, "strcpy fail %s:%d", __FILE__, __LINE__);
		return;
	}

	src->local_rep_mode = INITSYNC_MODE_SOURCE;

	src->remote_ip = malloc(strlen(src->name) + strlen("tgt") + 2);
	if(!src->remote_ip)
	{
			LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		return;
	}

	sprintf(src->remote_ip, "%s_tgt", src->name);
	
	tgt = session_alloc(node->session_manager);
	if (!tgt) {
		/* out of free list */
		LOG_ERROR(LOG_MOD_SESSION, "out of free session");
		return;
	}

	tgt->name = malloc(strlen(node->name) + 1);
	if(!tgt->name)
	{
		LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		return;
	}

	ret = strncpy_s(tgt->name, strlen(node->name) + 1, node->name, _TRUNCATE);
	if(ret != 0){
		LOG_ERROR(LOG_MOD_SESSION, "strcpy fail %s:%d", __FILE__, __LINE__);
		return;
	}

	tgt->local_rep_mode = INITSYNC_MODE_TARGET;

	
	tgt->remote_ip = malloc(strlen(tgt->name) + strlen("src") + 2);
	if(!tgt->remote_ip)
	{
		LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
		return;
	}

	sprintf(tgt->remote_ip, "%s_src", src->name);

	session_local_connect(src, tgt);
	LOG_INFO(LOG_MOD_SESSION, "Local Initsync Connected");

	/* create thread */
	src->session_thread = (HANDLE)_beginthreadex( 
		NULL, 0, session_control_thread, src, 0, &(src->session_thread_id));
	if(src->session_thread == 0)
	{
		LOG_ERROR(LOG_MOD_SESSION, "work thread create fail, error code : %d", errno);
		return;
	}

	src->recv_thread = (HANDLE)_beginthreadex( 
		NULL, 0, session_local_recv_thread, src, 0, &(src->recv_thread_id));
	if(src->recv_thread == 0)
	{
		LOG_ERROR(LOG_MOD_SESSION, "recv thread create fail, error code : %d", errno);
		return;
	}

	Sleep(1000);

	tgt->session_thread = (HANDLE)_beginthreadex( 
		NULL, 0, session_control_thread, tgt, 0, &(tgt->session_thread_id));
	if(tgt->session_thread == 0)
	{
		LOG_ERROR(LOG_MOD_SESSION, "work thread create fail, error code : %d", errno);
		return;
	}

	
	tgt->recv_thread = (HANDLE)_beginthreadex( 
		NULL, 0, session_local_recv_thread, tgt, 0, &(tgt->recv_thread_id));
	if(tgt->recv_thread == 0)
	{
		LOG_ERROR(LOG_MOD_SESSION, "recv thread create fail, error code : %d", errno);
		return;
	}

	LOG_INFO(LOG_MOD_SESSION, "Local Initsync Ready");

}

unsigned int WINAPI session_send_file_thread(LPVOID arg)
{
	int ret, err;
	session_t *s = (session_t *)arg;

	s->send_duration = get_unix_time_ms();

	ResetEvent(s->destroy_send_file_th_event);

	LOG_INFO(LOG_MOD_SESSION, "[T:%s] Send File Thread Start", s->remote_ip);

	ret = send_files(s);
	if(ret < 0){
		LOG_INFO(LOG_MOD_SESSION, "[T:%s] Do not Send ALL", s->remote_ip);

		if(g_stopFlag){
			if(g_stopFlag & SD_CMD_STOP){
				s->run_state = SESSION_RUN_STATE_STOP;
			}else if(g_stopFlag){
				s->run_state = SESSION_RUN_STATE_PAUSE;
			}
			if(g_node->status == NODE_STATUS_FAILOVER)
				s->run_state = SESSION_RUN_STATE_FAIL;

			session_stop_process(s);
		}else{
			/* Sync fail */
			session_fail_process(s);
		}
	}

	SetEvent(s->destroy_send_file_th_event);

	_endthread();
	return 0;
}

int session_check_mode_handler(session_t *s)
{
	PSESSION_CHECK_MODE_HDR_T hdr;
	char *pos;
	int ret, err;

	if(s->state != SESSION_STATE_RUN){
		goto check_mode_error;
	}

	if(s->run_state != SESSION_RUN_STATE_MODE_CHECK){
		goto check_mode_error;
	}

	pos = s->data;

	pos += SIZEOF_SESSION_CMD_HDR;
	hdr = (PSESSION_CHECK_MODE_HDR_T)pos;

	LOG_INFO(LOG_MOD_SESSION, "Recv Initsync Mode Type : %d", hdr->mode);

	if(hdr->mode == INITSYNC_CNTL_RESUME){ //resume 인 경우
		ret = send_cmd(s, INITSYNC_CMD_RESUME, &err);
		if(ret == -1){ /* error */
			s->run_state = SESSION_RUN_STATE_FAIL;
			goto check_mode_error;
		}

		segment_mdp_resume(s->flist, g_node->mdp_controller);

		s->run_state = SESSION_RUN_STATE_READY;
	}else{                                 //normal인 경우
		ret = send_cmd(s, INITSYNC_CMD_REQ_AFL, &err);
		if(ret == -1){ /* error */
			s->run_state = SESSION_RUN_STATE_FAIL;
			goto check_mode_error;
		}

		s->run_state = SESSION_RUN_STATE_AFL;
	}

	s->total_duration = get_unix_time_ms();

	return 0;
check_mode_error:
	return -1;
}

int session_req_afl_handler(session_t *s)
{
	file_list_t *fl = (file_list_t *)s->flist;
	int ret, err;
	
	if(s->state != SESSION_STATE_RUN){
		goto req_afl_error;
	}

	if(s->run_state != SESSION_RUN_STATE_AFL){
		goto req_afl_error;
	}
	LOG_INFO(LOG_MOD_SESSION, "[S:%s] Processing All file list", s->remote_ip);

	NOTIFY_START(g_node->sd_notifier, s->remote_id);

	if(s->local_rep_mode == INITSYNC_MODE_TARGET)
		check_N_create_target_dir(fl);

	LOG_INFO(LOG_MOD_SESSION, "[S:%s] Generate All file list", s->remote_ip);
	ret = generate_all_file_list(fl);
	if(ret < 0){ /* error */
		if(g_stopFlag){
			LOG_INFO(LOG_MOD_SESSION, "Stop Generate All file list");
			
			s->run_state = SESSION_RUN_STATE_STOP;
			goto req_afl_end;
		}

		LOG_INFO(LOG_MOD_SESSION, "[S:%s] Generate All file list Fail", s->remote_ip);
		s->run_state = SESSION_RUN_STATE_FAIL;
		goto req_afl_error;
	}

	LOG_INFO(LOG_MOD_SESSION, "[S:%s] Send All file list", s->remote_ip);
	
	
	ret = send_ctrl_file(s, INITSYNC_CMD_AFL, &err);
	if(ret == -1){
		/* net error */
		LOG_INFO(LOG_MOD_SESSION, "[S:%s] AFL Send Fail %s" ,s->remote_ip);
		goto req_afl_error;
	}	

	LOG_INFO(LOG_MOD_SESSION, "[S:%s] Send All file Complete", s->remote_ip);

	s->run_state = SESSION_RUN_STATE_MFL;

req_afl_end:
	return 0;

req_afl_error:
	return -1;
}


int session_afl_handler(session_t *s)
{
	file_list_t *fl = (file_list_t *)s->flist;
	PSESSION_CTRL_FILE_HDR_T hdr;
	char *pos;
	DWORD err;

	DWORD written;
	int ret;

	if(s->state != SESSION_STATE_RUN){
		goto afl_error;
	}

	if(s->run_state != SESSION_RUN_STATE_AFL){
		goto afl_error;
	}

	pos = s->data;
	pos += SIZEOF_SESSION_CMD_HDR;

	hdr = (PSESSION_CTRL_FILE_HDR_T)pos;
	pos += SIZEOF_SESSION_CTRL_FILE_HDR;

	if(s->local_rep_mode == INITSYNC_MODE_UNKNOWN){ 
		if(hdr->seq == 0){
			LOG_INFO(LOG_MOD_SESSION, "[T:%s] Recv remote all file list", s->remote_ip);
#ifdef _DEBUG
			printf("process AFL\n");
#endif
			NOTIFY_START(g_node->sd_notifier, s->remote_id);

			INITSYNC_FILE_CREATE(fl->hAFL, fl->all_files, err, afl_error);
		}
	}

	if(hdr->len > 0){
		INITSYNC_FILE_WRITE(fl->hAFL, pos, hdr->len, err, afl_error);	
	}else if(hdr->len == 0){
		// recv all
		SAFE_CLOSE_HANDLE(fl->hAFL);

		s->run_state = SESSION_RUN_STATE_MFL;

		ret = compare_file_list(fl);
		if(ret == 0){
			s->run_state = SESSION_RUN_STATE_READY;
		}else{ /* fail */
			if(g_stopFlag){
				LOG_INFO(LOG_MOD_SESSION, "[T:%s] Get PAUSE/STOP CMD", s->remote_ip);
				s->run_state = SESSION_RUN_STATE_STOP;
				goto afl_error;
			}else{
				LOG_INFO(LOG_MOD_SESSION, "[T:%s] File list Compare Fail", s->remote_ip);
				s->run_state = SESSION_RUN_STATE_FAIL;
				goto afl_error;
			}
		}
		s->total_bytes = fl->total_size;
		s->total_files = fl->nr_files;

		/* Send Segment List */
		ret = send_ctrl_file(s, INITSYNC_CMD_SEGMENT_LIST, &err);
		if(ret == -1){
			/* net error */
			LOG_INFO(LOG_MOD_SESSION, "[T:%s] Segment Info Send Fail %s" ,s->remote_ip);
			goto afl_error;
		}	
	}

	return 0;
afl_error:
	return -1;
}


int	session_resume_handler(session_t *s)
{
	int ret, err;
	file_list_t *fl = (file_list_t *)s->flist;

	if(s->state != SESSION_STATE_RUN){
		goto error_out;
	}

	if(s->run_state != SESSION_RUN_STATE_RESUME){
		goto error_out;
	}

	/* resume */
	LOG_INFO(LOG_MOD_SESSION, "[S:%s] Recv RESUME CMD", s->remote_ip);

	ret = segment_mdp_get_cur_seg(fl, g_node->mdp_controller);
	if(ret < 0){
		LOG_INFO(LOG_MOD_SESSION, "[S:%s] Get Segment Info From mDP Fail", s->remote_ip);
		s->run_state = SESSION_RUN_STATE_FAIL;
		goto error_out;
	}

	ret = segment_mdp_resume(fl, g_node->mdp_controller);
	if(ret < 0){
		LOG_INFO(LOG_MOD_SESSION, "[S:%s] Send Resume msg to mDP Fail", s->remote_ip);
		s->run_state = SESSION_RUN_STATE_FAIL;
		goto error_out;
	}

	s->run_state = SESSION_RUN_STATE_READY;

	LOG_INFO(LOG_MOD_SESSION, "[S:%s] Send Target Ready", s->remote_ip);
	ret = send_cmd(s, INITSYNC_CMD_TARGET_READY, &err);
	if(ret == -1){ /* error */
		s->run_state = SESSION_RUN_STATE_FAIL;
		goto error_out;
	}

	s->run_state = SESSION_RUN_STATE_FILE;

	return 0;

error_out:
	return -1;
}

int session_segment_list_handler(session_t *s)
{
	file_list_t *fl = (file_list_t *)s->flist;
	PSESSION_CTRL_FILE_HDR_T hdr;
	char *pos;
	DWORD written;
	int ret, err;

	if(s->state != SESSION_STATE_RUN){
		goto segment_error;
	}

	if(s->run_state != SESSION_RUN_STATE_MFL){
		goto segment_error;
	}

	LOG_INFO(LOG_MOD_SESSION, "[T:%s] Processing segment list", s->remote_ip);

	pos = s->data;
	pos += SIZEOF_SESSION_CMD_HDR;

	hdr = (PSESSION_CTRL_FILE_HDR_T)pos;
	pos += SIZEOF_SESSION_CTRL_FILE_HDR;

	LOG_INFO(LOG_MOD_SESSION, "[S:%s] Recv segment list", s->remote_ip);

	if(s->local_rep_mode == INITSYNC_MODE_UNKNOWN){ 
		if(hdr->seq == 0){ // first
			INITSYNC_FILE_CREATE(fl->hSegmentList, fl->segment_files, err, segment_error);
		}
	}
	
	if(hdr->len > 0){
		INITSYNC_FILE_WRITE(fl->hSegmentList, pos, hdr->len, err, segment_error);
	}else if(hdr->len == 0){
		// recv all
		SAFE_CLOSE_HANDLE(fl->hSegmentList);

		ret = segment_read(fl);
		if(ret < 0){
			LOG_INFO(LOG_MOD_SESSION, "[S:%s] Get Segment Info Fail", s->remote_ip);
			s->run_state = SESSION_RUN_STATE_FAIL;
			goto segment_error;
		}

		segment_print(fl);

		ret = segment_mdp_set(fl, g_node->mdp_controller);
		if(ret < 0){
			LOG_INFO(LOG_MOD_SESSION, "[S:%s] Set Segment Info to mDP Fail", s->remote_ip);
			s->run_state = SESSION_RUN_STATE_FAIL;
			goto segment_error;
		}

		ret = segment_mdp_set_cur(fl, g_node->mdp_controller);
		if(ret < 0){
			LOG_INFO(LOG_MOD_SESSION, "[S:%s] Set Cur Segment Info to mDP Fail", s->remote_ip);
			s->run_state = SESSION_RUN_STATE_FAIL;
			goto segment_error;
		}

		s->run_state = SESSION_RUN_STATE_READY;

		LOG_INFO(LOG_MOD_SESSION, "[S:%s] Send Target Ready", s->remote_ip);
		ret = send_cmd(s, INITSYNC_CMD_TARGET_READY, &err);
		if(ret == -1){ /* error */
			s->run_state = SESSION_RUN_STATE_FAIL;
			goto segment_error;
		}

		s->run_state = SESSION_RUN_STATE_FILE;
	}

	return 0;

segment_error:
	return -1;
}

int session_target_ready_handler(session_t *s)
{
	int ret;

	LOG_INFO(LOG_MOD_SESSION, "[T:%s] Recv Target Ready", s->remote_ip);

	if(s->state != SESSION_STATE_RUN){
		goto ready_error;
	}

	if(s->run_state != SESSION_RUN_STATE_READY){
		goto ready_error;
	}

	/* Send Ready to mDP */
	ret = mdp_ctrl(g_node->mdp_controller, MDP_CTRL_MSG_READY, s->remote_id, 0, NULL);
	if(ret < 0){
		LOG_ERROR(LOG_MOD_SESSION, "[T:%s] mDP Communication fail", s->remote_ip);
		goto ready_error;
	}

	s->run_state = SESSION_RUN_STATE_FILE;

	// Create File Send Thread
	s->send_file_thread = (HANDLE)_beginthreadex( 
			NULL, 0, session_send_file_thread, s, 0, &(s->send_file_thread_id));

	return 0;

ready_error:
	return -1;
}

int session_file_handler(session_t *s)
{
	PSESSION_FILE_HDR_T hdr;
	PSEND_FILE_HDR_T fhdr;
	PSEND_FILE_DATA_HDR_T fbd;
	char *pos, *path, *secu_data;
	int err;

	DWORD written;
	int ret;

	if(s->state != SESSION_STATE_RUN){
		goto recv_file_error;
	}

	if(s->run_state != SESSION_RUN_STATE_FILE){
		goto recv_file_error;
	}
			
	pos = s->data;
	pos += SIZEOF_SESSION_CMD_HDR;

	hdr = (PSESSION_FILE_HDR_T)pos;
	pos += SIZEOF_SESSION_FILE_HDR;

	if(hdr->type == SESSION_FILE_HDR_TYPE_HDR){ // first
		fhdr = (PSEND_FILE_HDR_T)pos;
		pos += SIZEOF_SEND_FILE_HDR;
		
		s->dwCurAttribute = fhdr->File_attribute;
		s->cur_file_size = fhdr->filesize;

		path = malloc(fhdr->path_size + sizeof(TCHAR));

		memcpy(path, pos, fhdr->path_size);
		pos += fhdr->path_size;

		path[fhdr->path_size] = 0;//'\0';
#ifdef _UNICODE
		path[fhdr->path_size + 1] = 0;//'\0';
#endif
		s->cur_recv_path = (TCHAR *)path;
		if(s->local_rep_mode == INITSYNC_MODE_TARGET)
			s->cur_lr_recv_path = convert_path_to_local_rep_path((file_list_t *)s->flist, (TCHAR *)path);
		else
			s->cur_lr_recv_path = NULL;

		//LOG_INFO_DETAIL_W(LOG_MOD_SESSION, _T("Recv File path : %s") ,s->cur_recv_path);

		if(fhdr->modified_type != MODIFIED_TYPE_TO){
			/* recv security data */
			secu_data = malloc(fhdr->secu_len);
			if(!secu_data){
				LOG_ERROR(LOG_MOD_SESSION, "out of memory %s:%d", __FILE__, __LINE__);
				goto recv_file_error;
			}

			memcpy(secu_data, pos, fhdr->secu_len);
			pos += fhdr->secu_len;

			s->cur_security = secu_data;
			s->cur_secu_len = fhdr->secu_len;
			s->cur_secu_info = fhdr->secu_info;
		}
		/* Create File */
		if(s->local_rep_mode == INITSYNC_MODE_TARGET){
			s->hCurFile = generate_file(fhdr, s->cur_lr_recv_path, secu_data, &err);
		}else{
			s->hCurFile = generate_file(fhdr, s->cur_recv_path, secu_data, &err);
		}
		
		//SAFE_FREE(secu_data);

		
		if(s->hCurFile == INVALID_HANDLE_VALUE){
			if(err != ERROR_SUCCESS){  // && err != ERROR_ALREADY_EXISTS){
				LOG_ERROR(LOG_MOD_SESSION, "Create File or Directory Fail");
				goto recv_file_error;
			}
		}

		if(fhdr->modified_type != MODIFIED_TYPE_TO){
			if(fhdr->File_attribute & FILE_ATTRIBUTE_DIRECTORY){
				if(s->local_rep_mode == INITSYNC_MODE_TARGET)
					SetFileAttributes(s->cur_lr_recv_path, s->dwCurAttribute);
				else
					SetFileAttributes(s->cur_recv_path, s->dwCurAttribute);
				// check_segment
				if(!g_alone){
					ret = segment_check(s->flist, s->cur_recv_path);
					if(ret > 0){
						segment_mdp_set_cur(s->flist, g_node->mdp_controller);
					}
				}
				SAFE_FREE(s->cur_lr_recv_path);
				SAFE_FREE(s->cur_recv_path);
			}
		}else{
			/* TARGET Only */
			SAFE_FREE(s->cur_lr_recv_path);
			SAFE_FREE(s->cur_recv_path);
		}
	}else if(hdr->type == SESSION_FILE_HDR_TYPE_BODY){
		if(s->hCurFile == INVALID_HANDLE_VALUE){
			goto recv_file_out;
		}

		fbd = (PSEND_FILE_DATA_HDR_T)pos;
		pos += SIZEOF_SEND_FILE_DATA_HDR;

		if(fbd->length == 0){
			if(s->hCurFile != INVALID_HANDLE_VALUE){
				SetEndOfFile(s->hCurFile);

				SAFE_CLOSE_HANDLE(s->hCurFile);

				/* change Attribute */
				if(s->dwCurAttribute & FILE_ATTRIBUTE_READONLY){
					if(s->local_rep_mode == INITSYNC_MODE_TARGET)
						SetFileAttributes(s->cur_lr_recv_path, s->dwCurAttribute);
					else
						SetFileAttributes(s->cur_recv_path, s->dwCurAttribute);
				}

				/* Security information sync */
				if(s->local_rep_mode == INITSYNC_MODE_TARGET)
					ret = SetSecurity(s->cur_lr_recv_path, s->cur_security, s->cur_secu_len, s->cur_secu_info);
				else
					ret = SetSecurity(s->cur_recv_path, s->cur_security, s->cur_secu_len, s->cur_secu_info);

				if(ret < 0){
					if(s->local_rep_mode == INITSYNC_MODE_TARGET)
						LOG_ERROR_W(LOG_MOD_FILE, _T("Set Security [%s] Fail"), s->cur_lr_recv_path);
					else
						LOG_ERROR_W(LOG_MOD_FILE, _T("Set Security [%s] Fail"), s->cur_recv_path);
				}
			}

			// send ack
			s->ack_offset = hdr->ack_offset;
			s->ack_idx = hdr->ack_idx;
	
			ret = send_cmd(s, INITSYNC_CMD_FILE_ACK, &err);
			if(ret == -1){ /* error */
				s->run_state = SESSION_RUN_STATE_FAIL;
				goto recv_file_error;
			}

			// check_segment
			if(!g_alone){
				ret = segment_check(s->flist, s->cur_recv_path);
				if(ret > 0){
					segment_mdp_set_cur(s->flist, g_node->mdp_controller);
				}
			}

			SAFE_FREE(s->cur_lr_recv_path);
			SAFE_FREE(s->cur_recv_path);
			SAFE_FREE(s->cur_security);
		}else if(fbd->length == IGNORE_FILE_LEN){
			if(s->hCurFile != INVALID_HANDLE_VALUE){
				SetEndOfFile(s->hCurFile);
				
				SAFE_CLOSE_HANDLE(s->hCurFile);
			}

			if(s->local_rep_mode == INITSYNC_MODE_TARGET){
				DeleteFile(s->cur_lr_recv_path);
			}else{
				DeleteFile(s->cur_recv_path);
			}
			
			SAFE_FREE(s->cur_lr_recv_path);
			SAFE_FREE(s->cur_recv_path);
			SAFE_FREE(s->cur_security);
		}else{
			INITSYNC_FILE_WRITE(s->hCurFile, pos, fbd->length, err, recv_file_error);
		}
	}

recv_file_out:
	return 0;

recv_file_error:
	session_error_process(s, err);

	SAFE_FREE(s->cur_lr_recv_path);
	SAFE_FREE(s->cur_recv_path);
	SAFE_FREE(s->cur_security);
	SAFE_CLOSE_HANDLE(s->hCurFile);
	return -1;
}

int session_file_ack_handler(session_t *s)
{
	PSESSION_FILE_ACK_HDR_T hdr;
	file_list_t *fl = (file_list_t *)s->flist;
	char *pos;
	int ret;

	pos = s->data;
	pos += SIZEOF_SESSION_CMD_HDR;

	hdr = (PSESSION_FILE_ACK_HDR_T)pos;
	pos += SIZEOF_SESSION_FILE_ACK_HDR;

	/* FILE ACK */
	/* Send file 성능 향상을 위해 file ack 는 recv Th에서 처리한다. */
	if(fl->hHistory == INVALID_HANDLE_VALUE){
		ret = create_hitory_mmap(fl);
		if(ret < 0){
			s->run_state = SESSION_RUN_STATE_FAIL;
			goto file_ack_error;
		}
	}

	/* history update */
	write_history(fl, hdr->ack_offset, hdr->ack_idx, hdr->file_size);


	/* notify to SD */
	/*
	if(s->total_bytes > s->sent_bytes)
	{
		NOTIFY_PROGRESS(g_node->sd_notifier, s->remote_id, s->total_bytes, s->sent_bytes, 0);
	}else{
		NOTIFY_PROGRESS(g_node->sd_notifier, s->remote_id, s->total_bytes, s->total_bytes - 10, 0);
	}
	*/

	//LOG_DEBUG(LOG_MOD_SESSION, "Recv Ack offset : %llu, idx : %llu - (%llu bytes send/%llu bytes)", hdr->ack_offset, hdr->ack_idx, s->sent_bytes, s->total_bytes);

	return 0;

file_ack_error:
	//session_crash(s);
	//SetEvent(s->destroy_recv_th_event);
	return -1;
}	

int session_file_end_handler(session_t *s)
{
	int ret, err;
	file_list_t *fl = (file_list_t *)s->flist;
	uint64 end;

	LOG_DEBUG(LOG_MOD_SESSION, "Recv File Send END");

	if(g_node->mode == INITSYNC_MODE_SOURCE
		|| g_node->mode == INITSYNC_MODE_LOCAL_REPLICATION && s->local_rep_mode == INITSYNC_MODE_SOURCE){
		
		close_history_mmap(fl);

		NOTIFY_PROGRESS(g_node->sd_notifier, s->remote_id, s->total_bytes, s->total_bytes, 1);

		NOTIFY_COMPLETE(g_node->sd_notifier, s->remote_id);

		/* Send Complete to mDP */
		if(s->local_rep_mode == INITSYNC_MODE_UNKNOWN){
			ret = mdp_ctrl(g_node->mdp_controller, MDP_CTRL_MSG_COMPLETE, s->remote_id, 0, NULL);
			if(ret < 0){
				LOG_ERROR(LOG_MOD_SESSION, "[T:%s] mDP Communication fail", s->remote_ip);
				return -1;
			}
		}

		end = get_unix_time_ms();
		s->total_duration = end - s->total_duration;
		s->send_duration = end - s->send_duration;

		LOG_INFO(LOG_MOD_SESSION, "Sync Session [%s] Complete\nSent Bytes : %llu bytes, Total duration : %llu ms, Sending duration : %llu ms, Speed : %f MB/s", 
			s->remote_ip, s->sent_bytes, s->total_duration, s->send_duration, (s->sent_bytes/(float)POW_2_20)/(s->total_duration/(float)1000));

		s->run_state = SESSION_RUN_STATE_COMPLETE;

		/* source 는 ack 를 받으면 session을 close 한다 */
		/* target 은 source 가 close 할때까지 기다린다 */
		s->state = SESSION_STATE_TERM;

	}else if(g_node->mode == INITSYNC_MODE_TARGET
		|| g_node->mode == INITSYNC_MODE_LOCAL_REPLICATION && s->local_rep_mode == INITSYNC_MODE_TARGET){
		LOG_INFO(LOG_MOD_SESSION, "[S:%s] Recv files END", s->remote_ip);
		ret = send_cmd(s, INITSYNC_CMD_FILE_END, &err);
		if(ret == -1){ /* error */
			s->run_state = SESSION_RUN_STATE_FAIL;
		}

		s->run_state = SESSION_RUN_STATE_COMPLETE;

		LOG_INFO(LOG_MOD_SESSION, "[S:%s] Session Sync Complete", s->remote_ip);

		if(g_node->mode != INITSYNC_MODE_LOCAL_REPLICATION)
			NOTIFY_COMPLETE(g_node->sd_notifier, s->remote_id);

		/* Send Complete to mDP */
		ret = mdp_ctrl(g_node->mdp_controller, MDP_CTRL_MSG_COMPLETE, s->remote_id, 0, NULL);
		if(ret < 0){
			LOG_ERROR(LOG_MOD_SESSION, "[S:%s] mDP Communication fail", s->remote_ip);
			return -1;
		}
	}

	return 0;
}


int session_complete_handler(session_t *s)
{
	return 0;
}

int session_pause_handler(session_t *s)
{
	file_list_t *fl = (file_list_t *)s->flist;
	int ret, err;

	if(g_node->mode == INITSYNC_MODE_SOURCE 
		|| g_node->mode == INITSYNC_MODE_LOCAL_REPLICATION && s->local_rep_mode == INITSYNC_MODE_SOURCE){
		LOG_INFO(LOG_MOD_SESSION, "[T:%s] Recv PAUSE ACK", s->remote_ip);

		/* source 는 ack 를 받으면 session을 close 한다 */
		/* target 은 source 가 close 할때까지 기다린다 */
		s->state = SESSION_STATE_TERM;
		g_node->status = NODE_STATUS_PAUSE;

		close_history_mmap(fl);

		LOG_INFO(LOG_MOD_SESSION, "Session send ctrl PAUSE to mDP");
		if(s->local_rep_mode == INITSYNC_MODE_UNKNOWN){
			mdp_ctrl_pause(g_node->mdp_controller, s->remote_id, -1, NULL);
		}

		NOTIFY_PAUSE(g_node->sd_notifier, s->remote_id, fl->total_size, fl->sent_size);
	}else if(g_node->mode == INITSYNC_MODE_TARGET
		|| g_node->mode == INITSYNC_MODE_LOCAL_REPLICATION && s->local_rep_mode == INITSYNC_MODE_TARGET){
		LOG_INFO(LOG_MOD_SESSION, "[S:%s] Session Sync PAUSE", s->remote_ip);

		if(s->hCurFile != INVALID_HANDLE_VALUE){
			SAFE_CLOSE_HANDLE(s->hCurFile);

			if(s->cur_recv_path){
				if(s->local_rep_mode == INITSYNC_MODE_TARGET){
					DeleteFile(s->cur_lr_recv_path);
				}else{
					DeleteFile(s->cur_recv_path);
				}
				SAFE_FREE(s->cur_lr_recv_path);
				SAFE_FREE(s->cur_recv_path);
			}
		}

		LOG_INFO(LOG_MOD_SESSION, "Session send ctrl PAUSE to mDP");
		segment_mdp_pause((file_list_t *)s->flist, g_node->mdp_controller);

		LOG_INFO(LOG_MOD_SESSION, "[S:%s] Send PAUSE Ack to REMOTE Initsync", s->remote_ip);
		ret = send_cmd(s, INITSYNC_CMD_PAUSE, &err);
		if(ret == -1){ /* error */
			s->run_state = SESSION_RUN_STATE_FAIL;
			return -1;
		}

		s->run_state = SESSION_RUN_STATE_PAUSE;
		g_node->status = NODE_STATUS_PAUSE;
	}

	return 0;
}

int	session_pause_immediate_handler(session_t *s)
{
	return session_pause_handler(s);
}

int session_stop_handler(session_t *s)
{
	int ret, err;

	if(g_node->mode == INITSYNC_MODE_SOURCE 
		|| g_node->mode == INITSYNC_MODE_LOCAL_REPLICATION && s->local_rep_mode == INITSYNC_MODE_SOURCE){
		LOG_INFO(LOG_MOD_SESSION, "[T:%s] Recv STOP ACK", s->remote_ip);
		
		/* source 는 ack 를 받으면 session을 close 한다 */
		/* target 은 source 가 close 할때까지 기다린다 */
		s->state = SESSION_STATE_TERM;

		LOG_INFO(LOG_MOD_SESSION, "Session send ctrl STOP to mDP");
		if(s->local_rep_mode == INITSYNC_MODE_UNKNOWN){
			mdp_ctrl_stop(g_node->mdp_controller, s->remote_id, -1, NULL, ((g_stopFlag & SD_CMD_STOP_WITH_MDP)?1:0));
		}

		NOTIFY_STOP(g_node->sd_notifier, s->remote_id);
	}else if(g_node->mode == INITSYNC_MODE_TARGET
		|| g_node->mode == INITSYNC_MODE_LOCAL_REPLICATION && s->local_rep_mode == INITSYNC_MODE_TARGET){
		LOG_INFO(LOG_MOD_SESSION, "[S:%s] Session Sync STOP", s->remote_ip);

		if(s->hCurFile != INVALID_HANDLE_VALUE){
			SAFE_CLOSE_HANDLE(s->hCurFile);

			if(s->cur_recv_path){
				if(s->local_rep_mode == INITSYNC_MODE_TARGET){
					DeleteFile(s->cur_lr_recv_path);
				}else{
					DeleteFile(s->cur_recv_path);
				}
				SAFE_FREE(s->cur_lr_recv_path);
				SAFE_FREE(s->cur_recv_path);
			}
		}

		segment_mdp_stop((file_list_t *)s->flist, g_node->mdp_controller, ((g_stopFlag & SD_CMD_STOP_WITH_MDP)?1:0));

		ret = send_cmd(s, INITSYNC_CMD_STOP, &err);
		if(ret == -1){ /* error */
			s->run_state = SESSION_RUN_STATE_FAIL;
			return -1;
		}

		s->run_state = SESSION_RUN_STATE_STOP;
		g_stopFlag = SD_CMD_STOP;
	}

	return 0;
}

int session_stop_with_mdp_handler(session_t *s)
{
	g_stopFlag = SD_CMD_STOP_WITH_MDP;
	
	return session_stop_handler(s);
}

int session_fail_process(session_t *s)
{
	int ret, err;

	if(g_node->mode == INITSYNC_MODE_TARGET ||
		s->local_rep_mode == INITSYNC_MODE_TARGET){
		LOG_INFO(LOG_MOD_SESSION, "[S:%s] Session Sync Fail", s->remote_ip);

		if(s->hCurFile != INVALID_HANDLE_VALUE){
			SAFE_CLOSE_HANDLE(s->hCurFile);

			if(s->cur_recv_path){
				if(s->local_rep_mode == INITSYNC_MODE_TARGET){
					DeleteFile(s->cur_lr_recv_path);
				}else{
					DeleteFile(s->cur_recv_path);
				}
				SAFE_FREE(s->cur_lr_recv_path);
				SAFE_FREE(s->cur_recv_path);
			}
		}

		segment_mdp_stop((file_list_t *)s->flist, g_node->mdp_controller, 1);
	}

	ret = send_cmd(s, INITSYNC_CMD_FAIL, &err);
	if(ret == -1){ /* error */
		s->run_state = SESSION_RUN_STATE_FAIL;
		return -1;
	}

	if(g_node->mode == INITSYNC_MODE_TARGET ||
		s->local_rep_mode == INITSYNC_MODE_TARGET){
		s->run_state = SESSION_RUN_STATE_FAIL;
		g_stopFlag = SD_CMD_STOP;
	}else{
		s->run_state = SESSION_RUN_STATE_FAIL;
	}

	LOG_DEBUG(LOG_MOD_SESSION, "Send Fail");
	return ret;
}


int session_fail_handler(session_t *s)
{
	int ret, err;

	if(g_node->mode == INITSYNC_MODE_SOURCE 
		|| g_node->mode == INITSYNC_MODE_LOCAL_REPLICATION && s->local_rep_mode == INITSYNC_MODE_SOURCE){
		
		if(s->run_state == SESSION_RUN_STATE_FILE){
			s->run_state = SESSION_RUN_STATE_FAIL;
			g_stopFlag = SD_CMD_STOP_WITH_MDP;

			return 0;
		}else{
			if(s->run_state == SESSION_RUN_STATE_FAIL){
				LOG_INFO(LOG_MOD_SESSION, "[T:%s] Recv Fail ACK", s->remote_ip);
			}else{
				LOG_INFO(LOG_MOD_SESSION, "[T:%s] Recv Fail", s->remote_ip);
				s->run_state = SESSION_RUN_STATE_FAIL;
			}
		}

		/* source 는 session을 close 한다 */
		/* target 은 source 가 close 할때까지 기다린다 */
		s->state = SESSION_STATE_TERM;

		LOG_INFO(LOG_MOD_SESSION, "Session send ctrl STOP to mDP");
		if(s->local_rep_mode == INITSYNC_MODE_UNKNOWN){
			mdp_ctrl_stop(g_node->mdp_controller, s->remote_id, -1, NULL, 1);
		}

		NOTIFY_STOP(g_node->sd_notifier, s->remote_id);
	}else if(g_node->mode == INITSYNC_MODE_TARGET
		|| g_node->mode == INITSYNC_MODE_LOCAL_REPLICATION && s->local_rep_mode == INITSYNC_MODE_TARGET){
		LOG_INFO(LOG_MOD_SESSION, "[S:%s] Session Sync Fail", s->remote_ip);

		if(s->hCurFile != INVALID_HANDLE_VALUE){
			SAFE_CLOSE_HANDLE(s->hCurFile);

			if(s->cur_recv_path){
				if(s->local_rep_mode == INITSYNC_MODE_TARGET){
					DeleteFile(s->cur_lr_recv_path);
				}else{
					DeleteFile(s->cur_recv_path);
				}
				SAFE_FREE(s->cur_lr_recv_path);
				SAFE_FREE(s->cur_recv_path);
			}
		}

		segment_mdp_stop((file_list_t *)s->flist, g_node->mdp_controller, 1);

		ret = send_cmd(s, INITSYNC_CMD_FAIL, &err);
		if(ret == -1){ /* error */
			s->run_state = SESSION_RUN_STATE_FAIL;
			return -1;
		}

		s->run_state = SESSION_RUN_STATE_FAIL;
		g_stopFlag = SD_CMD_STOP;
	}

	return 0;
}

int session_error_process(session_t *s, int err)
{
	char *data, *pos;
	int size, len;

	PSESSION_CMD_HDR_T cmd_hdr;
	PSESSION_ERROR_HDR_T err_hdr;

	data = alloc_buf(s->buf_pool, &size);

	pos = data;

	cmd_hdr = (PSESSION_CMD_HDR_T)pos;
	pos += SIZEOF_SESSION_CMD_HDR;

	err_hdr = (PSESSION_ERROR_HDR_T)pos;
	pos += SIZEOF_SESSION_ERROR_HDR;

	cmd_hdr->type = INITSYNC_CMD_ERROR;

	err_hdr->state = s->run_state;
	err_hdr->err = err;
	err_hdr->ack_offset = s->ack_offset;
	err_hdr->ack_idx = s->ack_idx;
	err_hdr->length = 0;
	
	if(s->state == SESSION_RUN_STATE_FILE && s->cur_recv_path){
		err_hdr->length = _tcslen(s->cur_recv_path) * sizeof(TCHAR);
		memcpy(pos, s->cur_recv_path, err_hdr->length);
		pos += err_hdr->length;
	}

	cmd_hdr->length = SIZEOF_SESSION_ERROR_HDR + err_hdr->length;

	len = (int)(pos - data);
	session_send(s, data, len);

	return 0;
}

int	session_error_handler(session_t *s)
{
	PSESSION_ERROR_HDR_T err_hdr;
	char *pos;

	pos = s->data;

	err_hdr = (PSESSION_ERROR_HDR_T)pos;
	pos += SIZEOF_SESSION_ERROR_HDR;

	if(err_hdr->state == SESSION_RUN_STATE_FILE && err_hdr->length)
	{
		/* Notify Sync Fail File */
		NOTIFY_FAILED_FILE(g_node->sd_notifier, s->remote_id, err_hdr->err, pos);

		LOG_DEBUG(LOG_MOD_SESSION, "nOTIFY ok");

		/* Write Sync Fail File List */
		flist_sync_fail(s->flist, pos, NULL, err_hdr->err);
	}

	return 0;
}