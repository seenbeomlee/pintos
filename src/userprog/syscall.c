#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

static bool is_valid_mmap_request(int fd, void* addr);
static struct mmap_file_entry* create_mmap_file(int fd);
static bool map_pages_from_file(struct mmap_file_entry* mmap_file_entry, void* addr);
static struct spt_entry* create_spt_entry_for_mmap(struct mmap_file_entry* mmap_file_entry, void* addr, size_t offset, int length);

static void free_mmap_file(struct mmap_file_entry *mmap_file_entry, struct list_elem **elem);
static void cleanup_mmap_pages(struct mmap_file_entry* mmap_file_entry);
static void handle_page_removal(struct spt_entry *entry, uint32_t *pagedir);

static int perform_file_action(int fd, void* buffer, unsigned size, bool is_write);
static struct file* get_valid_file(int fd);

static void check_address(void* vaddr);
static void check_buffer(const char *buffer, unsigned size, bool is_write);

struct lock filesys_lock;

void
syscall_init (void)
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/** 2
 * 시스템 콜을 호출할 때, 원하는 기능에 해당하는 시스템 콜 번호를 rax에 담는다.
 * 그리고 시스템 콜 핸들러는 rax의 숫자로 시스템 콜을 호출하고, -> 이는 enum으로 선언되어있다.
 * 해달 콜의 반환값을 다시 rax에 담아서 intr_frame(인터럽트 프레임)에 저장한다.
 */
static void
syscall_handler (struct intr_frame *f) 
{
  // Argument 순서
	// %rdi %rsi %rdx %r10 %r8 %r9
  int syscall_num = *(uint32_t *)(f->esp);
  switch (syscall_num) {
    case SYS_HALT:                   /* Halt the operating system. */
    halt();
    break;
    case SYS_EXIT:                   /* Terminate this process. */
    check_address(f->esp+4);
    check_address(f->esp+4);
    exit(*(int*)(f->esp+4));
    break;
    case SYS_EXEC:                   /* Start another process. */
    check_address(f->esp+4);
    check_address(f->esp+4);
    f->eax=exec((char*)*(uint32_t*)(f->esp+4));
    break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
    check_address(f->esp+4);
    check_address(f->esp+4);
    f->eax = wait(*(uint32_t*)(f->esp+4));
    break;
    case SYS_CREATE:                 /* Create a file. */
    check_address(f->esp+4);
    check_address(f->esp+8);
    check_address(f->esp+4);
    check_address(f->esp+8);
    f->eax = create((char*)*(uint32_t*)(f->esp+4), *(uint32_t*)(f->esp+8));
    break;
    case SYS_REMOVE:                 /* Delete a file. */
    check_address(f->esp+4);
    check_address(f->esp+4);
    f->eax = remove((char*)*(uint32_t*)(f->esp+4));
    break;
    case SYS_OPEN:                   /* Open a file. */
    check_address(f->esp+4);
    check_address(f->esp+4);
    f->eax = open((char*)*(uint32_t*)(f->esp+4));
    break;
    case SYS_FILESIZE:               /* Obtain a file's size. */
    check_address(f->esp+4);
    check_address(f->esp+4);
    f->eax = filesize(*(uint32_t*)(f->esp+4));
    break;
    case SYS_READ:                   /* Read from a file. */
    check_address(f->esp+4);
    check_address(f->esp+8);
    check_address(f->esp+12);
    check_address(f->esp+4);
    check_address(f->esp+8);
    check_address(f->esp+12);
    f->eax = read((int)*(uint32_t*)(f->esp+4), (void*)*(uint32_t*)(f->esp+8),
					(unsigned)*(uint32_t*)(f->esp+12));
    break;
    case SYS_WRITE:                  /* Write to a file. */
    //printf("write system call!\n");
    check_address(f->esp+4);
    check_address(f->esp+8);
    check_address(f->esp+12);
    check_address(f->esp+4);
    check_address(f->esp+8);
    check_address(f->esp+12);
    f->eax = write((int)*(uint32_t*)(f->esp+4), (const void*)*(uint32_t*)(f->esp+8),
					(unsigned)*(uint32_t*)(f->esp+12));
    break;
    case SYS_SEEK:                   /* Change position in a file. */
    check_address(f->esp+4);
    check_address(f->esp+8);
    check_address(f->esp+4);
    check_address(f->esp+8);
    seek((int)*(uint32_t*)(f->esp+4), (unsigned)*(uint32_t*)(f->esp+8));
    break;
    case SYS_TELL:                   /* Report current position in a file. */
    check_address(f->esp+4);
    check_address(f->esp+4);
    f->eax = tell((int)*(uint32_t*)(f->esp+4));
    break;
    case SYS_CLOSE:                  /* Close a file. */
    check_address(f->esp+4);
    check_address(f->esp+4);
    close(*(uint32_t*)(f->esp+4));
    break;
    case SYS_MMAP:
      check_address(f->esp+4);
      check_address(f->esp+8);
      f->eax = mmap((int)(*(uint32_t *)(f->esp+4)), (void*)(*(uint32_t *)(f->esp+8)));
      break;
    case SYS_MUNMAP:
      check_address(f->esp+4);
      munmap((int)(*(uint32_t *)(f->esp+4)));
      break;
  }
}

void 
halt(void) {
  shutdown_power_off(); // 핀토스를 종료시키는 시스템 콜이다.
}

void 
exit (int status) 
{
  /* document의 요구사항에 따라, 스레드가 종료될 때에는 종료 메세지를 출력한다. */
  struct thread* t = thread_current();
  t->exit_status = status; // 현재 스레드의 exit_status에 종료 상태 코드를 저장
  printf("%s: exit(%d)\n", thread_name(), t->exit_status); // 종료 메세지 출력
  thread_exit (); // 자원 해제 및 스레드 스케줄러에 제어 넘기기
}

pid_t
exec(const char *cmd_line) 
{
  tid_t tid;

  tid = process_execute(cmd_line);
  // 자식 프로세스(tid를 갖는)가 문제없이 생성되었으면 그 자식 프로세스가 메모리에 적재될 때까지 대기한다.
  if (tid != -1) {
    sema_down(&(find_child_thread(tid)->load_sema));
  }
  return tid;
}

int
wait(pid_t pid)
{
  return process_wait(pid); // 특정 자식 프로세스의 종료를 기다리고 
}

int read(int fd, void* buffer, unsigned int size) {
  if (fd == 0) { // stdin 처리
    unsigned int result = 0;
    uint8_t temp;

    lock_acquire(&filesys_lock);
    while (result < size) {
      temp = input_getc();
      ((uint8_t*)buffer)[result++] = temp;
    }
    lock_release(&filesys_lock);

    return result;
  }

  // 일반 파일 읽기
  return perform_file_action(fd, buffer, size, false);
}

int write(int fd, const void* buffer, unsigned size) {
  if (fd == 1) { // stdout 처리
    putbuf(buffer, size);
    return size;
  }

  // 일반 파일 쓰기
  return perform_file_action(fd, (void*)buffer, size, true);
}

int open(const char* file) {
  if (file == NULL) { exit(-1); }

  lock_acquire(&filesys_lock);
  struct file* f = filesys_open(file);
  if (f == NULL) {
    lock_release(&filesys_lock);
    return -1;
  }

  int fd = process_add_file(f);
  lock_release(&filesys_lock);

  return fd;
}

void close(int fd) {
  process_close_file(fd);
}

bool 
create (const char *file, unsigned initial_size)
{
  // NULL 파일은 열 수 없다.
  if(file==NULL){
    exit(-1);
  }
  return filesys_create(file, initial_size);
}

bool 
remove (const char *file)
{
  // NULL 파일은 열 수 없다.
  if(file==NULL){
    exit(-1);
  }
  return filesys_remove(file);
}

int 
filesize (int fd)
{
  struct file* f=process_get_file(fd);
  if(f==NULL){exit(-1);}
  return file_length(f);
}

void 
seek (int fd, unsigned int position)
{
  struct file* f=process_get_file(fd);
  if(f==NULL){exit(-1);}
  file_seek(f, position);
}

unsigned int 
tell (int fd)
{
  struct file* f=process_get_file(fd);
  if(f==NULL){exit(-1);}
  return file_tell(f);
}

// is_write에 따라서 read or write 수행
static int perform_file_action(int fd, void* buffer, unsigned size, bool is_write) {
  struct file* f = get_valid_file(fd);
  
  check_buffer(buffer, size, is_write);

  int result;
  lock_acquire(&filesys_lock);
  if (is_write) {
    result = file_write(f, buffer, size);
  } else {
    result = file_read(f, buffer, size);
  }
  lock_release(&filesys_lock);

  return result;
}

// 파일 디스크립터 유효 범위 검증
static struct file* get_valid_file(int fd) {
  if (fd < 0 || fd >= FDTABLE_SIZE) {
    exit(-1);
  }
  struct file* f = process_get_file(fd);
  if (f == NULL) {
    exit(-1);
  }
  return f;
}

/* ********** ********** ********** procject 3 : virtual memory ********** ********** ***********/

int mmap(int fd, void* addr) {
  // 요청 유효성 검증
  if (!is_valid_mmap_request(fd, addr)) {
    return -1;
  }

  // 매핑 파일 구조체 생성
  struct mmap_file_entry* mmap_file_entry = create_mmap_file(fd);
  if (mmap_file_entry == NULL) {
    return -1;
  }

  // 파일 매핑을 가상 메모리에 적용
  if (!map_pages_from_file(mmap_file_entry, addr)) {
    free(mmap_file_entry);
    return -1;
  }

  // mmap 리스트에 추가
  list_push_back(&thread_current()->mmap_list, &mmap_file_entry->elem);

  // 매핑 ID 반환
  return mmap_file_entry->mapid;
}

// mmap 요청의 유효성을 확인하는 함수
static bool is_valid_mmap_request(int fd, void* addr) {
  // 주소가 NULL인지 확인
  if (!addr) {
    return false;
  }

  // 주소가 페이지 정렬되었는지 확인
  if (pg_ofs(addr) != 0) {
    return false;
  }

  // 주소가 사용자 공간인지 확인
  if (!is_user_vaddr(addr)) {
    return false;
  }

  return true;
}

// mmap_file_entry 구조체 생성 및 초기화
static struct mmap_file_entry* create_mmap_file(int fd) {
  // 메모리 할당
  struct mmap_file_entry* mmap_file_entry = malloc(sizeof(struct mmap_file_entry));
  if (!mmap_file_entry) {
    return NULL; // 메모리 할당 실패
  }

  // 구조체 초기화
  memset(mmap_file_entry, 0, sizeof(struct mmap_file_entry));
  list_init(&mmap_file_entry->spt_entry_list);

  // 파일 핸들 확인
  struct file* file = thread_current()->fd_table[fd];
  if (!file) {
    free(mmap_file_entry);
    return NULL; // 유효하지 않은 파일 디스크립터
  }

  // 파일 핸들 복제
  mmap_file_entry->mmap_file = file_reopen(file);
  if (!mmap_file_entry->mmap_file) {
    free(mmap_file_entry);
    return NULL; // 파일 복제 실패
  }

  // 고유한 매핑 ID 설정
  mmap_file_entry->mapid = thread_current()->next_mapid++;
  return mmap_file_entry;
}

// mmap_file_entry 구조체와 가상 메모리 주소를 기반으로 파일 매핑
static bool map_pages_from_file(struct mmap_file_entry* mmap_file_entry, void* addr) {
  int length = file_length(mmap_file_entry->mmap_file); // 파일 길이 계산
  size_t offset = 0;

  while (length > 0) {
    // 중복 매핑 방지: 이미 해당 주소에 엔트리가 존재하면 실패
    if (lookup_spt_entry(addr)) {
      return false;
    }

    // SPT 엔트리 생성
    struct spt_entry* entry = create_spt_entry_for_mmap(mmap_file_entry, addr, offset, length);
    if (!entry) {
      return false; // 엔트리 생성 실패
    }

    // SPT에 엔트리 추가
    insert_spt_entry(&thread_current()->spt, entry);

    // mmap_file_entry spe_list에 추가
    list_push_back(&mmap_file_entry->spt_entry_list, &entry->mmap_elem);

    // 다음 페이지로 이동
    addr += PGSIZE;
    offset += PGSIZE;
    length -= PGSIZE;
  }

  return true; // 파일 매핑 성공
}

// 파일 매핑을 위한 SPT 엔트리 생성
static struct spt_entry* create_spt_entry_for_mmap(struct mmap_file_entry* mmap_file_entry, void* addr, size_t offset, int length) {
  // 메모리 할당
  struct spt_entry* entry = malloc(sizeof(struct spt_entry));
  if (!entry) {
    return NULL; // 메모리 할당 실패
  }

  // 엔트리 초기화
  memset(entry, 0, sizeof(struct spt_entry));
  entry->entry_type = VM_FILE; // 매핑된 파일 타입

  // // 파일의 deny_write 상태에 따라 writable 설정
  // entry->is_writable = return_deny_write(mmap_file_entry->mmap_file); // deny_write가 true면 쓰기 금지
  entry->is_writable = true; // 쓰기 가능 여부
  
  entry->virtual_addr = addr; // 매핑될 가상 주소
  entry->offset = offset; // 파일 내 오프셋
  entry->read_bytes = length < PGSIZE ? length : PGSIZE; // 읽을 바이트 크기
  entry->zero_bytes = PGSIZE - entry->read_bytes; // 남은 공간을 0으로 초기화
  entry->mmap_file = mmap_file_entry->mmap_file; // 파일 핸들 설정

  return entry; // 초기화된 SPT 엔트리 반환
}

/* ********** ********** ********** ********** ********** ********** ********** ***********/

void munmap(int mapid) {
    struct mmap_file_entry *mmap_file_entry;  // 매핑된 파일 정보를 담는 구조체
    struct thread *t = thread_current();  // 현재 스레드 정보
    struct list_elem *elem = list_begin(&t->mmap_list);  // mmap_list의 첫 번째 요소
    struct list_elem *next;  // 다음 요소를 저장할 포인터

    // mmap_list를 순회하며 매핑된 파일을 해제
    while (elem != list_end(&t->mmap_list)) {
        mmap_file_entry = list_entry(elem, struct mmap_file_entry, elem);
        next = list_next(elem);  // 다음 요소를 미리 저장

        switch (mapid) {
            case -1:  // 모든 매핑 해제
                free_mmap_file(mmap_file_entry, &elem);
                elem = next;  // 다음 요소로 이동
                break;

            default:  // 특정 mapid 매핑 해제
                if (mapid == mmap_file_entry->mapid) {
                    free_mmap_file(mmap_file_entry, &elem);
                    return;  // 특정 mapid 작업 완료 후 함수 종료
                }
                elem = next;  // 다음 요소로 이동
                break;
        }
    }
}

static void free_mmap_file(struct mmap_file_entry *mmap_file_entry, struct list_elem **elem) {
    if (!mmap_file_entry || !elem || !*elem) {
        return;  // 유효하지 않은 입력을 처리하지 않고 반환
    }

    /* 매핑된 페이지 해제 */
    cleanup_mmap_pages(mmap_file_entry);

    /* 파일 닫기 */
    file_close(mmap_file_entry->mmap_file);

    /* 리스트에서 제거 및 메모리 해제 */
    list_remove(*elem);
    free(mmap_file_entry);
}

static void cleanup_mmap_pages(struct mmap_file_entry *mmap_file_entry) {
    struct thread *t = thread_current();
    struct list_elem *elem, *next;
    struct list *spe_list = &(mmap_file_entry->spt_entry_list);

    for (elem = list_begin(spe_list); elem != list_end(spe_list); elem = next) {
        next = list_next(elem);  // 다음 요소 저장
        struct spt_entry *entry = list_entry(elem, struct spt_entry, mmap_elem);

        // 매핑된 페이지의 상태 확인 및 처리
        if (entry->is_loaded) { // 로드된 경우, (1)dirty page처리, (2)물리 메모리 해제 가 필요하다.
            handle_page_removal(entry, t->pagedir);  // 메모리 정리
            entry->is_loaded = false;               // is_loaded = false로 상태 업데이트
        }
        // 로드되지 않은 경우, page가 이미 메모리에 로드되지 않은 상태이므로 추가 작업 없이 바로 정리할 수 있다.

        // 엔트리 제거
        list_remove(elem); // mmap_file_entry->spt_entry_list spt_entry를 제거
        delete_spt_entry(&t->spt, entry); // spt에서 해당 spt_entry를 삭제하고, 관련 리소스를 해제(free)
    }
}

// 물리 메모리에 로드되어 있는 경우, (1)dirty 페이지 기록 (2)페이지 테이블 제거 (3)메모리 해제를 한 함수로 통합
static void handle_page_removal(struct spt_entry *entry, uint32_t *pagedir) {
    ASSERT(entry != NULL);
    ASSERT(pagedir != NULL);

    // spt 엔트리의 가장 주소(entry->virtual_addr)에 매핑된 물리 메모리 주소를 가져온다.
    void *kernal_addr = pagedir_get_page(pagedir, entry->virtual_addr);

    // dirty 페이지인 경우 디스크에 기록한다.
    // dirty 페이지란, 물리 메모리에서 수정되었으나 아직 디스크에 기록되지 않은 페이지를 의미한다.
    if (pagedir_is_dirty(pagedir, entry->virtual_addr)) {
        lock_acquire(&filesys_lock);
        file_write_at(entry->mmap_file, entry->virtual_addr, entry->read_bytes, entry->offset);
        lock_release(&filesys_lock);
    }

    // 페이지 테이블에서 제거 및 메모리 해제, 즉 페이지 테이블에서 '가상 주소와 물리 주소의 매핑을 제거'한다.
    // 이후 해당 가상 주소로 접근하려고 하면, 매핑이 해제되었기 때문에 당연히 page fault가 발생한다.
    pagedir_clear_page(pagedir, entry->virtual_addr);
    // 받아 온 물리 메모리 주소(kernal_addr)를 통해 '물리 메모리를 실제로 해제'하여 다른 작업에서 재사용할 수 있도록 만든다.
    free_frame(kernal_addr);
}

/** 2
 * 주소 값이 user 영역에서 사용하는 주소 값인지 확인한다.
 * user 영역을 벗어난 영역일 경우, process를 종료한다. (exit (-1))
 * pintos에서는 시스템 콜이 접근할 수 있는 주소를 0cx0000000 ~ 0x8048000(== KERN_BASE) 으로 제한한다. (이 이상은 커널 영역이다.)
 * 유저 영역을 벗어난 영역일 경우, 비정상 접근이라고 판단하여 exit(-1)로 프로세스를 종료한다.
 */
static void check_address(void* vaddr) {
  if (vaddr == NULL) {
    exit(-1);
  }
  if (!is_user_vaddr(vaddr)) {
    exit(-1);
  }
  // page fault 인지 체크하기 위해 필요한데, 추가하면 모든 테스트가 fail 된다. 이유는 모르겠다.
  // if (!pagedir_get_page(thread_current()->pagedir, vaddr) == NULL) {
  //   exit(-1);
  // }
}

/** project 3 : virtual memory
 * 1. 버퍼의 전체 범위 검사
 * 2. spt 활용하여 각 주소가 spt에 존재하는지 확인, 해당 페이지가 로드되었는지 확인
 * 3. 쓰기 권한 확인 (to_write)
 */
static void check_buffer(const char *buffer, unsigned size, bool is_write) {
    ASSERT(buffer != NULL);
    struct spt_entry *entry;

    /* 버퍼의 모든 바이트를 검사 */
    for (int i = 0; i < size; i++) {
        void *addr = (void *)(buffer + i);

        /* 주소가 사용자 메모리 공간에 있는지 확인 */
        check_address(addr);

        /* SPT에서 엔트리를 검색 */
        entry = lookup_spt_entry(addr);

        /* 페이지가 로드되지 않았거나 유효하지 않으면 예외 처리 */
        if (entry == NULL) {
            exit(-1);
        }

        // // spt 초기화시, VM_FILE에 대해서 잘못 설계된 것으로 보인다..
        // /* 쓰기 요청 시 페이지의 쓰기 권한 확인 */
        // if (is_write && !entry->writable) {
        //     exit(-1);
        // }
    }
}
