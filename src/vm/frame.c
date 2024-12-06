#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "lib/kernel/list.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"
#include "vm/frame.h"

// Frame 관리용 리스트와 락
static struct list frame_table;
static struct list_elem* frame_clock_pointer;

static struct lock frame_table_lock;

extern struct lock filesys_lock;

static struct page* create_frame(enum palloc_flags flags);
static void resolve_memory_shortage(enum palloc_flags flags, struct page* frame);

static struct page* find_frame_by_kaddr(void* kernel_addr);
static void evict_frame(void);
static struct list_elem* get_next_lru_pointer(void);
static void release_frame(struct page* frame);

void init_frame_table(void) {
  list_init(&frame_table);  // 프레임 리스트 초기화
  lock_init(&frame_table_lock);  // 락 초기화
  frame_clock_pointer = NULL;  // 클럭 포인터 초기화
}

// 페이지 프레임 할당
struct page* allocate_frame(enum palloc_flags flags) {
  struct page* new_frame = create_frame(flags);
  if (!new_frame) return NULL;

  if (!new_frame->kaddr) {
    resolve_memory_shortage(flags, new_frame);  // 메모리 부족 시 처리
  }
  return new_frame;
}

// 프레임 구조체 생성
static struct page* create_frame(enum palloc_flags flags) {
  struct page* frame = (struct page*)malloc(sizeof(struct page));
  if (frame != NULL) {
    memset(frame, 0, sizeof(struct page));
    frame->t = thread_current();
    frame->kaddr = palloc_get_page(flags);
  }
  return frame;
}

// 메모리 부족 처리 로직
static void resolve_memory_shortage(enum palloc_flags flags, struct page* frame) {
    evict_frame();  // LRU 알고리즘 기반 페이지 해제
    frame->kaddr = palloc_get_page(flags);
    ASSERT(frame->kaddr != NULL);
}

void add_frame_to_lru(struct page* page) {
  ASSERT(page);
  ASSERT(pg_ofs(page->kaddr) == 0); // 페이지 정렬 확인

  lock_acquire(&frame_table_lock);  // 락 획득
  list_push_back(&frame_table, &page->lru); // 리스트 뒤에 추가
  lock_release(&frame_table_lock);  // 락 해제
}

void remove_frame_from_lru(struct page* page) {
  ASSERT(page);

  lock_acquire(&frame_table_lock);

  // 클럭 포인터 재설정
  if (frame_clock_pointer == &page->lru) {
    frame_clock_pointer = list_remove(frame_clock_pointer); // 현재 포인터를 제거 후 재설정
  } else {
    list_remove(&page->lru);
  }

  lock_release(&frame_table_lock);
}

// 커널 주소로 페이지 검색
static struct page* find_frame_by_kaddr(void* kernel_addr) {
  ASSERT(pg_ofs(kernel_addr) == 0);

  struct page* frame = NULL;
  struct list_elem* elem;

  lock_acquire(&frame_table_lock);
  for (elem = list_begin(&frame_table); elem != list_end(&frame_table); elem = list_next(elem)) {
    struct page* current_frame = list_entry(elem, struct page, lru);
    if (current_frame->kaddr == kernel_addr) {
      frame = current_frame;
      break;
    }
  }
  lock_release(&frame_table_lock);

  return frame;
}

// 메모리 부족 시 페이지 해제
static void evict_frame(void) {
  ASSERT(!list_empty(&frame_table));

  struct page* victim_frame = NULL;

  lock_acquire(&frame_table_lock);

  while (1) {
    struct list_elem* elem = get_next_lru_pointer();
    victim_frame = list_entry(elem, struct page, lru);

    if (pagedir_is_accessed(victim_frame->t->pagedir, victim_frame->spe->vaddr)) {
      pagedir_set_accessed(victim_frame->t->pagedir, victim_frame->spe->vaddr, false);
      continue;
    }

    if (pagedir_is_dirty(victim_frame->t->pagedir, victim_frame->spe->vaddr) || victim_frame->spe->type == VM_ANON) {
      if (victim_frame->spe->type == VM_FILE) {
        lock_acquire(&filesys_lock);
        file_write_at(victim_frame->spe->file, victim_frame->kaddr, victim_frame->spe->read_bytes, victim_frame->spe->offset);
        lock_release(&filesys_lock);
      } else {
        victim_frame->spe->type = VM_ANON;
        victim_frame->spe->swap_slot = swap_out(victim_frame->kaddr);
      }

      victim_frame->spe->is_loaded = false;
      pagedir_clear_page(victim_frame->t->pagedir, victim_frame->spe->vaddr);

      lock_release(&frame_table_lock);
      break;
    }
  }

  release_frame(victim_frame);
}

// 다음 LRU 포인터 가져오기
static struct list_elem* get_next_lru_pointer(void) {
  if (!frame_clock_pointer || frame_clock_pointer == list_end(&frame_table)) {
    frame_clock_pointer = list_begin(&frame_table);
  } else {
    frame_clock_pointer = list_next(frame_clock_pointer);
  }

  if (frame_clock_pointer == list_end(&frame_table)) {
    frame_clock_pointer = list_begin(&frame_table);
  }
  return frame_clock_pointer;
}

// 페이지 메모리 해제
void free_page(void* kernel_addr) {
  struct page* target_frame = find_frame_by_kaddr(kernel_addr);
  ASSERT(target_frame != NULL);
  release_frame(target_frame);
}

// 페이지 삭제 내부 로직
static void release_frame(struct page* frame) {
  remove_frame_from_lru(frame);
  palloc_free_page(frame->kaddr);
  free(frame);
}
