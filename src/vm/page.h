#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include "lib/kernel/hash.h"
#include "filesys/file.h"
#include <list.h>
#include "threads/vaddr.h"

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

// Memory-mapped file 구조체
struct mmap_file {
    int mapid;                  // 이 파일의 mapid (식별)
    struct file* file;          // 연결된 파일
    struct list_elem elem;      // 리스트 순회 및 검색을 위한 elem
    struct list spe_list;       // mmap과 연관된 spt_entry 관리 리스트
};

// Frame 정보를 나타내는 구조체
struct page {
    void* kaddr;                // 이 페이지에 연결된 물리 주소
    struct spt_entry* spe;      // 이 페이지에 연결된 가상 주소를 가진 spt_entry
    struct thread* t;           // 이 페이지와 연관된 스레드
    struct list_elem lru;       // LRU 리스트 탐색을 위한 list_elem
};

// Supplementary Page Entry 구조체
struct spt_entry {
    uint8_t type;               // VM_BIN, VM_FILE, VM_ANON의 타입
    void* vaddr;                // 가상 페이지 번호

    bool writable;              // 쓰기가 가능한지 여부
    bool is_loaded;             // 물리 메모리에 로드 여부

    struct file* file;          // 가상 주소와 매핑된 파일
    size_t offset;              // 파일에서 읽어야 할 위치
    size_t read_bytes;          // 페이지에 쓰여 있는 데이터 크기
    size_t zero_bytes;          // 0으로 채워야 할 페이지의 나머지 크기

    struct hash_elem elem;      // 해시 테이블에 저장될 hash_elem
    struct list_elem mmap_elem; // mmap 리스트와 연결된 elem

    size_t swap_slot;           // 스왑 슬롯 번호
};

// Supplementary Page Table (SPT) 관리 함수
void spt_init(struct hash* supplementary_table);

bool insert_spe(struct hash* supplementary_table, struct spt_entry* new_entry);
bool delete_spe(struct hash* supplementary_table, struct spt_entry* target_entry);
struct spt_entry* find_spe(void* virtual_address);
void spt_destroy(struct hash* supplementary_table);

// 파일 로드 함수
bool load_file(void* kernel_address, struct spt_entry* page_entry);

#endif /* VM_PAGE_H */
