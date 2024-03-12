#include "hb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "NetworkEventHandler.h"
#include "network_api.h"
#include "node.h"
#include "report.h"

int hb_cn_regist(void *dis, hb_channel_t *cn){
	int ret, err;
	hb_t *hb = cn->hb;

	cn->sock = net_udp_socket();

	ret = net_bind(cn->sock, cn->ip, cn->port);
	if (ret == SOCKET_ERROR) {
		err = GetLastError();
		LOG_ERROR(LOG_MOD_MAIN, "Bind Fail %s:%d, err : %d", cn->ip, cn->port, err);
		return -1;
	}

	ret = net_set_reuseaddr(cn->sock);
	if (ret < 0) {
		LOG_ERROR(LOG_MOD_ALL, "set reuseaddr fail");
		return -1;
	}

	ret = net_set_nonblock(cn->sock);
	if (ret < 0) {
		LOG_ERROR(LOG_MOD_ALL, "set nonblock fail");
		return -1;
	}

	recv_regist(dis, cn->sock, cn, hb_recv_handler);
	cn->hb_sender = time_event_set_timeout(dis, hb->interval, cn, hb_send_handler);

	return 0;
}

int hb_cn_unregist(void *dis, hb_channel_t *cn){
	recv_unregist(dis, cn->sock);
	
	if(cn->hb_sender)
		time_event_del_timer(dis, (time_event_t *)cn->hb_sender);
	
	cn->hb_sender = NULL;

	cn->recv_pos = 0;
	cn->send_pos = 0;

	return 0;
}

void hb_update(hb_t *hb, hb_channel_t *cn){
	//LOG_DEBUG(LOG_MOD_MAIN, "Channel [%d] Update", cn->cnid);

	cn->state = HB_STATE_SET;
}

void hb_expired(hb_t *hb, hb_channel_t *cn){
	int i;
	cn->state = HB_STATE_EXPIRED;

	LOG_DEBUG(LOG_MOD_MAIN, "Channel [%d] Expired", cn->cnid);

	if(hb->curr_cnid == cn->cnid){
		LOG_DEBUG(LOG_MOD_MAIN, "Current Channel [%d] Expired", cn->cnid);

		for(i = 0; i < hb->nr_cn; i++){
			if(hb->cn[i]->state < HB_STATE_TIMEOUT){
				hb->curr_cnid = hb->cn[i]->cnid;
				break;
			}
		}

		if(i != hb->nr_cn){
			LOG_DEBUG(LOG_MOD_MAIN, "Set Cur Channel [%d]", hb->curr_cnid);
			node_failover_process(HB_MSG_CURRENT_CHANNEL, hb->curr_cnid);
		}else{
			LOG_DEBUG(LOG_MOD_MAIN, "ALL Channel Fail");
			node_failover_process(HB_MSG_NETWORK_FAIL, -1);
		}
	}

	//hb_cn_unregist(hb->dis, cn);
	//net_closesocket(cn->sock);
	//hb_cn_regist(hb->dis, cn);
}

hb_t *hb_init(int nid, int nr_cn, char **locals, char **remote_ips, int port, int interval, int timeout){
	hb_t *hb;
	int i, ret;

	hb = (hb_t *)malloc(sizeof(hb_t));

	hb->dis = init_dispatcher(0);

	hb->nid = nid;
	hb->nr_cn = nr_cn;

	hb->hb_port = port;
	hb->interval = interval;
	hb->timeout = timeout;

	hb->curr_cnid = -1;

	hb->cn = (hb_channel_t **)malloc(sizeof(hb_channel_t *) * nr_cn);

	for(i = 0; i < nr_cn; i++){
		hb->cn[i] = (hb_channel_t *)malloc(sizeof(hb_channel_t));

		memset(hb->cn[i], 0, sizeof(hb_channel_t));

		hb->cn[i]->ip = (char *)malloc(strlen(locals[i]) + 1);
		strcpy(hb->cn[i]->ip, locals[i]);

		hb->cn[i]->remote = (char *)malloc(strlen(remote_ips[i]) + 1);
		strcpy(hb->cn[i]->remote, remote_ips[i]);

		hb->cn[i]->port = hb->hb_port;

		hb->cn[i]->cnid = i;

		hb->cn[i]->send_pkt.nid = nid;
		hb->cn[i]->send_pkt.cnid = i;

		hb->cn[i]->hb = hb;

		ret = hb_cn_regist(hb->dis, hb->cn[i]);
		if(ret < 0){
			SAFE_FREE(hb);
			return NULL;
		}
	}

	hb->destroy_hb_th_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	time_event_set_timer(hb->dis, hb->timeout, hb, hb_check_handler);

	return hb;
}

void hb_send_handler(void *dis, void *handle, void *data, UINT32 timeout){
	hb_channel_t *cn = (hb_channel_t *)data;
	hb_t *hb = cn->hb;
	int send_len;
	int ret;

	send_len = sizeof(hb_hdr_t) - cn->send_pos;
	//LOG_DEBUG(LOG_MOD_MAIN, "Send HB[%d] to %s", cn->cnid, cn->remote);
	ret = net_sendto(cn->sock, (char *)(&cn->send_pkt) + cn->send_pos, send_len, NULL, cn->remote, cn->port);
	if(ret > 0){
		cn->send_pos += ret;

		if(cn->send_pos == sizeof(hb_hdr_t)){
			cn->send_pos = 0;
			cn->hb_sender = time_event_set_timeout(dis, hb->interval, (void *)cn, hb_send_handler);
		}else{
			cn->hb_sender = time_event_set_timeout(dis, 10, (void *)cn, hb_send_handler);
		}
	}else{
		cn->hb_sender = time_event_set_timeout(dis, hb->interval, (void *)cn, hb_send_handler);
	}
}

void hb_recv_handler(void *dis, SOCKET sock, void *data, int err){
	hb_channel_t *cn = (hb_channel_t *)data;
	int recv_len;
	int ret;

	recv_len = sizeof(hb_hdr_t) - cn->recv_pos;
	ret = net_recvfrom(cn->sock, (char *)(&cn->recv_pkt) + cn->recv_pos, recv_len);
	if(ret > 0){
		cn->recv_pos += ret;

		if(cn->recv_pos == sizeof(hb_hdr_t)){
			hb_update(cn->hb, cn);

			cn->recv_pos = 0;
		}
	}else{
		int err = GetLastError();
		if(err!= WSAEWOULDBLOCK){
			//LOG_DEBUG(LOG_MOD_MAIN, "HB[%d] Recv Fail : %d",cn->cnid, err);
		}
	}
}

void hb_check_handler(void *dis, void *handle, void *data, UINT32 timeout){
	hb_t *hb = data;
	hb_channel_t *cn;
	int i;

	//LOG_DEBUG(LOG_MOD_MAIN, "Check HB");
	for(i = 0; i < hb->nr_cn; i++){
		cn = hb->cn[i];

		if(cn->state == HB_STATE_SET){
			cn->state = HB_STATE_RESET;
		}else if(cn->state == HB_STATE_RESET){
			cn->state = HB_STATE_TIMEOUT;
		}else if(cn->state == HB_STATE_TIMEOUT){
			cn->state = HB_STATE_EXPIRED;
			hb_expired(hb, cn);
		}
	}

	/* Set First current nid */
	if(hb->curr_cnid == -1){
		for(i = 0; i < hb->nr_cn; i++){
			if(hb->cn[i]->state < HB_STATE_TIMEOUT){
				hb->curr_cnid = hb->cn[i]->cnid;
				break;
			}
		}

		if(i != hb->nr_cn){
			LOG_DEBUG(LOG_MOD_MAIN, "Set First Cur Channel [%d]", hb->curr_cnid);
			node_failover_process(HB_MSG_CURRENT_CHANNEL, hb->curr_cnid);
		}	
	}
}

unsigned int WINAPI hb_thread(LPVOID arg)
{
	hb_t *hb = (hb_t *)arg;

	start_dispatcher(hb->dis);

	SetEvent(hb->destroy_hb_th_event);

	return 0;
}

void hb_start(hb_t *hb)
{
	HANDLE hb_th;
	DWORD  hb_th_id;

	hb_th = (HANDLE)_beginthreadex(NULL, 0, hb_thread, hb, 0, &(hb_th_id));
}

void hb_fin(hb_t *hb){
	dispatcher_t *dis = hb->dis;
	int i;

	dis->exit = 1;

	WaitForSingleObject(hb->destroy_hb_th_event, INFINITE);

	for(i = 0; i < hb->nr_cn; i++){
		hb_cn_unregist(dis, hb->cn[i]);

		SAFE_FREE(hb->cn[i]->ip);
		SAFE_FREE(hb->cn[i]->remote);

		SAFE_FREE(hb->cn[i]);
	}
	SAFE_FREE(hb->cn);

	CloseHandle(hb->destroy_hb_th_event);

	SAFE_FREE(hb);
}