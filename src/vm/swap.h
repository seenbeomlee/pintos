#include <stddef.h>

#define SECTORS_PER_PAGE 8

/**
 * 비트맵(bitmap)에서 '0'과 '1' 상태를 나타내는 관용적인 표현으로 사용된다.
 * SWAP_FREE = 0 -> 해당 비트가 사용 가능한 상태임을 의미한다.
 * SWAP_USER = 0 -> 해당 비트가 사용 불가능한 상태임을 의미한다.
 */
#define SWAP_FREE 0
#define SWAP_USED 1

// swap 영역 초기화
void swap_init(void);

// swap_in : used_index의 swap slot에 저장된 데이터를 kernal_addr 복사
void swap_in(size_t used_index, void* kernal_addr);

// kernal_addr 주소가 가리키는 페이지를 swap partition에 기록
// page를 기록한 swap slot 번호를 return
size_t swap_out(void* kernal_addr);

