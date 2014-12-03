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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
 /********New for A3*************/
#include <syscall.h>
#include <coremap.h>
#include <mips/trapframe.h>
#include "opt-A3.h"
 /*********************************/


/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

#ifdef OPT_A3

/* define segment types*/
#define TEXT_SEGMENT  1
#define DATA_SEGMENT  2
#define STACK_SEGMENT 3

/*
   0 ----- vm has not yet bootstrapped 
   1 ----- vm has alreday bootstrapped
*/
static int vm_bootstrap_flag = 0;
static uint32_t num_of_coremap_pages, num_of_pages;
static paddr_t firstaddr, lastaddr;
static struct coremap_entry* coremap;

#endif

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;

void
vm_bootstrap(void)
{
	#ifdef OPT_A3
    
    unsigned long coremap_size;

    spinlock_acquire(&coremap_lock);

    ram_getsize(&firstaddr, &lastaddr);

    /* Start position of coremap */
    coremap = (struct coremap_entry*)PADDR_TO_KVADDR(firstaddr);
    
    /* Calculate the number of frames available */
    num_of_pages = (lastaddr - firstaddr) / PAGE_SIZE;

    /* Calculate the size of the core map */
    coremap_size = num_of_pages * sizeof(struct coremap_entry);

    /* Calculate the number of pages used by coremap. It's better to make it page-aligned*/
    num_of_coremap_pages = ROUNDUP(coremap_size, PAGE_SIZE) / PAGE_SIZE;

    // Now we initialize coremap
    // The number of pages should be same as the number of coremap entries
    // The first few pages are used by coremap so we label their "used" as 1
    for(uint32_t i = 0; i < num_of_pages; i++) {
    	if(i < num_of_coremap_pages) {
    	    coremap[i].used = 1;
    	    coremap[i].count = 1;
        }
        else {
        	coremap[i].used = 0;
            coremap[i].count = 0;
        }
    }
    
    // vm finished bootstrapping !
    vm_bootstrap_flag = 1;

    spinlock_release(&coremap_lock);

	#endif
}

#ifdef OPT_A3

static
struct pagetable_entry* 
pagetable_alloc(size_t npages) {
   struct pagetable_entry* page_table;
   unsigned long page_table_size;
   unsigned long page_table_pages;

   /* Calculate the size of the page table */
   page_table_size = npages * sizeof(struct pagetable_entry);

   /* Calculate how many pages should be used by page table. It's better to round up*/
   page_table_pages = ROUNDUP(page_table_size, PAGE_SIZE) / PAGE_SIZE;

   /* Now we allocate space for the page table by using alloc_kpages */
   page_table = (struct pagetable_entry *)alloc_kpages(page_table_pages);

   if(page_table == 0) {
   	  return 0;
   }
   return page_table;
}

static
void 
coremap_dealloc(paddr_t pa) {

    spinlock_acquire(&coremap_lock);

    uint32_t start = ( pa - firstaddr ) / PAGE_SIZE;
    uint32_t end = start + coremap[start].count;
    for(uint32_t i = start; i < end; i++) {
        coremap[i].used = 0;
        coremap[i].count = 0;
    }
    
    spinlock_release(&coremap_lock);
}

static
paddr_t
coremap_alloc(unsigned long npages) {
    paddr_t addr;
    int successful = 0;
    uint32_t length = 0;
    uint32_t start = 0;

    spinlock_acquire(&coremap_lock);

    for(uint32_t i = num_of_coremap_pages; i < num_of_pages; i++) {
        if(coremap[i].used == 0) {
        	++length;
        	/* We have found a sequence of free frames */
        	if(length == npages) {
               successful = 1;
    		   start = i - length + 1;

    		   /* Start filling in the coremap entries */
    		   coremap[start].used = 1;
    		   coremap[start].count = npages;
    		   for(uint32_t j = start + 1 ; j <= i; j++) {
                   coremap[j].used = 1;
                   coremap[j].count = 1;
    		   }
    		   /* End filling in the coremap entries */

    		   addr = firstaddr + start * PAGE_SIZE;
    		   break;
    	    }
        }
        else {
        	length = 0;
        }
    }
    if(!successful) {
       spinlock_release(&coremap_lock);
       return 0;
    }
    spinlock_release(&coremap_lock);

    return addr;
}

#endif

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	#if OPT_A3
    
    // Need to call coremap_alloc here. !important
    // as_prepare_load will call getppages directly instead of alloc_kpages
    if(vm_bootstrap_flag) {
        addr = coremap_alloc(npages);
    }
    else {
    	spinlock_acquire(&stealmem_lock);
    
        addr = ram_stealmem(npages);

	    spinlock_release(&stealmem_lock);
    }

	#else

	spinlock_acquire(&stealmem_lock);
    
    addr = ram_stealmem(npages);

	spinlock_release(&stealmem_lock);

	#endif

	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;

	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);

}

void 
free_kpages(vaddr_t addr)
{
	#if OPT_A3

    paddr_t pa;
    pa = KVADDR_TO_PADDR(addr);
    coremap_dealloc(pa);

	#else

	/* nothing - leak the memory.  */

	(void)addr;

	#endif
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl, segment;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);


	switch (faulttype) {
	    case VM_FAULT_READONLY:
		    /* We always create pages read-write, so we can't get this */
            #if OPT_A3

            sys__exit(EX_MOD);

	        #else

		    panic("dumbvm: got VM_FAULT_READONLY\n");

		    #endif

	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		    break;
	    default:
		    return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->page_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->page_pbase2 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->page_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		#if OPT_A3
        paddr = as->page_pbase1[(faultaddress - vbase1)/ PAGE_SIZE].paddr;
		#else
		paddr = (faultaddress - vbase1) + as->as_pbase1;
		#endif 
		segment = TEXT_SEGMENT;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		#if OPT_A3
        paddr = as->page_pbase2[(faultaddress - vbase2) / PAGE_SIZE].paddr;
		#else
		paddr = (faultaddress - vbase2) + as->as_pbase2;
		#endif
		segment = DATA_SEGMENT;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		#if OPT_A3
        paddr = as->page_stackpbase[((faultaddress - stackbase)) / PAGE_SIZE].paddr;
		#else
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
		#endif
		segment = STACK_SEGMENT;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;

		#ifdef OPT_A3
        // If it's the text 
        if(segment == TEXT_SEGMENT && curproc->loaded == true) {
              elo &= ~TLBLO_DIRTY;
        }
        #endif

		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

    #if OPT_A3
    
    // Set the values for ehi and elo
    ehi = faultaddress;
    elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
    // If it's the text 
    if(segment == TEXT_SEGMENT && curproc->loaded == true) {
          elo &= ~TLBLO_DIRTY;
    }
    // Pass to random !
    tlb_random(ehi, elo);
    splx(spl);
    return 0;

    #else

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;

	#endif
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->page_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->page_pbase2 = 0;
	as->as_npages2 = 0;
	as->page_stackpbase = 0;

	return as;
}

void
as_destroy(struct addrspace *as)
{
	#ifdef OPT_A3
    /* Time to destroy the text page table */
    size_t npages1 = as->as_npages1;
    struct pagetable_entry* text = as->page_pbase1;
    for (size_t i = 0; i < npages1; i++) {
    	free_kpages(PADDR_TO_KVADDR(text[i].paddr));
    }

    /* Time to destroy the data page table */
    size_t npages2 = as->as_npages2;
    struct pagetable_entry* data = as->page_pbase2;
    for (size_t i = 0; i < npages2; i++) {
    	free_kpages(PADDR_TO_KVADDR(data[i].paddr));
    }

    /* Time to destroy the stack page table */
    struct pagetable_entry* stack = as->page_stackpbase;
    for (size_t i = 0; i < DUMBVM_STACKPAGES; i++) {
    	free_kpages(PADDR_TO_KVADDR(stack[i].paddr));
    }

    free_kpages((vaddr_t)as->page_pbase1);
    free_kpages((vaddr_t)as->page_pbase2);
    free_kpages((vaddr_t)as->page_stackpbase);
    #endif
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	#if OPT_A3
    KASSERT(as->page_pbase1 == NULL);
    KASSERT(as->page_pbase2 == NULL);
    KASSERT(as->page_stackpbase == NULL);

    /* Now we initialize text page table */
    as->page_pbase1 = pagetable_alloc(as->as_npages1);
    if(as->page_pbase1 == 0) {
    	return ENOMEM;
    }

    /* Now we initialize data page table */
    as->page_pbase2 = pagetable_alloc(as->as_npages2);
    if(as->page_pbase2 == 0) {
    	return ENOMEM;
    }

    /* Now we initialize stack page table */
    as->page_stackpbase = pagetable_alloc(DUMBVM_STACKPAGES);
    if(as->page_stackpbase == 0) {
    	return ENOMEM;
    }

    /* Now we map frames for text page table */
    struct pagetable_entry* text = as->page_pbase1;
    for(size_t i = 0; i < as->as_npages1; i++) {
       /* We allocate one page */
       text[i].paddr = getppages(1);
       /* Check if getppages succedd */
       if(text[i].paddr == 0) {
       	  return ENOMEM;
       }
       /* Zero out the allocated page */
       as_zero_region(text[i].paddr, 1);
    }

    /* Now we map frames for data page table */
    struct pagetable_entry* data = as->page_pbase2;
    for (size_t i = 0; i < as->as_npages2; i++) {
       /* We allocate one page */
       data[i].paddr = getppages(1);
       /* Check if getppages succedd */
       if(data[i].paddr == 0) {
       	  return ENOMEM;
       }
       /* Zero out the allocated page */
       as_zero_region(data[i].paddr, 1);
    }

    /* Now we map frames for stack page table */
    struct pagetable_entry* stack = as->page_stackpbase;
    for (size_t i = 0; i < DUMBVM_STACKPAGES; i++) {
       /* We allocate one page */
       stack[i].paddr = getppages(1);
       /* Check if getppages succedd */
       if(stack[i].paddr == 0) {
       	  return ENOMEM;
       }
       /* Zero out the allocated page */
       as_zero_region(stack[i].paddr, 1);
    }

    return 0;


	#else
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}
	
	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
	#endif
}

int
as_complete_load(struct addrspace *as)
{
	
	(void)as;

	#ifdef OPT_A3
    
    curproc->loaded = true;
    as_activate();

    #endif

	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->page_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	#if OPT_A3

	KASSERT(new->page_pbase1 != 0);
	KASSERT(new->page_pbase2 != 0);
	KASSERT(new->page_stackpbase != 0);

	/* Text segment */ 
	size_t npages1 = new->as_npages1;
	struct pagetable_entry* new_text = new->page_pbase1;
	struct pagetable_entry* old_text = old->page_pbase1;
    for(size_t i = 0; i < npages1; i++) {
    	memmove((void *)PADDR_TO_KVADDR(new_text[i].paddr),
		(const void *)PADDR_TO_KVADDR(old_text[i].paddr),
		PAGE_SIZE);
    }


    /* Data segment */ 
	size_t npages2 = new->as_npages2;
	struct pagetable_entry* new_data = new->page_pbase2;
	struct pagetable_entry* old_data = old->page_pbase2;
    for(size_t i = 0; i < npages2; i++) {
    	memmove((void *)PADDR_TO_KVADDR(new_data[i].paddr),
		(const void *)PADDR_TO_KVADDR(old_data[i].paddr),
		PAGE_SIZE);
    }    

    /* Stack segment */ 
	struct pagetable_entry* new_stack = new->page_stackpbase;
	struct pagetable_entry* old_stack = old->page_stackpbase;
    for(size_t i = 0; i < DUMBVM_STACKPAGES; i++) {
    	memmove((void *)PADDR_TO_KVADDR(new_stack[i].paddr),
		(const void *)PADDR_TO_KVADDR(old_stack[i].paddr),
		PAGE_SIZE);
    }
	#else

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
	
	#endif

	*ret = new;
	return 0;
}
