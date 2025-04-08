#ifndef __KERNLE_SOCKET_H
#define __KERNLE_SOCKET_H

#include "tinyos.h"
#include "kernel_streams.h"



typedef struct socket_control_block Socket_cb;

typedef enum socket_type{

	SOCKET_LISTENER,
	SOCKET_UNBOUND,
	SOCKET_PEER
	
}socket_type;

struct listener_socket{
	rlnode queue;
	CondVar req_available;
};

struct unbound_socket
{
	rlnode unbound_s;
};

struct peer_socket
{
	Socket_cb* peer;
	Pipe_cb* write_pipe;
	Pipe_cb* read_pipe;
};

typedef struct socket_control_block{

	uint refcount;
	FCB* fcb;
	socket_type type;
	port_t port;

	union{
		struct listener_socket listener;
		struct unbound_socket unbound;
		struct peer_socket peer; 
	};

}Socket_cb;

Socket_cb* port_map[MAX_PORT+1] = {NULL};

typedef struct connection_request{

	int admited;
	Socket_cb* peer;

	CondVar connected_cv;
	rlnode queue_node;
}REQ;

int socket_read(void* socket, char* buf, unsigned int size);
int socket_write(void* socket, const char* buf, unsigned int size);
int socket_close(void* fid);


#endif