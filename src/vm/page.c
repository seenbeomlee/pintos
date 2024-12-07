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

static bool read_file_to_memory(void* kernel_address, struct spt_entry* page_entry);
static void zero_unused_memory(void* kernel_address, size_t read_bytes, size_t zero_bytes);

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
  // 물리 메모리 주소가 유효한지 확인
  // kernel_address가 NULL이면 메모리에 데이터를 저장할 수 없으므로 프로그램 중단
  ASSERT(kernel_address != NULL);

  // Supplementary Page Table(SPT) 엔트리가 유효한지 확인
  // page_entry가 NULL이면 파일 데이터를 읽을 메타정보가 없으므로 프로그램 중단
  ASSERT(page_entry != NULL);

  // Supplementary Page Table(SPT) 엔트리의 페이지 타입이 올바른지 확인
  // 지원하는 타입은 VM_BIN(실행 파일)과 VM_FILE(메모리 매핑 파일)만 해당
  // 다른 타입(VM_ANON 등)이 들어오면 load_file이 처리할 수 없으므로, 이를 사전에 차단
  ASSERT(page_entry->type == VM_BIN || page_entry->type == VM_FILE);

  // 파일에서 읽기
  if (!read_file_to_memory(kernel_address, page_entry)) {
    return false;
  }

  // 남은 공간(zero_bytes)을 0으로 초기화
  zero_unused_memory(kernel_address, page_entry->read_bytes, page_entry->zero_bytes);

  return true;
}

// 파일에서 데이터를 물리 메모리에 읽기
static bool read_file_to_memory(void* kernel_address, struct spt_entry* page_entry) {
  int bytes_read = file_read_at(
    page_entry->file,
    kernel_address,
    page_entry->read_bytes,
    page_entry->offset
  );

  // 실제 값 : bytes_read == 실제로 파일에서 읽어들인 바이트 수
  // 예상 값 : page_entry->read_bytes == 해당 페이지에서 읽어야 할 데이터 크기
  return bytes_read == (int)page_entry->read_bytes;
}

// 물리 메모리에서 남은 공간 0으로 초기화
static void zero_unused_memory(void* kernel_address, size_t read_bytes, size_t zero_bytes) {
  // kernel_address + read_bytes : 파일 데이터를 읽어들인 직후의 메모리 위치, 즉, 읽지 않은 페이지의 시작 부분
  // zero_bytes : 해당 영역의 크기만큼 메모리를 0으로 채운다.
  memset(kernel_address + read_bytes, 0, zero_bytes);
}
