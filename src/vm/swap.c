#include "vm/swap.h"
#include "vm/frame.h"
#include <bitmap.h>
#include <debug.h>
#include "threads/synch.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/interrupt.h"

struct lock swap_lock;
struct bitmap* swap_map;
struct block* swap_block;

static void swap_read_write(size_t swap_index, void *page_addr, bool is_read);

void swap_init(void) {
	lock_init(&swap_lock);  // 락 초기화
	swap_block = block_get_role(BLOCK_SWAP); // 스왑 디스크 블록을 가져온다.

	if (swap_block) {
		size_t total_pages = block_size(swap_block) / SECTORS_PER_PAGE; // 스왑 디스크 크기를 계산하여 페이지 크기로 나눈다.
		swap_map = bitmap_create(total_pages); // 이를 기반으로, bitmap을 생성한다.
		if (swap_map) {
			bitmap_set_all(swap_map, SWAP_FREE);  // 모든 슬롯을 비어 있는 상태로 초기화
		}
	}
}

void swap_in(size_t swap_index, void *kernal_addr) {
	if (bitmap_test(swap_map, swap_index) == SWAP_FREE) return;

	lock_acquire(&swap_lock);
	swap_read_write(swap_index, kernal_addr, true);  // 스왑 디스크에서 데이터를 읽어서 물리 메모리에 복사한다.
	bitmap_flip(swap_map, swap_index);  // 스왑 슬롯을 비어있는 상대로 업데이트 한다.
	lock_release(&swap_lock);
}

size_t swap_out(void *kernal_addr) {
	lock_acquire(&swap_lock);

	size_t free_index = bitmap_scan_and_flip(swap_map, 0, 1, SWAP_FREE); // 비어있는 스왑 슬롯이 있는지 비트맵에서 검색한다.
	if (free_index == BITMAP_ERROR) {
		lock_release(&swap_lock);
		return BITMAP_ERROR;
	}

	swap_read_write(free_index, kernal_addr, false);  // 선택된 스왑 슬롯 번호를 사용하여, 페이지 데이터를 스왑 디스크에 저장한다.
	lock_release(&swap_lock);

	return free_index; // 스왑된 슬롯 번호를 반환하여 spt에 업데이트 한다.
}

static void swap_read_write(size_t swap_index, void *page_addr, bool is_read) {
	ASSERT(swap_map != NULL);  // 스왑 비트맵이 초기화되었는지 확인
	ASSERT(swap_block != NULL);  // 스왑 블록 장치가 초기화되었는지 확인

	// 스왑 슬롯의 섹터 시작 인덱스를 계산한다.
  // 스왑 디스크의 슬롯은 페이지 단위로 관리되며, 각 페이지는 여러 섹터로 나뉜다.
	size_t sector_offset = swap_index * SECTORS_PER_PAGE;

	// 페이지 데이터를 섹터 단위로 분할하여 각 섹터를 처리한다.
	for (size_t i = 0; i < SECTORS_PER_PAGE; ++i) {
		/**
		 * 현재 섹터에 해당하는 페이지 내 주소 계산
		 * page_addr : 페이지의 시작 주소
		 * i * BLOCK_SECTOR_SIZE : 현재 섹터의 offset
		 */
		void *sector_addr = (uint8_t *)page_addr + i * BLOCK_SECTOR_SIZE;

		if (is_read) { // 읽기 작업: 페이지 폴트(page fault)가 발생했을 때, 스왑 블록에서 메모리로 데이터 읽기
			// swap_block에서 sector_offset + i 섹터의 데이터를 읽어와 sector_addr에 저장.
			block_read(swap_block, sector_offset + i, sector_addr);
		} 
		else { // 쓰기 작업: eviction이 필요할 때, 메모리에서 스왑 블록으로 데이터 쓰기
			// sector_addr의 데이터를 swap_block의 sector_offset + i 섹터에 저장.
			block_write(swap_block, sector_offset + i, sector_addr);
		}
	}
}

