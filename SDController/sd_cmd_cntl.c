#include "sd_cmd_cntl.h"
#include "sd_notifier.h"
#include "report.h"
#include "NetworkEventHandler.h"
#include "network_api.h"
#include <errno.h>
#include <stdio.h>

extern int g_stopFlag;
//extern int g_pauseFlag;

sd_cmd_cntl_t *init_sd_cmd_cntl(node_t *node)
{
	sd_cmd_cntl_t *c;

	c = malloc(sizeof(sd_cmd_cntl_t));
	if(!c){
		LOG_ERROR(LOG_MOD_SD_CNTRL, "out of memory %s:%d", __FILE__, __LINE__);
		return NULL;
	}

	memset(c, 0, sizeof(sd_cmd_cntl_t));

	c->node = node;

	return c;
}

void sd_cmd_handler(void *dis, SOCKET sock, void *data, int err)
{
	sd_cmd_cntl_t *c;
	node_t *node;
	int len, recvlen;
	int ret;

	c = data;
	node = c->node;

	if(sock == INVALID_SOCKET)
		return;

	recvlen = 0;

	len = sizeof(sd_cmd_msg_t);
	if(len > 0){
		while(recvlen < len){

			ret = net_recv(sock, (char *)(&c->cmd) + recvlen, len - recvlen);

			if(ret > 0){
				recvlen += ret;
				if(recvlen == len){
					/* process cmd */
					if(c->cmd.group_index != node->group_index){
						/* return fail */
						c->response.group_index = node->group_index;
						c->response.result = SD_CMD_FAIL;
					}else{
						LOG_INFO(LOG_MOD_SD_CNTRL, "Get CMD[%d] From SD", c->cmd.cmd);

						if(c->cmd.cmd & SD_CMD_RESUME){
							
							c->response.result = resume_node_handler(node);

						}else if(c->cmd.cmd & SD_CMD_PAUSE){
							
							c->response.result = pause_node_handler(node, c->cmd.cmd);

						}else if(c->cmd.cmd & SD_CMD_STOP){
							
							c->response.result = stop_node_handler(node, c->cmd.cmd);

						}else if(c->cmd.cmd & SD_CMD_IS_RUNNING){
							/* Fill result */
							if(g_stopFlag & SD_CMD_STOP)
								c->response.result = SD_CMD_FAIL;
							else{
								c->response.result = SD_CMD_SUCCESS;
								c->response.return_info = node->status;
							}

							LOG_DEBUG(LOG_MOD_SD_CNTRL,"Node State : %d", node->status);
						}

						c->response.group_index = node->group_index;
				
					}

					LOG_INFO(LOG_MOD_SD_CNTRL, "Send Response[%d] of CMD[%d] From SD", c->response.result, c->cmd.cmd);

					/* Send regist result */
					sd_send_response(sock,  c);
				}
			} else if (ret == 0) { // TCP FIN
				LOG_ERROR(LOG_MOD_SD_CNTRL, "recv FIN from SD");
				/* Clear cmd */
				memset(&c->cmd, 0, sizeof(c->cmd));
				memset(&c->response, 0, sizeof(c->response));

				recv_unregist(dis, sock);

				closesocket(sock);
				return;

			} else { // SOCKET_ERROR
				if (WSAGetLastError() != WSAEWOULDBLOCK) {	/* Peer TCP로부터 RST 수신 혹은 TCP Timeout */

					LOG_ERROR(LOG_MOD_SD_CNTRL, "RST or TCP Timeout from SD, error code : %d" , WSAGetLastError());
					
					/* Clear cmd */
					memset(&c->cmd, 0, sizeof(c->cmd));
					memset(&c->response, 0, sizeof(c->response));

					recv_unregist(dis, sock);

					closesocket(sock);
					return;
				}
			}
		}
	}
}

void sd_response_handler(void *dis, SOCKET sock, void *data, int err)
{
	sd_cmd_cntl_t *c;
	node_t *node;

	int ret;
	
	int len, sendlen;
	
	c = data;
	node = c->node;

	if(sock == INVALID_SOCKET)
		return;

	sendlen = 0;

	len = sizeof(sd_rspns_msg_t);
	if(len > 0){
		while(sendlen < len){
			ret = net_send(sock, (char *)(&c->response) + sendlen, len-sendlen);
			if(ret >= 0){
				sendlen += ret;
			}else{
				if (WSAGetLastError() != WSAEWOULDBLOCK) {	/* Peer TCP로부터 RST 수신 혹은 TCP Timeout */

					LOG_ERROR(LOG_MOD_SD_CNTRL, "RST or TCP Timeout from SD, error code : %d" ,WSAGetLastError());

					/* Clear cmd */
					memset(&c->cmd, 0, sizeof(c->cmd));
					memset(&c->response, 0, sizeof(c->response));

					recv_unregist(dis, sock);
					closesocket(sock);

					break;
				}
			}
		}
	}
}

int sd_send_response(SOCKET sock, void *data)
{
	sd_cmd_cntl_t *c;
	node_t *node;

	int ret;
	
	int len, sendlen;
	
	c = data;
	node = c->node;

	if(sock == INVALID_SOCKET)
		return -1;

	sendlen = 0;

	len = sizeof(sd_rspns_msg_t);
	if(len > 0){
		while(sendlen < len){
			ret = net_send(sock, (char *)(&c->response) + sendlen, len-sendlen);
			if(ret >= 0){
				sendlen += ret;
			}else{
				if (WSAGetLastError() != WSAEWOULDBLOCK) {	/* Peer TCP로부터 RST 수신 혹은 TCP Timeout */

					LOG_ERROR(LOG_MOD_SD_CNTRL, "RST or TCP Timeout from SD, error code : %d" ,WSAGetLastError());

					/* Clear cmd */
					memset(&c->cmd, 0, sizeof(c->cmd));
					memset(&c->response, 0, sizeof(c->response));

					closesocket(sock);

					break;
				}
			}
		}
	}

	return 0;
}

sd_cmd_rspns_t initsyncCmd(int index, char *remoteip, int port, sd_cmd_t cmd, int *return_info)
{
	struct sockaddr_in  address;
	SOCKET csock;

	int result;

	sd_cmd_msg_t cmd_msg;
	sd_rspns_msg_t rspns;


	address.sin_family       = AF_INET;
	address.sin_addr.s_addr  = inet_addr( remoteip );
	// connect another uDC server
	address.sin_port         = htons(  port  );
	csock                    = socket( AF_INET, SOCK_STREAM, 0 );
	//set_sock_option( csock );
	if( csock == INVALID_SOCKET )
	{
		LOG_ERROR(LOG_MOD_SD_CNTRL, "Can't create socket INITSYNC[%d]:%d", index, WSAGetLastError());
		return SD_CMD_FAIL;
	} 

#if   defined( _MDP_DEBUG_ )
	LOG_DEBUG("Try Connect to Initsync : %d", csock);
#endif
	result  = connect(csock, (struct sockaddr *)&address, sizeof(address));
	if( result == SOCKET_ERROR )
	{
#if   defined( _MDP_DEBUG_ )
		LOG_ERROR("Connect fail to Initsync (%s:%d, %d)",
			g_sd_config.local_ip, BIND_PORT_SD + 100 + index, WSAGetLastError());
#endif
		return SD_CMD_FAIL;
	}

	// Send cmd
	cmd_msg.group_index = index;
	cmd_msg.cmd = cmd;

	result = send(csock, (char *)&cmd_msg, sizeof(sd_cmd_msg_t), 0);
	if( result == SOCKET_ERROR )
	{
#if   defined( _MDP_DEBUG_ )
		LOG_ERROR("GUI message forward fail to %s : %d", g_sd_config.local_ip, WSAGetLastError());
#endif
		closesocket(csock);
		return SD_CMD_FAIL;
	}

	result = recv(csock, (char *)&rspns, sizeof(sd_rspns_msg_t), 0);
	if(result <= 0){
		closesocket(csock);
		return SD_CMD_FAIL;
	}

	closesocket(csock);

	if(rspns.group_index != index)
		return SD_CMD_FAIL;

	if(return_info)
		*return_info = rspns.return_info;

	return rspns.result;
}
