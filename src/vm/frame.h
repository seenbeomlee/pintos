#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include <list.h>
#include "vm/page.h"

/* 
 * 프레임 테이블과 LRU 리스트를 초기화한다.
 * 시스템 초기화 중 반드시 호출되어야 하며, 물리 메모리 프레임을
 * 관리하기 위한 데이터 구조를 설정한다.
 */
void init_frame_table(void);

/* 
 * 페이지에 물리 프레임을 할당한다.
 * 물리 메모리가 부족한 경우, LRU 페이지 교체 알고리즘을 통해
 * 메모리 부족 문제를 해결한다.
 * 
 * 매개변수:
 *  - flags: 할당 플래그 (PAL_USER 등 사용자 페이지용 플래그)
 * 
 * 반환값:
 *  - 할당된 페이지 구조체를 가리키는 포인터를 반환하며, 할당에 실패하면 NULL을 반환한다.
 */
struct page* allocate_frame(enum palloc_flags flags);

/* 
 * 새로 할당된 프레임을 LRU 리스트에 추가한다.
 * 프레임이 성공적으로 할당된 이후에 호출되며, 페이지 교체를 위해
 * 프레임을 추적하는 용도로 사용한다.
 * 
 * 매개변수:
 *  - new_page: 새로 할당된 프레임을 나타내는 페이지 구조체.
 */
void add_frame_to_lru(struct page* new_page);

/* 
 * 커널 가상 주소를 기반으로 물리 프레임을 해제한다.
 * 프레임을 해제하고, 프레임 테이블과 LRU 리스트에서 해당 프레임을 제거한다.
 * 
 * 매개변수:
 *  - kernel_addr: 해제할 프레임의 커널 가상 주소.
 */
void free_page(void* kernel_addr);

#endif
