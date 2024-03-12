#include "sd_notifier.h"
#include "report.h"
#include <errno.h>
#include <process.h>
#include <stdio.h>
#include <tchar.h>
#include <stdlib.h>
#include "sd_cmd_cntl.h"

extern int g_stopFlag;

unsigned int WINAPI sd_notify_thread(LPVOID arg)
{
	sd_notifier_t *sn = (sd_notifier_t *)arg;
	char *data = NULL;
	DWORD written;
	int len, ret;
	KM_MESSAGE *km_msg;

	while(1){
		data = popQ(sn->msg_Q, &len, 1);
		if(len < 0){
			
			/* Cleanup */
			g_stopFlag = SD_CMD_STOP;

			LOG_ERROR(LOG_MOD_SD_CNTRL, "SD Notifier : PopQ Fail");
			goto out;
		}

		if(len > 0){
			km_msg = (KM_MESSAGE *)data;
			if(km_msg->type == SD_MSG_TYPE_CLOSE){
				LOG_INFO(LOG_MOD_SD_CNTRL, "SD Notifier Colse, Notifier Thread Exit");
				goto out;
			}

			ret = WriteFile(sn->hPipe, data, len, &written, NULL);
			if((!ret) || written == 0)
			{
				/* Cleanup */
				g_stopFlag = SD_CMD_STOP;

				LOG_ERROR(LOG_MOD_SD_CNTRL, "SD Notifier Pipe Fail. error code[%d]", GetLastError());

				goto out;
			}

			SAFE_FREE(data);
		}
	}

out:
	SAFE_FREE(data);
	SetEvent(sn->destroy_snder_event);
	return 0;
}

sd_notifier_t *init_sd_notifier(node_t *node)
{
	sd_notifier_t *sn;

	sn = malloc(sizeof(sd_notifier_t));
	if(!sn){
		LOG_ERROR(LOG_MOD_SD_CNTRL, "out of memory %s:%d", __FILE__, __LINE__);
		return NULL;
	}

	memset(sn, 0, sizeof(sd_notifier_t));

	sn->node = node;

	sn->msg_Q = init_queue(1024);
	if(!sn->msg_Q){
		LOG_ERROR(LOG_MOD_SD_CNTRL, "init sd notify msg queue fail");
		SAFE_FREE(sn);
		return NULL;
	}

	sn->hPipe = INVALID_HANDLE_VALUE;
	GET_SD_PIPE_NAME(sn->pipename, SEC_INITSYNC_KMEM_KEY_BASE + node->group_index * MAX_INITSYNC + node->initsync_id);

	LOG_INFO(LOG_MOD_SD_CNTRL, "Connect to SD : %s", sn->pipename);
	if(!WaitNamedPipeA(sn->pipename, 3000))
	{
		LOG_ERROR(LOG_MOD_SD_CNTRL, "No SD Notify Pipe");
		return NULL;
	}
	else
	{
		sn->hPipe  = CreateFileA(sn->pipename,
			GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, 0, NULL);
		if(sn->hPipe == INVALID_HANDLE_VALUE)
		{
			LOG_ERROR(LOG_MOD_SD_CNTRL, "SD-InitSync Pipe Create Fail");
			return NULL;
		}
	}

	/* create send thread */
	sn->destroy_snder_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(sn->destroy_snder_event == INVALID_HANDLE_VALUE){
		LOG_ERROR(LOG_MOD_SD_CNTRL, "SD notify Create Event Fail, error code : %d", errno);
		return NULL;
	}

	sn->sd_notify_snder = (HANDLE)_beginthreadex( 
		NULL, 0, sd_notify_thread, sn, 0, &(sn->sd_notify_snder_id));
	if(sn->sd_notify_snder == 0)
	{
		LOG_ERROR(LOG_MOD_SD_CNTRL, "SD notify thread create fail, error code : %d", errno);
		return NULL;
	}

	return sn;
}

void destroy_sd_notifier(sd_notifier_t *sn)
{
	KM_MESSAGE *km_msg;
	node_t *node;
	sd_notify_msg_t *msg;
	int ret;

	LOG_INFO(LOG_MOD_MAIN, "+- Destoroy SD Notifier -+");

	if(!sn)
		return;

	node = sn->node;
	
	km_msg = malloc(sizeof(KM_MESSAGE));

	memset((char *)km_msg, 0, sizeof(KM_MESSAGE));

	km_msg->type = SD_MSG_TYPE_CLOSE;

	pushQ(sn->msg_Q, (char *)km_msg, sizeof(KM_MESSAGE));//, QUEUE_ENTRY_DATA_TYPE_BINARY);

	//ret = WaitForSingleObject(sn->destroy_snder_event, 1000);
	ret = WaitForSingleObject(sn->destroy_snder_event, INFINITE);
	if(ret == WAIT_TIMEOUT){
		// if timeout
		TerminateThread(sn->sd_notify_snder, -1);
	}

	SAFE_CLOSE_HANDLE(sn->hPipe);
}

int notify_fail(sd_notifier_t *sn, int t_idx, int err, TCHAR *path)
{
	KM_MESSAGE *km_msg;
	node_t *node;
	sd_notify_msg_t *msg;
	int ret, len, path_len;
	char *pos;
	
	if(!sn)
		return 0;

	node = sn->node;
	
	path_len = _tcslen(path) * sizeof(TCHAR);
	len = sizeof(KM_MESSAGE) + sizeof(int) + path_len;

	km_msg = malloc(len);
	pos = (char *)km_msg;

	memset((char *)km_msg, 0, len);

	km_msg->type = SD_MSG_TYPE_FAIL;
	pos += sizeof(long);

	msg = (sd_notify_msg_t *)km_msg->msg;

	msg->group_index = node->group_index;
	msg->initsync_id = node->initsync_id;
	msg->state = SD_NOTIFY_STATE_FAILED_FILE;
	msg->target_index = t_idx;
	msg->total_size = 0;
	msg->sent_size = 0;

	pos = (char *)km_msg->msg + sizeof(sd_notify_msg_t);

	memcpy(pos, (char *)&path_len, sizeof(int));
	pos += sizeof(int);

	memcpy(pos, (char *)path, path_len);
	pos += path_len;

	ret = pushQ(sn->msg_Q, (char *)km_msg, len);//, QUEUE_ENTRY_DATA_TYPE_BINARY);

	return 0;
}

int notify(sd_notifier_t *sn, int status, int t_idx, uint64 total_size, uint64 sent_size, int reset)
{
	KM_MESSAGE *km_msg;
	node_t *node;
	sd_notify_msg_t *msg;
	int ret;
	
	if(!sn)
		return 0;

	node = sn->node;

	if(status == SD_NOTIFY_STATE_SENDING){
		if(reset){
			if(total_size > 0)
				LOG_INFO(LOG_MOD_SD_CNTRL, "[PROGRESS] sent : %llu bytes, total : %llu bytes, %llu %c", sn->noti_sent_size, total_size, (sn->noti_sent_size*100)/total_size, '%');
			else
				LOG_INFO(LOG_MOD_SD_CNTRL, "[PROGRESS] sent : %llu bytes, total : %llu bytes", sn->noti_sent_size, total_size);

			sn->noti_sent_size = sent_size;
		}else{
			if(sent_size > sn->noti_sent_size + (total_size/100) || total_size == sent_size){
				if(total_size > 0)
					LOG_INFO(LOG_MOD_SD_CNTRL, "[PROGRESS] sent : %llu bytes, total : %llu bytes, %llu %c", sn->noti_sent_size, total_size, (sn->noti_sent_size*100)/total_size, '%');
				else
					LOG_INFO(LOG_MOD_SD_CNTRL, "[PROGRESS] sent : %llu bytes, total : %llu bytes", sn->noti_sent_size, total_size);

				sn->noti_sent_size = sent_size;
				//LOG_DEBUG(LOG_MOD_SD_CNTRL, "Progress Noti after, noti_sent : %llu, sent : %llu", sn->noti_sent_size, sent_size);
			}else{
				return 0;
			}
		}
	}

	km_msg = malloc(sizeof(KM_MESSAGE));

	memset((char *)km_msg, 0, sizeof(KM_MESSAGE));

	if(status == SD_NOTIFY_STATE_SENDING){
		km_msg->type = SD_MSG_TYPE_PROGRESS;
	}else{
		km_msg->type = SD_MSG_TYPE_STATUS;
	}

	msg = (sd_notify_msg_t *)km_msg->msg;

	msg->group_index = node->group_index;
	msg->initsync_id = node->initsync_id;
	msg->state = status;
	msg->target_index = t_idx;
	msg->total_size = total_size;
	msg->sent_size = sent_size;


	if(msg->state != SD_NOTIFY_STATE_SENDING){
		if(msg->state != SD_NOTIFY_STATE_WAIT && msg->state != SD_NOTIFY_STATE_FINISH){
			LOG_INFO(LOG_MOD_SD_CNTRL, "Notify: GROUP[%d], STATUS[%d], TARGET[%d]", msg->group_index, msg->state, msg->target_index);
		}else{
			LOG_INFO(LOG_MOD_SD_CNTRL, "Notify: GROUP[%d], STATUS[%d]", msg->group_index, msg->state);
		}
	}

	ret = pushQ(sn->msg_Q, (char *)km_msg, sizeof(KM_MESSAGE));//, QUEUE_ENTRY_DATA_TYPE_BINARY);

	return 0;
}