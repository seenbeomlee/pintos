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

/** project 3 : virtual memory
 * LRU (Least Recently Used) 알고리즘을 통해 페이지 교체를 하기 위한 변수이다.
 */
static struct list frame_table; // 모든 물리 메모리 프레임이 저장된 리스트로, 리스트 노드로 관리되며, 페이지 교체 시 순회한다.
static struct list_elem* frame_clock_pointer; // LRU에서 현재 탐색 중인 프레임을 가리키는 포인터로, 이 포인터로 순회하며 희생 페이지 선택한다.

static struct lock frame_table_lock; // 프레임 작업시 동기화를 위한 락이다.
extern struct lock filesys_lock; // 파일 시스템 작업과의 동기화를 위한 외부 락이다.

static struct frame_entry* create_frame(enum palloc_flags flags); // 새로운 프레임 생성
static void resolve_memory_shortage(enum palloc_flags flags, struct frame_entry* frame); // 물리 메모리 부족시 해결하는 로직

static struct frame_entry* find_frame_by_kaddr(void* kernel_addr); // 커널 주소를 기반으로 프레임을 검색한다.
static struct frame_entry* find_frame(bool (*condition)(struct frame_entry*, void*), void* aux); // 조건 함수에 맞는 프레임을 찾는다.

static bool is_kaddr_match(struct frame_entry* frame, void* kernel_addr); // 커널 주소가 일치하는지 확인하는 조건 함수이다.

static void evict_frame(void); // LRU 알고리즘에 기반해서 페이지 교체를 수행한다.
static bool process_eviction(struct frame_entry* frame); // dirty page나 anonymous page를 처리한다.

static struct list_elem* get_next_lru_pointer(void); // LRU 포인터를 다음으로 이동시킨다.
static void release_frame(struct frame_entry* frame); // 프레임과 관련된 모든 리소스를 해제한다.
static void remove_frame_from_lru(struct frame_entry* frame); // LRU 리스트에서 프레임을 제거한다.

// 프레임 테이블 초기화
void init_frame_table(void) {
  list_init(&frame_table);  // 프레임 리스트 초기화
  lock_init(&frame_table_lock);  // 락 초기화
  frame_clock_pointer = NULL;  // 클럭 포인터 초기화
}

// 페이지 프레임 할당
struct frame_entry* allocate_frame(enum palloc_flags flags) {
  struct frame_entry* new_frame = create_frame(flags);
  if (!new_frame) return NULL;

  if (!new_frame->kernal_addr) {
    resolve_memory_shortage(flags, new_frame);  // 메모리 부족 시 처리
  }
  return new_frame;
}

// 프레임 구조체 생성
static struct frame_entry* create_frame(enum palloc_flags flags) {
  struct frame_entry* frame = (struct frame_entry*)malloc(sizeof(struct frame_entry));
  if (frame != NULL) {
    memset(frame, 0, sizeof(struct frame_entry));
    frame->owner_thread = thread_current();
    frame->kernal_addr = palloc_get_page(flags);
  }
  return frame;
}

// 메모리 부족 처리 로직
static void resolve_memory_shortage(enum palloc_flags flags, struct frame_entry* frame) {
    evict_frame();  // LRU 알고리즘 기반 페이지 해제
    frame->kernal_addr = palloc_get_page(flags);
    ASSERT(frame->kernal_addr != NULL);
}

/* ********** ********** ********** ********** ********** ********** ********** ********** */

void add_frame_to_lru(struct frame_entry* new_frame) {
  ASSERT(new_frame);
  ASSERT(pg_ofs(new_frame->kernal_addr) == 0); // 페이지 정렬 확인

  lock_acquire(&frame_table_lock);  // 락 획득
  list_push_back(&frame_table, &new_frame->lru_elem); // 리스트 뒤에 추가
  lock_release(&frame_table_lock);  // 락 해제
}

/* ********** ********** ********** ********** ********** ********** ********** ********** */

// 커널 주소를 기반으로 페이지 프레임을 검색
static struct frame_entry* find_frame_by_kaddr(void* kernel_addr) {
  ASSERT(pg_ofs(kernel_addr) == 0);  // 커널 주소는 페이지 크기로 정렬되어야 함

  return find_frame(is_kaddr_match, kernel_addr);  // 특정 조건에 따라 프레임 검색
}

// 리스트를 순회하며 조건에 맞는 프레임을 검색
static struct frame_entry* find_frame(bool (*condition)(struct frame_entry*, void*), void* aux) {
  struct frame_entry* result_frame = NULL;
  struct list_elem* elem;

  lock_acquire(&frame_table_lock);  // 동시 접근 방지를 위한 락 획득
  for (elem = list_begin(&frame_table); elem != list_end(&frame_table); elem = list_next(elem)) {
    struct frame_entry* current_frame = list_entry(elem, struct frame_entry, lru_elem);
    if (condition(current_frame, aux)) {  // 조건을 만족하는 경우 프레임 반환
      result_frame = current_frame;
      break;
    }
  }
  lock_release(&frame_table_lock);  // 락 해제

  return result_frame;  // 조건을 만족하는 프레임 반환 또는 NULL
}

// 커널 주소가 일치하는지 확인하는 조건 함수
static bool is_kaddr_match(struct frame_entry* frame, void* kernel_addr) {
  return frame->kernal_addr == kernel_addr;  // 프레임의 커널 주소가 주어진 주소와 동일한지 확인
}

/* ********** ********** ********** ********** ********** ********** ********** ********** */

// LRU 알고리즘을 사용해 메모리 부족 시 페이지를 해제
static void evict_frame(void) {
  ASSERT(!list_empty(&frame_table));  // 프레임 테이블이 비어 있지 않음

  struct frame_entry* victim_frame = NULL;

  lock_acquire(&frame_table_lock);  // 동시 접근 방지를 위한 락 획득

  while (true) {
    struct list_elem* elem = get_next_lru_pointer();  // LRU 알고리즘에 따라 다음 프레임 선택
    victim_frame = list_entry(elem, struct frame_entry, lru_elem); // 희생 페이지 가져오기

    // 접근 비트가 설정된 경우 클리어하고 다음 프레임으로 이동
    if (pagedir_is_accessed(victim_frame->owner_thread->pagedir, victim_frame->spt_entry->virtual_addr)) {
      pagedir_set_accessed(victim_frame->owner_thread->pagedir, victim_frame->spt_entry->virtual_addr, false); // 접근 비트 클리어
      continue;  // 최근에 사용된 페이지이므로 다음으로 이동
    }

    // Dirty 페이지 처리 또는 스왑 아웃
    if (process_eviction(victim_frame)) {  // 페이지가 Dirty하거나 익명 페이지인 경우 스왑을 처리하는 방식
      lock_release(&frame_table_lock);  // 성공적으로 페이지를 처리한 경우 락 해제 후 종료
      break;
    }
  }

  release_frame(victim_frame);  // 희생 페이지 메모리 해제
}

// Dirty 페이지 또는 Anonymout Page의 스왑 아웃을 처리하는 방식
static bool process_eviction(struct frame_entry* frame) {
  if (pagedir_is_dirty(frame->owner_thread->pagedir, frame->spt_entry->virtual_addr) || frame->spt_entry->entry_type == VM_ANON) {
    if (frame->spt_entry->entry_type == VM_FILE) { // 파일 기반 페이지의 경우 처리
      // 파일 페이지는 디스크로 다시 기록
      lock_acquire(&filesys_lock);
      file_write_at(frame->spt_entry->mmap_file, frame->kernal_addr, frame->spt_entry->read_bytes, frame->spt_entry->offset);
      lock_release(&filesys_lock);
    } else { // 익명 페이지의 경우 처리
      // 익명 페이지는 스왑 슬롯으로 기록
      frame->spt_entry->entry_type = VM_ANON;
      frame->spt_entry->swap_index = swap_out(frame->kernal_addr);
    }

    // 페이지 상태 갱신
    frame->spt_entry->is_loaded = false; // 메모리에서 로드 상태 제거
    pagedir_clear_page(frame->owner_thread->pagedir, frame->spt_entry->virtual_addr);  // 페이지 테이블에서 제거
    return true;  // 페이지 처리 성공
  }

  return false;  // 페이지 처리 실패
}

// LRU 알고리즘에 따라 다음 포인터를 가져옴
static struct list_elem* get_next_lru_pointer(void) {
  if (!frame_clock_pointer || frame_clock_pointer == list_end(&frame_table)) {
    frame_clock_pointer = list_begin(&frame_table);  // 포인터를 리스트의 처음 프레임으로 이동
  } else {
    frame_clock_pointer = list_next(frame_clock_pointer);  // 포인터를 다음 프레임으로 이동
  }

  if (frame_clock_pointer == list_end(&frame_table)) {
    frame_clock_pointer = list_begin(&frame_table);  // 리스트의 끝에 도달하면 처음 프레임으로 포인터를 이동
  }
  return frame_clock_pointer;
}

// 물리 메모리 프레임을 해제하는 함수
// 주어진 커널 주소(kernel_addr)에 해당하는 프레임을 찾아 해제한다.
void free_frame(void* kernel_addr) {
  struct frame_entry* target_frame = find_frame_by_kaddr(kernel_addr);
  ASSERT(target_frame != NULL);  // 해당 커널 주소가 유효해야 함
  release_frame(target_frame);  // 페이지 메모리와 관련 리소스 해제
}

// 프레임과 관련된 리소스를 정리하고 해제하는 함수
static void release_frame(struct frame_entry* frame) {
  remove_frame_from_lru(frame);      // LRU 리스트에서 제거
  palloc_free_page(frame->kernal_addr);   // 물리 메모리 해제
  free(frame);                      // 페이지 구조체 메모리 해제
}

// LRU 리스트에서 프레임을 제거하는 함수
static void remove_frame_from_lru(struct frame_entry* frame) {
  ASSERT(frame);

  lock_acquire(&frame_table_lock);

  // 클럭 포인터를 현재 프레임에서 다음 프레임으로 이동 
  if (frame_clock_pointer == &frame->lru_elem) {
    frame_clock_pointer = list_remove(frame_clock_pointer); // 현재 포인터를 제거 후 재설정
  } else {
    list_remove(&frame->lru_elem);  // 단순히 리스트에서 제거
  }

  lock_release(&frame_table_lock);
}