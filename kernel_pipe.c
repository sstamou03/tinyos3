
#include "tinyos.h"
#include "kernel_dev.h"
#include "kernel_streams.h"
#include "kernel_cc.h"


// file operations for the read method 
 static file_ops reader_fops = {
  .Open = NULL ,
  .Read = pipe_read,
  .Write =  NULL, 
  .Close = pipe_reader_close
};

// file operations for the write method 
static file_ops writer_fops = {
  .Open = NULL,
  .Read = NULL,
  .Write =  pipe_write,
  .Close = pipe_writer_close
};


int sys_Pipe(pipe_t* pipe)
{
	Pipe_cb* pipe_cb = (Pipe_cb*)xmalloc(sizeof(Pipe_cb));

	if(pipe_cb == NULL)
		return -1;

	FCB* fcb[2]; //list of fcbs
	Fid_t fid[2]; //pointers to fcbs

	//intiliazation
	fcb[0] = NULL; //left limit
	fcb[1] = NULL; //right limit

	
	if(FCB_reserve(2, fid, fcb) == 1){

		pipe->read = fid[0];
		pipe->write = fid[1];

		pipe_cb->reader = fcb[0];
		pipe_cb->writer = fcb[1];
		pipe_cb->has_space = COND_INIT;
		pipe_cb->has_data = COND_INIT;
		pipe_cb->w_position = 0;
		pipe_cb->r_position = 0;
		pipe_cb->buf_size = 0;


		fcb[0]->streamfunc = &reader_fops;
		fcb[1]->streamfunc = &writer_fops;
		fcb[0]->streamobj = pipe_cb;
		fcb[1]->streamobj = pipe_cb;

		return 0;

	}

	return -1;


}


int pipe_read(void* reader, char* buf,unsigned int size){

	Pipe_cb *pipe_cb = (Pipe_cb*) reader;

	unsigned int byte_counter = 0; // pipes that readed
	int rem_buf_size = 0; //remaing buffer size 
	int temp_buf_size = 0;//temporary buffer size 
	int final_counter = 0;


	//FALSE
	if(pipe_cb == NULL && pipe_cb->reader == NULL)
		return -1;

	if(pipe_cb->buf_size == 0 && pipe_cb->writer == NULL)
		return 0;

	while(byte_counter < size){

		//buffer is empty , we wake up pipes and we wait until writer to write
		while(pipe_cb->buf_size == 0 && pipe_cb->writer!= NULL){
			kernel_broadcast(&pipe_cb->has_space);
			kernel_wait(&pipe_cb->has_data, SCHED_PIPE);	 
		}

		if(pipe_cb->buf_size == 0) //buffer is empty after the write 
			return byte_counter;

		rem_buf_size = pipe_cb->buf_size;

		if((size - byte_counter) < rem_buf_size)
			temp_buf_size = (int) (size - byte_counter);
		else
			temp_buf_size = rem_buf_size;


		if(temp_buf_size < PIPE_BUFFER_SIZE - pipe_cb->r_position)
			final_counter = temp_buf_size;
		else
			final_counter = PIPE_BUFFER_SIZE - pipe_cb->r_position;

		memcpy(&(buf[byte_counter]), &(pipe_cb->BUFFER[pipe_cb->r_position]), final_counter); // copy from pipe_cb buffer to the given buffer

		byte_counter += final_counter;
		pipe_cb->buf_size -= final_counter;
		pipe_cb->r_position = (pipe_cb->r_position + final_counter) % PIPE_BUFFER_SIZE ; //we put the reader position to the next one 
	}

	kernel_broadcast(&pipe_cb->has_space);
	return byte_counter;

}

int pipe_write(void* writer, const char * buf, unsigned int size){

	Pipe_cb *pipe_cb = (Pipe_cb*) writer;

	unsigned int byte_counter = 0;
	int rem_buf_size = 0;
	int temp_buf_size = 0;
	int final_counter = 0;

	if(pipe_cb == NULL || pipe_cb->writer == NULL || pipe_cb->reader==NULL)
		return -1;


	while(byte_counter < size) {
        while (pipe_cb->buf_size == PIPE_BUFFER_SIZE && pipe_cb->reader != NULL) {
            kernel_broadcast(&pipe_cb->has_data);
            kernel_wait(&pipe_cb->has_space, SCHED_PIPE);
        }

        if (pipe_cb->writer == NULL)			/* If both writer and reader ends are closed, return copied bytes */ 
            return (int) byte_counter;
        

        rem_buf_size = PIPE_BUFFER_SIZE - pipe_cb->buf_size;

        if ((size - byte_counter) < rem_buf_size)
        	temp_buf_size = (int) size - byte_counter;
        else
        	temp_buf_size = rem_buf_size;
        

        if (temp_buf_size < PIPE_BUFFER_SIZE - pipe_cb->w_position)
        	final_counter = temp_buf_size;
        else
        	final_counter = PIPE_BUFFER_SIZE - pipe_cb->w_position;
        
        memcpy(&(pipe_cb->BUFFER[pipe_cb->w_position]), &(buf[byte_counter]), final_counter);

        byte_counter += final_counter;
        pipe_cb->buf_size += final_counter;
        pipe_cb->w_position = (pipe_cb->w_position + final_counter) % PIPE_BUFFER_SIZE;
   }

    kernel_broadcast(&pipe_cb->has_data);
    return (int) byte_counter;

}



int pipe_writer_close(void* fid) {
    Pipe_cb *pipe_cb = (Pipe_cb*)fid;

   if (pipe_cb == NULL)
   	return -1;

  pipe_cb->writer = NULL;

  if (pipe_cb->reader == NULL){
  	free(pipe_cb);
  	return 0;
  }else{
  	kernel_broadcast(&pipe_cb->has_data);
  	return 0;
  }
    
}

int pipe_reader_close(void* fid) {
    Pipe_cb  *pipe_cb = (Pipe_cb*)fid;

    if (pipe_cb == NULL)
    	return -1;
   
   pipe_cb->reader = NULL;

   if (pipe_cb->writer == NULL)
   {
   	 free(pipe_cb);
   	 return 0;
   }else{
   	kernel_broadcast(&pipe_cb->has_space);
   	return 0;
   }
 }