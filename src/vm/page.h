#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include "lib/kernel/hash.h"
#include "filesys/file.h"
#include <list.h>
#include "threads/vaddr.h"

// 페이지 타입 정의

// 실행 파일과 연관된 페이지 타입
// 프로그램의 코드, 읽기 전용 데이터, 초기화된 전역 변수 등이 포함된 페이지
#define VM_BIN 0  

// 메모리 매핑된 파일과 연관된 페이지 타입
// mmap 시스템 호출로 매핑된 파일의 일부를 메모리에 로드하는 페이지
#define VM_FILE 1  

// 익명 페이지 타입
// 파일과 무관한 페이지로, 동적 메모리 할당(malloc), 스택 확장 등에서 사용
#define VM_ANON 2  

// Frame 정보를 나타내는 구조체
struct frame_entry {
  struct spt_entry* spt_entry; // 이 프레임에 매핑된 페이지 테이블 엔트리
  struct thread* owner_thread; // 이 프레임과 연관된 스레드
  struct list_elem lru_elem;  // LRU 리스트 탐색을 위한 리스트 요소

  void* kernal_addr;           // 물리 주소에 매핑된 프레임 주소
};


/** Supplementary Page Entry 구조체 
 * process.c > load_segment() : create
 */
struct spt_entry {
  struct file* mmap_file;   // 가상 주소와 매핑된 파일
  size_t offset;            // 파일에서 읽어야 할 오프셋
  size_t read_bytes;        // 페이지에 쓰여 있는 데이터 크기
  size_t zero_bytes;        // 0으로 채워야 할 나머지 페이지 크기

  uint8_t entry_type;       // 엔트리 타입 (VM_BIN, VM_FILE, VM_ANON 등)
  void* virtual_addr;       // 가상 페이지 주소

  bool is_writable;         // 쓰기가 가능한지 여부 => 테스트 케이스 통과를 못해서 사실상 필요없는 변수가 되어버림.
  bool is_loaded;           // 물리 메모리에 로드 여부

  size_t swap_index;        // 스왑 슬롯 인덱스

  struct hash_elem hash_elem; // 해시 테이블에 저장될 요소
  struct list_elem mmap_elem; // mmap 리스트와 연결된 리스트 요소
};

// Memory-mapped file 구조체
struct mmap_file_entry {
  struct file* mmap_file;          // 연결된 파일

  int mapid;                  // 이 파일의 mapid (식별)

  struct list spt_entry_list;       // mmap과 연관된 spt_entry 관리 리스트

  struct list_elem elem;      // 리스트 순회 및 검색을 위한 elem
};

/* ********** ********** ********** ********** ********** ********** ********** ***********/

/**
 * Supplementary Page Table (SPT) 관리 함수
 * process.c > start_process() 에서 사용된다.
 * 프로세스가 사용하는 가상 메모리 구조를 초기화하기 위해서 호출한다.
 * Supplemental Page Table(SPT)은 각 process가 별도로 관리해야 하므로, 
 * start_process()가 호출될 때 현재 process의 spt를 초기화한다.
 * 이를 통해 프로세스 간 가상 메모리 충돌을 방지하고 독립적ㅇ니 메모리 관리를 보장한다.
 */
void spt_init(struct hash* supplementary_table);

/* ********** ********** ********** ********** ********** ********** ********** ***********/

bool insert_spt_entry(struct hash* supplementary_table, struct spt_entry* new_entry);
bool delete_spt_entry(struct hash* supplementary_table, struct spt_entry* target_entry);

/* ********** ********** ********** ********** ********** ********** ********** ***********/

struct spt_entry* lookup_spt_entry(void* virtual_address);

/* ********** ********** ********** ********** ********** ********** ********** ***********/

void spt_destroy(struct hash* supplementary_table);

/* ********** ********** ********** ********** ********** ********** ********** ***********/

// 파일 로드 함수
// excepion.h가 아닌 page.h에 선언된 이유는 파일 로드 작업이 VM 시스템의 일환으로 spt에 맞춰져 있기 때문이다.
bool load_file(void* kernel_address, struct spt_entry* entry);

#endif /* VM_PAGE_H */
