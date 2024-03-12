#ifndef __HB_HB_H_
#define __HB_HB_H_

#include "crt_dbg.h"
#include "extern_c.h"
#ifdef _MT
#include <process.h>
#endif
#include <winsock2.h>

#define HB_MSG_CURRENT_CHANNEL (27)
#define HB_MSG_NETWORK_FAIL    (30)

typedef enum hb_state{
	HB_STATE_RESET,
	HB_STATE_SET,
	HB_STATE_TIMEOUT,
	HB_STATE_EXPIRED
}hb_state_t;

typedef struct hb_hdr{
	int nid;
	int cnid;
}hb_hdr_t;

typedef struct hb_channel{
	int cnid;
	char *ip;
	char *remote;
	int port;

	hb_hdr_t send_pkt;
	hb_hdr_t recv_pkt;
	int send_pos;
	int recv_pos;

	SOCKET sock;

	hb_state_t state;
	void *hb_sender;
	void *hb;
}hb_channel_t;

typedef struct hb{
	int nid;
	int nr_cn;
	hb_channel_t **cn;
	int curr_cnid;
	int hb_port;
	int interval;
	int timeout;
	HANDLE destroy_hb_th_event;

	void *dis;
}hb_t;

hb_t *hb_init(int nid, int nr_cn, char **locals, char **remote_ips, int port, int interval, int timeout);
void hb_send_handler(void *dis, void *handle, void *data, UINT32 timeout);
void hb_recv_handler(void *dis, SOCKET sock, void *data, int err);
void hb_check_handler(void *dis, void *handle, void *data, UINT32 timeout);
void hb_start(hb_t *hb);
void hb_fin(hb_t *hb);

#endif