#include "vm/page.h"
#include <stdio.h>
#include <string.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"

extern struct lock filesys_lock;

static unsigned spt_hash_func(const struct hash_elem* element, void* aux);
static bool spt_less_func(const struct hash_elem* first, const struct hash_elem* second, void* aux);

static struct hash_elem* find_hash_elem(struct hash* supplementary_table, void* virtual_address);

static void spt_destroy_func(struct hash_elem* element, void* aux UNUSED);
static void release_spt_entry(struct spt_entry* entry);
static void handle_loaded_page(struct spt_entry* entry);

// Hash 테이블 초기화
void spt_init(struct hash* supplementary_table) {
    ASSERT(supplementary_table != NULL);
    hash_init(supplementary_table, spt_hash_func, spt_less_func, NULL);
}

// 해시 함수: 구조체에서 해시 값을 생성
static unsigned spt_hash_func(const struct hash_elem* element, void* aux UNUSED) {
    ASSERT(element != NULL);
    struct spt_entry* entry = hash_entry(element, struct spt_entry, elem);
    return hash_int((int)entry->vaddr);
}

// 해시 비교 함수
static bool spt_less_func(const struct hash_elem* first, const struct hash_elem* second, void* aux UNUSED) {
    struct spt_entry* entry_a = hash_entry(first, struct spt_entry, elem);
    struct spt_entry* entry_b = hash_entry(second, struct spt_entry, elem);
    return entry_a->vaddr < entry_b->vaddr;
}

// Supplementary Page Entry 삽입
bool insert_spe(struct hash* supplementary_table, struct spt_entry* new_entry) {
    ASSERT(supplementary_table != NULL);
    ASSERT(new_entry != NULL);
    ASSERT(pg_ofs(new_entry->vaddr) == 0);

    return hash_insert(supplementary_table, &new_entry->elem) == NULL;
}

// Supplementary Page Entry 삭제
bool delete_spe(struct hash* supplementary_table, struct spt_entry* target_entry) {
    ASSERT(supplementary_table != NULL);
    ASSERT(target_entry != NULL);

    if (!hash_delete(supplementary_table, &target_entry->elem)) {
        return false;
    }
    free(target_entry);
    return true;
}

// Supplementary Page Entry 검색
struct spt_entry* find_spe(void* virtual_address) {
    struct hash* spt = &(thread_current()->spt);
    struct hash_elem* element = find_hash_elem(spt, virtual_address);
    return element ? hash_entry(element, struct spt_entry, elem) : NULL;
}

// 해시 엔트리 검색
static struct hash_elem* find_hash_elem(struct hash* supplementary_table, void* virtual_address) {
    struct spt_entry temp_entry;
    temp_entry.vaddr = pg_round_down(virtual_address);
    ASSERT(pg_ofs(temp_entry.vaddr) == 0);
    return hash_find(supplementary_table, &temp_entry.elem);
}

// Hash 테이블 제거
void spt_destroy(struct hash* supplementary_table) {
  ASSERT(supplementary_table != NULL);

  // Hash 테이블 엔트리를 제거하며 관련 자원도 정리
  hash_destroy(supplementary_table, spt_destroy_func);
}

// Hash 요소 제거 함수
static void spt_destroy_func(struct hash_elem* element, void* aux UNUSED) {
  ASSERT(element != NULL);

  struct spt_entry* entry = hash_entry(element, struct spt_entry, elem);
  release_spt_entry(entry);
}

// Supplementary Page Table Entry 해제
static void release_spt_entry(struct spt_entry* entry) {
  ASSERT(entry != NULL);

  if (entry->is_loaded) {
    handle_loaded_page(entry);  // 로드된 페이지 처리
  } else {
    free(entry);  // 로드되지 않은 엔트리 메모리 해제
  }
}

// 로드된 페이지 처리
static void handle_loaded_page(struct spt_entry* entry) {
  ASSERT(entry != NULL);

  // 페이지 테이블에서 물리 주소를 검색
  void* kernel_address = pagedir_get_page(thread_current()->pagedir, entry->vaddr);

  // 페이지 메모리 해제 및 테이블에서 제거
  free_page(kernel_address);
  pagedir_clear_page(thread_current()->pagedir, entry->vaddr);

  // 엔트리 메모리 해제
  free(entry);
}


// 파일에서 데이터를 로드하여 물리 메모리에 저장
bool load_file(void* kernel_address, struct spt_entry* page_entry) {
    ASSERT(kernel_address != NULL);
    ASSERT(page_entry != NULL);
    ASSERT(page_entry->type == VM_BIN || page_entry->type == VM_FILE);

    int bytes_read = file_read_at(page_entry->file, kernel_address, page_entry->read_bytes, page_entry->offset);
    if (bytes_read != (int)page_entry->read_bytes) {
        return false;
    }

    memset(kernel_address + page_entry->read_bytes, 0, page_entry->zero_bytes);
    return true;
}
