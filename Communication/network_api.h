#ifndef __COMMUNICATION_NETWORK_API_H_
#define __COMMUNICATION_NETWORK_API_H_

#include "crt_dbg.h"
#include "extern_c.h"
#include "type_defines.h"

EXTERN_C_START
int init_network_api(void);
int destroy_network_api(void);
SOCKET net_tcp_socket(void);
SOCKET net_udp_socket(void);
int net_closesocket(SOCKET sock);
int net_bind(SOCKET sock, char *addr, uint16 port);
int net_listen(SOCKET sock);
SOCKET net_accept(SOCKET sock, struct sockaddr_in *cliaddr);
int net_connect(SOCKET sock, char *addr, uint16 port);
int net_send(SOCKET s, const void *buf, size_t len);
int net_recv(SOCKET s, void *buf, size_t len);
int net_sendto(SOCKET s, const void *buf, size_t len, struct sockaddr_in *sa, char *addr, uint16 port) ;
int net_recvfrom(SOCKET s, void *buf, size_t len);
int net_getsockopt(SOCKET s, int level, int optname, char *optval, int *optlen);
int net_setsockopt(SOCKET s, int level, int optname, const char *optval, int optlen);
int net_set_reuseaddr(SOCKET sock);
int net_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, int timeout);
int net_set_nonblock(SOCKET sock);
int net_set_block(SOCKET sock);
int net_get_sendbuf_size(SOCKET sock, int *size);
int net_set_sendbuf_size(SOCKET sock, int size);
int net_get_recvbuf_size(SOCKET sock, int *size);
int net_set_recvbuf_size(SOCKET sock, int size);
int net_set_keepalive(SOCKET s);
void net_get_peer_ip(SOCKET s, char *);
EXTERN_C_END

#endif
