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

/* Remove a frame from the LRU list and update the clock pointer if necessary */
void remove_frame_from_lru(struct page* target_page);

/* Free a page by its kernel address */
void free_page(void* kernel_addr);
void __free_page(struct page* target_page);

/* Retrieve a page structure using its kernel address */
struct page* get_page_with_kaddr(void* kernel_addr);

/* Iterate to the next node in the LRU list */
static struct list_elem* get_next_lru_clock(void);

/* Free pages based on the LRU algorithm */
void try_to_free_pages(void);

#endif
