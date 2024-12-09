#include <stddef.h>

// 한 페이지 단위(4KB) / 하드웨어 데이터 처리 단위(512bytes) == 8이 된다.
#define SECTORS_PER_PAGE 8

/**
 * 비트맵(bitmap)에서 '0'과 '1' 상태를 나타내는 관용적인 표현으로 사용된다.
 * SWAP_FREE = 0 -> 해당 비트가 사용 가능한 상태임을 의미한다.
 * SWAP_USER = 0 -> 해당 비트가 사용 불가능한 상태임을 의미한다.
 */
#define SWAP_FREE 0
#define SWAP_USED 1

// swap 영역 초기화
// init.c > int main() > vm_init()에서 사용된다.
void swap_init(void);

/** swap_in : used_index의 swap slot에 저장된 데이터를 kernal_addr 복사
 * exception.c > page_fault() > resolve_page_fault() > process_page_type() > VM_ANON 
 */
void swap_in(size_t used_index, void* kernal_addr);

/** kernal_addr 주소가 가리키는 페이지를 swap partition에 기록하고 page를 기록한 swap slot 번호를 return한다.
 * frame.c > allocate_frame()시, 물리 메모리 공간 부족하다면 
 *      > resolve_memory_shortage() > evict_frame() > process_eviction()에서 VM_ANON일 경우 swap_out()을 진행한다.
 */
size_t swap_out(void* kernal_addr);

