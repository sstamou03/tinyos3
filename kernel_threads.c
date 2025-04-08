#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"

void creating_process_thread()
{
  PTCB* ptcb = cur_thread()->ptcb;
  assert(ptcb != NULL);

  int exitval;

  Task call = ptcb->task;
  int argl = ptcb->argl;
  void* args = ptcb->args;

  exitval = call(argl,args);
  ThreadExit(exitval);
}

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  if(task != NULL){
    PTCB *ptcb;

    ptcb = (PTCB*)xmalloc(sizeof(PTCB));

    ptcb->task=task;
    ptcb->args=args;
    ptcb->argl=argl;
    ptcb->exited=0;
    ptcb->detached=0;
    ptcb->exit_cv=COND_INIT;
    ptcb->refcount=0;

    rlnode_init(& ptcb->ptcb_list_node, ptcb);
    CURPROC->thread_count++;
    
    
    rlist_push_back(&CURPROC->ptcb_list, &ptcb->ptcb_list_node);
    ptcb->tcb = spawn_thread(CURPROC, creating_process_thread);
    ptcb->tcb->ptcb=ptcb;
    wakeup(ptcb->tcb);                                           
    return (Tid_t)ptcb;
    
  } else{
    return NOTHREAD;
  }
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t)cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  rlnode* ptcb_node = rlist_find(&CURPROC->ptcb_list, (PTCB*)tid, NULL);  //find the running thrread

  if(ptcb_node == NULL)
   return -1;

  PTCB* ptcb = ptcb_node->ptcb;

  if(ptcb == NULL || ptcb->detached == 1 || tid == sys_ThreadSelf()) //ptcb does not exist or ptcb is not jooinable
    return -1;

  ptcb->refcount++; // increase joints PTCB_list

  while(ptcb->exited != 1 && ptcb->detached != 1){
    kernel_wait(&ptcb->exit_cv, SCHED_USER);        //thread ready to join, waits until (PTCB*)tid exits
  }

  ptcb->refcount--; // decrease reference counts

  if (ptcb->detached==1){   //ptcb is detached not joinable
    return -1;
  }

  if(exitval != NULL)
    *exitval=ptcb->exitval;         
//if ptcb references are 0  ptcb gets cleared
  if(ptcb->refcount == 0){
    rlist_remove(&ptcb->ptcb_list_node);
    free(ptcb);
  }
  return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  rlnode* ptcb_node = rlist_find(&CURPROC->ptcb_list, (PTCB*)tid, NULL);

  if(ptcb_node==NULL || ptcb_node->ptcb->exited == 1)
    return -1;

  ptcb_node->ptcb->detached = 1;
  kernel_broadcast(&ptcb_node->ptcb->exit_cv);

  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  PTCB* ptcb = (PTCB*) sys_ThreadSelf();
  ptcb->exitval = exitval;   
  ptcb->exited = 1;         // initialize 
  CURPROC->thread_count--; // make thread counter ready to use it (make it 1)
  kernel_broadcast(&ptcb->exit_cv);



  if(CURPROC->thread_count == 0){
    if (get_pid(CURPROC) != 1){            // sys_exit code here 

      /* Reparent any children of the exiting process to the 
       initial task */
    PCB* initpcb = get_pcb(1);
    while(!is_rlist_empty(&CURPROC->children_list)) {
      rlnode* child = rlist_pop_front(&CURPROC->children_list);
      child->pcb->parent = initpcb;
      rlist_push_front(& initpcb->children_list, child);
    }

    /* Add exited children to the initial task's exited list 
       and signal the initial task */
    if(!is_rlist_empty(&CURPROC->exited_list)) {
      rlist_append(& initpcb->exited_list, &CURPROC->exited_list);
      kernel_broadcast(& initpcb->child_exit);
    }

    /* Put me into my parent's exited list */
    rlist_push_front(&CURPROC->parent->exited_list, &CURPROC->exited_node);
    kernel_broadcast(&CURPROC->parent->child_exit);

  }

  assert(is_rlist_empty(&CURPROC->children_list));
  assert(is_rlist_empty(&CURPROC->exited_list));


  /* 
    Do all the other cleanup we want here, close files etc. 
   */

  /* Release the args data */
  if(CURPROC->args) {
    free(CURPROC->args);
    CURPROC->args = NULL;
  }

  /* Clean up FIDT */
  for(int i=0;i<MAX_FILEID;i++) {
    if(CURPROC->FIDT[i] != NULL) {
      FCB_decref(CURPROC->FIDT[i]);
      CURPROC->FIDT[i] = NULL;
    }
  }

  while(rlist_find(&CURPROC->ptcb_list, ptcb, NULL) != NULL){
      if (ptcb->refcount < 1){
        rlist_remove(&ptcb->ptcb_list_node);
        free(ptcb);
      }
    }

   /* Disconnect my main_thread */
  CURPROC->main_thread = NULL;

  /* Now, mark the process as exited. */
  CURPROC->pstate = ZOMBIE;
  }
  
  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
}

