#include "tinyos.h"
#include "kernel_socket.h"
#include "kernel_dev.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "util.h"

static file_ops socket_file_ops={
	.Open = NULL,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close
};


Fid_t sys_Socket(port_t port)
{
	if(port < NOPORT || port > MAX_PORT){
		return NOFILE;
	}
	
	FCB* fcb[1];
	Fid_t fid[1];

	fcb[0] = NULL;
	fid[0] = -1;

	if(1 == FCB_reserve(1, fid, fcb)){
		Socket_cb* scb = (Socket_cb*)xmalloc(sizeof(Socket_cb));

		if(scb == NULL){
			return NOFILE;
		}

		scb->fcb = fcb[0];

		scb->fcb->streamobj = scb;
		scb->fcb->streamfunc =& socket_file_ops;

		scb->type = SOCKET_UNBOUND;
		scb->port = port;
		scb->refcount = 0;

		return fid[0];
	}

	return NOFILE;
}

int sys_Listen(Fid_t sock)
{
	FCB* fcb = get_fcb(sock);
	if(fcb != NULL && sock >= 0 && sock <= MAX_FILEID){
		Socket_cb* scb = (Socket_cb*)fcb->streamobj;

		if(scb != NULL && scb->port != NOPORT && port_map[scb->port] == NULL && scb->type == SOCKET_UNBOUND){
			scb->type = SOCKET_LISTENER;
			scb->listener.req_available = COND_INIT;	
			port_map[scb->port] = scb;	

			rlnode_init(&(scb->listener.queue), NULL);

			return 0;
		}
	}
	return -1;
}

Fid_t sys_Accept(Fid_t lsock)
{
	FCB* fcb = get_fcb(lsock);

	if(fcb!=NULL && lsock >= 0 && lsock <= MAX_FILEID){ 

		Socket_cb* scb = (Socket_cb*)fcb->streamobj;

		if(scb != NULL && scb->type == SOCKET_LISTENER && port_map[scb->port] != NULL){ 

			scb->refcount++;

			while(is_rlist_empty(&(scb->listener.queue))){
				kernel_wait(&(scb->listener.req_available), SCHED_IO);
			}

			if(port_map[scb->port] == NULL){
				scb->refcount--;

    			if(scb->refcount < 0){
        			free(scb);
				}

				return NOFILE;
			}

			rlnode* rl_req_node = rlist_pop_front(&scb->listener.queue);
			REQ* req = rl_req_node->obj;
			Socket_cb* client = req->peer;

			Fid_t fcfid = sys_Socket(scb->port);

			if(fcfid == NOFILE){
				scb->refcount--;
        		kernel_signal(&(req->connected_cv));
        		if (scb->refcount < 0){
            		free(scb);
        		}
				return NOFILE;
			}

			FCB* fcb_server = get_fcb(fcfid);
			if (fcb_server == NULL || fcfid < 0 || fcfid > MAX_FILEID ){
       			return NOFILE;
   			}

			Socket_cb* server = fcb_server->streamobj;


			Pipe_cb* pipe1 = (Pipe_cb*)xmalloc(sizeof(Pipe_cb)); //write

			if(pipe1==NULL){
        		return NOFILE;
			}

			FCB* fcb1[2];			/* List of FCBs */

			//fcb1[0] = NULL;
			//fcb1[1] = NULL;

			pipe1->reader = fcb1[0];
			pipe1->writer = fcb1[1];
			pipe1->has_space = COND_INIT;
			pipe1->has_data = COND_INIT;
			pipe1->r_position = 0;
			pipe1->w_position = 0;
			pipe1->buf_size = 0;

			Pipe_cb* pipe2 = (Pipe_cb*)xmalloc(sizeof(Pipe_cb)); //read

			if(pipe2==NULL){
        		return NOFILE;
			}

			FCB* fcb2[2];			/* List of FCBs */
			
			//fcb2[0] = NULL;
			//fcb2[1] = NULL;

			pipe2->reader = fcb2[0];
			pipe2->writer = fcb2[1];
			pipe2->has_space = COND_INIT;
			pipe2->has_data = COND_INIT;
			pipe2->r_position = 0;
			pipe2->w_position = 0;
			pipe2->buf_size = 0;

			if(pipe1 == NULL || pipe2 == NULL){
				return NOFILE;
			}

			server->type = SOCKET_PEER;
			server->peer.write_pipe = pipe1;
			server->peer.read_pipe = pipe2;
			server->peer.peer = client;

			client->type = SOCKET_PEER;
			client->peer.write_pipe = pipe2;
			client->peer.read_pipe = pipe1;
			client->peer.peer = server;		

			req->admited = 1;

			scb->refcount--;

    		if(scb->refcount < 0){
        		free(scb);
			}
			kernel_signal(&(req->connected_cv));

			return fcfid;
		}
	}
	return NOFILE;
}

int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	FCB* fcb = get_fcb(sock);

	if(fcb != NULL && sock >= 0 && sock <= MAX_FILEID && (port >= 1 && port <= MAX_PORT)){ //port > NOPORT

		Socket_cb* client = fcb->streamobj;

		if(client == NULL || client->type != SOCKET_UNBOUND){
			return -1;
		}

		Socket_cb* listener = port_map[port];

		if(listener != NULL && listener->type == SOCKET_LISTENER){

			REQ* req = (REQ*)xmalloc(sizeof(REQ));
			req->peer = client;
			rlnode_init(&(req->queue_node),req); 
			req->connected_cv = COND_INIT;
			req->admited = 0;	

			rlist_push_back(&(listener->listener.queue), &(req->queue_node));
			kernel_signal(&(listener->listener.req_available));

			client->refcount++;

			if(timeout > 0) {
        		kernel_timedwait(&(req->connected_cv), SCHED_IO, timeout);
   			}else{
       			kernel_wait(&(req->connected_cv), SCHED_IO);
   			}

			client->refcount--;

			if(client->refcount < 0){
				free(client);
			}
    		
			if(req->admited == 1){
				rlist_remove(&(req->queue_node));
				free(req);
				return 0;
			}else{
				rlist_remove(&(req->queue_node));
				free(req);
				return -1;
				
			}
		}
	}

	return -1;
}
int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	FCB* fcb = get_fcb(sock);

	if(fcb != NULL && sock >= 0 && sock <= MAX_FILEID){

		Socket_cb* scb = fcb->streamobj;

		if(scb != NULL && scb->type == SOCKET_PEER){
			switch(how){

				case SHUTDOWN_READ:
					pipe_reader_close(scb->peer.read_pipe);
					scb->peer.read_pipe = NULL;
					break;

				case SHUTDOWN_WRITE:
					pipe_writer_close(scb->peer.write_pipe);
					scb->peer.write_pipe = NULL;
					break;

				case SHUTDOWN_BOTH:
					pipe_reader_close(scb->peer.read_pipe);
					pipe_writer_close(scb->peer.write_pipe);
					scb->peer.read_pipe = NULL;
					scb->peer.write_pipe = NULL;
					break;

				default:
					assert(0);
			}
			return 0;
		}
	}
	return -1;
}

int socket_close(void* fid){
    if(fid==NULL)
        return -1;

    Socket_cb* scb = (Socket_cb*) fid;

    switch (scb->type){
        case SOCKET_PEER:
            pipe_writer_close(scb->peer.write_pipe);
            pipe_reader_close(scb->peer.read_pipe);
            break;
        case SOCKET_LISTENER:
            while(!is_rlist_empty(&(scb->listener.queue))){
                rlnode_ptr node = rlist_pop_front(&(scb->listener.queue));
                free(node);
            }
            kernel_broadcast(&(scb->listener.req_available));
            port_map[scb->port] = NULL;
            break;
        case SOCKET_UNBOUND:
            break;
        default:
            assert(0);
    }

    scb->refcount--;

    if(scb->refcount < 0){
        free(scb);
    }

    return 0;
}

int socket_read(void* socket, char* buf, unsigned int size){
	Socket_cb* scb = (Socket_cb*) socket;

	if(scb == NULL || scb->type != SOCKET_PEER || scb->peer.read_pipe == NULL){
		return -1;
	}
	
	return pipe_read(scb->peer.read_pipe, buf, size);
}

int socket_write(void* socket, const char* buf, unsigned int size){
	Socket_cb* scb = (Socket_cb*)socket;

	if(scb == NULL || scb->type != SOCKET_PEER || scb->peer.write_pipe == NULL){
		return -1;
	}
	
	return pipe_write(scb->peer.write_pipe, buf, size);
}

