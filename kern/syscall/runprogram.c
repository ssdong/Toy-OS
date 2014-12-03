/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
/******** New for A2  ************/
#include <copyinout.h>
#include <limits.h>
/*********************************/
/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname, int argc, char** args)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr, /* A2 */ argv;
	int result;

        /* ----------------A2-----------------*/
  
	/* check if program exists */
	if(progname == NULL) {
		return ENOENT;
	}

	/* Check if it's a full path */
        int full_path = 0;
        const char* temp = progname;
        while(*temp != '\0') {
        if(*temp == '/') {
           full_path = 1;
           break;
        }
        temp++;
        }
        /* Check if program name exceed the limit */
        if(full_path) {
        char* file_name = strrchr(progname, '/');
           if(strlen(progname) > PATH_MAX || strlen(file_name) - 1 > NAME_MAX) {
               return E2BIG;
           }
        }
        else {
           if(strlen(progname) > NAME_MAX) {
               return E2BIG;
           }
        }

        /* ----------------A2-----------------*/

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(curproc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	curproc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	/* ------------------A2------------------ */
        vaddr_t user_space_pointer[1 + argc]; 
        /*   IMPORTANT !  */
        /* In the new process, argv[argc] must be NULL!!!! Thus, we have to assign user_space_pointer[arg] to be 0*/
        user_space_pointer[argc] = 0;

        /* Copy argument string to user space */
        for(int i = 0; i < argc; i++) {
            size_t len = strlen(args[i]);
            stackptr -= ROUNDUP((len + 1), 4);
            result = copyoutstr(args[i], (userptr_t)stackptr, ARG_MAX_LEN, NULL);
            if(result != 0) { 
               return result;
            } 
            /* Fill user_space_pointer with actual user space pointer */
            user_space_pointer[i] = stackptr;
        }

        for(int i = argc; i >= 0; i--) {
    	    stackptr -= sizeof(vaddr_t);
            result = copyout(&(user_space_pointer[i]), (userptr_t)stackptr, sizeof(vaddr_t));
            if(result != 0) {
               return result;
            }
        }
    
        /* Pointer to argument*/
        argv = stackptr;

        /* Stack pointer should always be 8-byte aligned */
        /* Note roundup cannot be applied here as stackptr decreases */
        stackptr -= (stackptr % 8);
        /* ------------------A2------------------ */

        /* Warp to user mode. */
        // Original enter_new_process
        // enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
        //		         stackptr, entrypoint);

        enter_new_process(argc, (userptr_t)argv, stackptr, entrypoint);

        /* enter_new_process does not return. */
        panic("enter_new_process returned\n");
        return EINVAL;
}

