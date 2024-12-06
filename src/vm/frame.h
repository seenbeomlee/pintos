#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include <list.h>
#include "vm/page.h"

/* Initialize the frame table and LRU list */
void init_frame_table(void);

/* Allocate a physical frame for a page, handling memory shortages if needed */
struct page* allocate_frame(enum palloc_flags flags);

/* Add a frame to the LRU list to manage replacement policies */
void add_frame_to_lru(struct page* new_page);

/* find frame with specific condition function (like lamdba) */
struct page* find_frame(bool (*condition)(struct page*, void*), void* aux) ;

/* Remove a frame from the LRU list and update the clock pointer if necessary */
void remove_frame_from_lru(struct page* target_page);

/* Free a page by its kernel address */
void free_page(void* kernel_addr);

#endif
