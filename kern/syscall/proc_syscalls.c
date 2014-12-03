#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
/******** New for A2  ************/
#include <mips/trapframe.h>
#include <kern/wait.h>
#include <kern/fcntl.h>
#include <synch.h>
#include <limits.h>
#include <vfs.h>
#include "opt-A2.h"
/*********************************/

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly. */


void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */

  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  #if OPT_A2

  struct lock* global_lock = get_global_lock();
  lock_acquire(global_lock);

  if(p->parent_pid == -1 || (parent_checker(p->parent_pid) != 1)) { // It does not have a parent !
      DEBUG(DB_EXEC, "It doesn't have a parent!\n");
      // Kill its exited children
      exited_children_cleanup(p->pid);
      // Just destroy this process because no one will care about this process any more !
      proc_destroy(p);
  }
  else { // Hold on! It still has a parent ! It loves its child !
      DEBUG(DB_EXEC, "It has a parent!\n");
      struct proc* parent = find_proc(p->parent_pid);
      // Yes! I have exited and I have set my exitcode !
      p->exit = 1;
      p->exitcode = _MKWAIT_EXIT(exitcode);

      DEBUG(DB_EXEC, "Wake up parent!\n");
      // Wait up its waiting parent!
      cv_signal(parent->wait_child, global_lock);
      // Kill its exited children
      exited_children_cleanup(p->pid);
  }
  lock_release(global_lock);
  #else
   /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
   proc_destroy(p);
  #endif
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  #if OPT_A2
    *retval = curproc->pid;
  #else 
    *retval = 1;
  #endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  DEBUG(DB_EXEC, "Start waitpid\n");

  if (options != 0) {
    return(EINVAL);
  }

 #if OPT_A2
  (void)exitstatus;

  // Acquire global lock first because we are touching global variable active_proc_list
  struct lock* global_lock = get_global_lock();
  lock_acquire(global_lock);

  // Find the process with pid
  struct proc* child = find_proc(pid);
  // Check if the pid exists or not
  if(child == NULL) {
    lock_release(global_lock);
    // No such process  
    DEBUG(DB_EXEC, "Child not exists!\n");
    return ESRCH;
  }
  // We haven't implemented the option WHOHANG, so just for "not interested" portion
  // A process can only be interested in its own child
  if(child->parent_pid != curproc->pid) {
    lock_release(global_lock);
    // No child processes 
    DEBUG(DB_EXEC, "It's not your child!\n");
    return ECHILD;
  }

  // Block the parent !
  DEBUG(DB_EXEC, "Parent goes to sleep. Wait for child!\n");
  while(child->exit != 1) {
     cv_wait(curproc->wait_child, global_lock);
  }

  DEBUG(DB_EXEC, "Get child exitcode!\n");
  result = copyout((void *)&(child->exitcode),status,sizeof(int));
  if(result) {
    lock_release(global_lock);
    return (result);
  }

  proc_destroy(child);
  lock_release(global_lock);
  
 #else 
   /* for now, just pretend the exitstatus is 0 */
   exitstatus = 0;
   result = copyout((void *)&exitstatus,status,sizeof(int));
   if (result) {
     return(result);
   }
  #endif
  *retval = pid;

  DEBUG(DB_EXEC, "Finish waitpid!\n");

  return(0);
}


int
sys_fork(struct trapframe* tf, pid_t* retval) 
{
   /* Make sure pointers are valid */
   KASSERT(tf != NULL);
   KASSERT(retval != NULL);

   int errno;
       
   DEBUG(DB_EXEC, "Start sys_fork\n");

   /* First check if the process number is already up to maximum */
   if(check_proc_limit()) {
       //  Too many processes in system     
       return ENPROC;
   }
   /* Create a fresh new process */
   struct proc* new_proc = proc_create_runprogram(curproc->p_name);
   if(new_proc == NULL) {
       // Out of memory
       return ENOMEM;
   }
  // Assign parent pid
   new_proc->parent_pid = curproc->pid;
   
   DEBUG(DB_EXEC, "Create process body\n");

    /* Create a new address space */
   errno = as_copy(curproc->p_addrspace, &new_proc->p_addrspace);
   if(errno != 0) {
       proc_destroy(new_proc);
       return errno;
   }
   
   DEBUG(DB_EXEC, "Create new address space\n");

   /* Create a new trapframe */
   struct trapframe* new_tf = (struct trapframe*)kmalloc(sizeof(struct trapframe));
   if(new_tf == NULL) {
       proc_destroy(new_proc);
       // Out of memory
       return ENOMEM;
   }
   memcpy(new_tf, tf, sizeof(struct trapframe));

   DEBUG(DB_EXEC, "Create new trapframe\n");

   /* Create new thread */
   errno = thread_fork(curthread->t_name, new_proc, enter_forked_process, (void*)new_tf, 0);
   if(errno != 0) {
       proc_destroy(new_proc);
       kfree(new_tf);
       return errno;
   }
   DEBUG(DB_EXEC, "sys_fork completed!\n");

   *retval = new_proc->pid;

   return 0;
}


int sys_execv(const char* program, char** args) {
     // KASSERT(program != NULL);
     // Probably it's better to return an error code
     if(program == NULL) {
        return ENOENT;
     }

     int errno;
     /* Check if it's a full path */
     int full_path = 0;
     const char* temp = program;
     while(*temp != '\0') {
       if(*temp == '/') {
         full_path = 1;
         break;
       }
       temp++;
     }
     /* Check if program name exceed the limit */
     if(full_path) {
        char* file_name = strrchr(program, '/');
        if(strlen(program) > PATH_MAX || strlen(file_name) - 1 > NAME_MAX) {
            return E2BIG;
        }
     }
     else {
        if(strlen(program) > NAME_MAX) {
            return E2BIG;
        }
     }

    /* Count the number of arguments */
     int arg = 0;
     while(args[arg] != NULL) {
         ++arg;
     }

     /* Allocate memory for kargs */
     char** kargs = kmalloc(sizeof(char*) * arg);
     if(kargs == NULL) {
        return ENOMEM;
     } 
     for (int i = 0; i < arg; i++) {
        kargs[i] = kmalloc(ARG_MAX_LEN * sizeof(char));
        if(kargs[i] == NULL) {
          /* Deallocate kargs */
          destroy_array(i, kargs);
          return ENOMEM;
        }
     }

     /* Copy arguments into the kernel */
     for(int i = 0; i < arg; i++) {
        errno = copyinstr((userptr_t)args[i], kargs[i], ARG_MAX_LEN, NULL);
        if(errno != 0) {
          /* Deallocate kargs */
          destroy_array(i, kargs);
          return errno;
        }
     }

     /* Copy program path into the kernel */
     char* kprogram = kmalloc(sizeof(char) * (strlen(program) + 1));
     if(kprogram == NULL) {
          /* Deallocate kargs */
          destroy_array(arg, kargs);
          return ENOMEM;
     }
     errno = copyinstr((userptr_t)program, kprogram, strlen(program) + 1, NULL);
     if(errno != 0) {
          /* Deallocate kargs */
          destroy_array(arg, kargs);
          kfree(kprogram);
          return errno;
     }
     
     ///////////////////////////////////////////
     struct addrspace *as;
     struct addrspace *old_as;
     struct vnode *v;
     vaddr_t entrypoint, stackptr, argv;

     /* Open the program file using vfs_open(program,...) */
     errno = vfs_open(kprogram, O_RDONLY, 0, &v);
     if (errno != 0) {
          /* Deallocate kargs */
          destroy_array(arg, kargs);
          kfree(kprogram);
          return errno;
     }

     /* kprogram is no longer needed */
     kfree(kprogram);

     /* Create a new address space. */
     as = as_create();
     if (as == NULL) {
        vfs_close(v);
        /* Deallocate kargs */
        destroy_array(arg, kargs);
        return ENOMEM;
     }

     /* Switch to it and activate it. */
     old_as = curproc_setas(as);
     as_activate();

     /* Load the executable. */
     errno = load_elf(v, &entrypoint);
     if (errno != 0) {
       /* p_addrspace will go away when curproc is destroyed */
       vfs_close(v);
       /* Deallocate kargs */
       destroy_array(arg, kargs);
       /* Restore the old process */
       curproc_setas(old_as);
       /* Activate it */
       as_activate();
       /* Destroy new address space */
       as_destroy(as);
       return errno;
     }

     /* Done with the file now. */
     vfs_close(v);

     /* Define the user stack in the address space */
     errno = as_define_stack(as, &stackptr);
     if (errno != 0) {
       /* p_addrspace will go away when curproc is destroyed */
       /* Deallocate kargs */
       destroy_array(arg, kargs);
       /* Restore the old process */
       curproc_setas(old_as);
       /* Activate it */
       as_activate();
       /* Destroy new address space */
       as_destroy(as);
       return errno;
     }
     
     vaddr_t user_space_pointer[1 + arg];
     
     /*   IMPORTANT !  */
     /* In the new process, argv[argc] must be NULL!!!! Thus, we have to assign user_space_pointer[arg] to be 0*/
     user_space_pointer[arg] = 0;

     /* Copy argument string to user space */
     for(int i = arg - 1; i >= 0; i--) {
         size_t len = strlen(kargs[i]);
         stackptr -= ROUNDUP((len + 1), 4);
         errno = copyoutstr(kargs[i], (userptr_t)stackptr, ARG_MAX_LEN, NULL);
         if(errno != 0) { // Bad things happened
            /* Deallocate kargs */
            destroy_array(arg, kargs);
            /* Restore the old process */
            curproc_setas(old_as);
            /* Activate it */
            as_activate();
            /* Destroy new address space */
            as_destroy(as);
            return errno;
         } 
         /* Fill user_space_pointer with actual user space pointer */
         user_space_pointer[i] = stackptr;
     }

     /* Copy argument pointer to user space */
     for(int i = arg; i >= 0; i--) {
        stackptr -= sizeof(vaddr_t);
        errno = copyout(&(user_space_pointer[i]), (userptr_t)stackptr, sizeof(vaddr_t));
        if(errno != 0) {
           /* Deallocate kargs */
           destroy_array(arg, kargs);
           /* Restore the old process */
           curproc_setas(old_as);
           /* Activate it */
           as_activate();
           /* Destroy new address space */
           as_destroy(as);
           return errno;
        }
     }

     /* kargs is no longer needed*/
     destroy_array(arg, kargs);

     /* Finally we delete old address space */
     as_destroy(old_as);

     /* Pointer to argument*/
     argv = stackptr;

     /* Stack pointer should always be 8-byte aligned */
     /* Note roundup cannot be applied here as stackptr decreases */
     stackptr -= (stackptr % 8);

     /* Warp to user mode. */
     enter_new_process(arg, (userptr_t)argv, stackptr, entrypoint);
     
     /* enter_new_process does not return. */
     panic("sys_execv failed\n");

     return 0;
}
