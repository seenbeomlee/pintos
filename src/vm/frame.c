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

void lru_list_init(void) {
    list_init(&frame_table);  // 프레임 리스트 초기화
    lock_init(&frame_table_lock);  // 락 초기화
    frame_clock_pointer = NULL;  // 클럭 포인터 초기화
}

// 페이지 프레임 할당
struct page* alloc_page_frame(enum palloc_flags flags) {
    struct page* new_frame = create_frame(flags);
    if (!new_frame) return NULL;

    if (!new_frame->kaddr) {
        resolve_memory_shortage(flags, new_frame);  // 메모리 부족 시 처리
    }
    return new_frame;
}

// 프레임 구조체 생성 (헬퍼 함수)
static struct page* create_frame(enum palloc_flags flags) {
    struct page* frame = (struct page*)malloc(sizeof(struct page));
    if (frame != NULL) {
        memset(frame, 0, sizeof(struct page));
        frame->t = thread_current();
        frame->kaddr = palloc_get_page(flags);
    }
    return frame;
}

// 메모리 부족 처리 로직 (헬퍼 함수)
static void resolve_memory_shortage(enum palloc_flags flags, struct page* frame) {
    try_to_free_pages();  // LRU 알고리즘 기반 페이지 해제
    frame->kaddr = palloc_get_page(flags);
    ASSERT(frame->kaddr != NULL);
}

void add_page_to_lru_list(struct page* page) {
  ASSERT(page);
  ASSERT(pg_ofs(page->kaddr) == 0); // 페이지 정렬 확인

  lock_acquire(&frame_table_lock);  // 락 획득
  list_push_back(&frame_table, &page->lru); // 리스트 뒤에 추가
  lock_release(&frame_table_lock);  // 락 해제
}

void delete_from_lru_list(struct page* page) {
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
struct page* get_page_with_kaddr(void* kaddr) {
    ASSERT(pg_ofs(kaddr) == 0);

    struct page* result_page = NULL;
    struct list_elem* elem;

    lock_acquire(&frame_table_lock);
    for (elem = list_begin(&frame_table); elem != list_end(&frame_table); elem = list_next(elem)) {
        struct page* current_page = list_entry(elem, struct page, lru);
        if (current_page->kaddr == kaddr) {
            result_page = current_page;
            break;
        }
    }
    lock_release(&frame_table_lock);

    return result_page;
}

// 페이지 메모리 해제
void free_page(void* kaddr) {
    struct page* target_page = get_page_with_kaddr(kaddr);
    ASSERT(target_page != NULL);
    __free_page(target_page);
}

// 페이지 삭제 내부 로직
void __free_page(struct page* page) {
    delete_from_lru_list(page);
    palloc_free_page(page->kaddr);
    free(page);
}

// 다음 LRU 포인터 가져오기 (헬퍼 함수)
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

// 메모리 부족 시 페이지 해제
void try_to_free_pages(void) {
    ASSERT(!list_empty(&frame_table));

    struct page* victim_page = NULL;

    lock_acquire(&frame_table_lock);

    while (1) {
        struct list_elem* elem = get_next_lru_pointer();
        victim_page = list_entry(elem, struct page, lru);

        if (pagedir_is_accessed(victim_page->t->pagedir, victim_page->spe->vaddr)) {
            pagedir_set_accessed(victim_page->t->pagedir, victim_page->spe->vaddr, false);
            continue;
        }

        if (pagedir_is_dirty(victim_page->t->pagedir, victim_page->spe->vaddr) || victim_page->spe->type == VM_ANON) {
            if (victim_page->spe->type == VM_FILE) {
                lock_acquire(&filesys_lock);
                file_write_at(victim_page->spe->file, victim_page->kaddr, victim_page->spe->read_bytes, victim_page->spe->offset);
                lock_release(&filesys_lock);
            } else {
                victim_page->spe->type = VM_ANON;
                victim_page->spe->swap_slot = swap_out(victim_page->kaddr);
            }

            victim_page->spe->is_loaded = false;
            pagedir_clear_page(victim_page->t->pagedir, victim_page->spe->vaddr);

            lock_release(&frame_table_lock);
            break;
        }
    }

    __free_page(victim_page);
}
