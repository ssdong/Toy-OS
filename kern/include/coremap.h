#ifndef _COREMAP_H_
#define _COREMAP_H_

struct coremap_entry
{ 
    // indicate if this entry(page) is being used
    int used;
    // number of continuous allocation of pages
    unsigned long count;
};


#endif /*_COREMAP_H_*/
