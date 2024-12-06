#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static void check_address(void* vaddr);
static void check_valid_buffer(const char *buffer, unsigned size, bool to_write);
static void rm_spt_umap(struct mmap_file* mmap_file);

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

/** multi-oom
 * start_process()에서 load()가 실패할 경우 exit(-1)을 한다.
 * 하지만, 이렇게 메모리에 적재 중인 자식 프로세스가 적재에 실패해서 종료될 경우, 부모 프로세스에서는 자식 프로세스의 적재 실패를 알 수 없다.
 * 따라서, 적재 실패(load_flag == false) 시 exec 자체에서 -1을 return 해줘야한다.
 */
pid_t
exec(const char *cmd_line) 
{
  tid_t tid;
  struct thread* t;

  tid = process_execute(cmd_line);
  t = find_child_thread(tid);
  // 자식 프로세스가 문제없이 생성되었으면 그 자식 프로세스가 메모리에 적재될 때까지 대기한다.
  if (t != NULL) {
    sema_down(&(t->load_sema));
    // 프로그램 적재 실패 시, -1 리턴
    if(t->load_flag==false) {
      return -1;
    }
    // 프로그램 적재 성공 시, child_tid 리턴
    else {
      return tid;
    }
  }
  else {
    return -1;
  }
}

int
wait(pid_t pid)
{
  return process_wait(pid); // 특정 자식 프로세스의 종료를 기다리고 
}

int 
read(int fd, void *buffer, unsigned int size)
{
  int result;
  uint8_t temp;
  if(fd<0 || fd==1 || fd>=FDTABLE_SIZE){exit(-1);}

  /* for read-bad-ptr */
  check_address(buffer);

  lock_acquire(&filesys_lock);
  if(fd==0){
    for(result=0;(result<size) && (temp=input_getc());result++){
      *(uint8_t*)(buffer+result)=temp;
    }
  }
  else{
    struct file* f=process_get_file(fd);
    if(f==NULL){
      lock_release(&filesys_lock);
      exit(-1);
    }
    result=file_read(f, buffer, size);
  }
  lock_release(&filesys_lock);
  return result;
}

int 
write (int fd, const void *buffer, unsigned size) 
{
  int file_write_result;
  struct file* f;
  if(fd<=0 || fd>=FDTABLE_SIZE){exit(-1);}

  /* for read-bad-ptr */
  check_address(buffer);

  lock_acquire(&filesys_lock);
  if(fd==1){
    putbuf(buffer, size);
    lock_release(&filesys_lock);
    return size;
  }
  else{
    f=process_get_file(fd);
    if(f==NULL){
      lock_release(&filesys_lock);
      exit(-1);
    }
    file_write_result=file_write(f, buffer, size);
    lock_release(&filesys_lock);
    return file_write_result;
  }
}

/** pintos manual 3.15
 * Accessing User Memory - bad address checking
 * 1. NULL pointer such as open(NULL)
 * 2. Unmapped virtual memory
 * 3. pointer to kernel address space 
 */
void
check_address(void* vaddr) {
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

int 
open (const char* file)
{
  int fd;
  struct file* f;
  if(file==NULL){exit(-1);}
  lock_acquire(&filesys_lock);
  f=filesys_open(file);
  if(f==NULL){
    lock_release(&filesys_lock);
    return -1;
  }
  fd=process_add_file(f);
  lock_release(&filesys_lock);
  return fd;
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

void 
close (int fd)
{
  process_file_close(fd);
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

/* ********** ********** ********** procject 3 : virtual memory ********** ********** ***********/


int mmap(int fd, void* addr){

  struct mmap_file *mmap_file;
  size_t offset = 0;

  if (pg_ofs (addr) != 0 || !addr)
    return -1;
  if (is_user_vaddr (addr) == false)
    return -1;
  mmap_file = (struct mmap_file *)malloc (sizeof (struct mmap_file));
  if (mmap_file == NULL)
    return -1;
  memset (mmap_file, 0, sizeof(struct mmap_file));
  list_init (&mmap_file->spe_list);
  if (!(mmap_file->file = thread_current()->fd_table[fd]))
    return -1;
  mmap_file->file = file_reopen(mmap_file->file);
  mmap_file->mapid = thread_current ()->next_mapid++;
  list_push_back (&thread_current ()->mmap_list, &mmap_file->elem);

  int length = file_length (mmap_file->file);
  while (length > 0)
    {
      if (find_spe (addr))
        return -1;

      struct spt_entry *spe = (struct spt_entry *)malloc (sizeof (struct spt_entry));
      memset (spe, 0, sizeof (struct spt_entry));
      spe->type = VM_FILE;
      spe->writable = true;
      spe->vaddr = addr;
      spe->offset = offset;
      spe->read_bytes = length < PGSIZE ? length : PGSIZE;
      spe->zero_bytes = PGSIZE - spe->read_bytes;
      spe->file = mmap_file->file;
      insert_spe (&thread_current ()->spt, spe);
      list_push_back (&mmap_file->spe_list, &spe->mmap_elem);
      addr += PGSIZE;
      offset += PGSIZE;
      length -= PGSIZE;
    }
  return mmap_file->mapid;
}

void munmap(int mapid) {

  struct mmap_file *mmap_file;
  struct thread* t = thread_current();

  struct list_elem* elem,  *temp;
  for(elem = list_begin(&t->mmap_list) ; elem != list_end(&t->mmap_list) ; elem = list_next(elem)){
    mmap_file = list_entry(elem, struct mmap_file, elem);

    /**
     * mapid == -1이면, 모든 매핑된 파일에 대해서
     * 1. rm_spt_umap을 호출하여 spt에서 매핑된 페이지를 제거한다.
     * 2. 매핑된 파일을 닫는다 (file_close)
     * 3. mmap_file 구조체를 리스트에서 제거하고 메모리를 해제한다.
     */
    if(mapid==-1) {
      rm_spt_umap(mmap_file);
      file_close(mmap_file->file);
      temp = list_prev(elem);
      list_remove(elem);
      elem = temp;
      free(mmap_file);
      continue;
    }
    /**
     * mapid == 특정 mapid가 주어진 경우,
     * 해당 mapid에 매핑된 파일에 대해서만 같은 작업을 처리하고 종료한다.
     */
    else if(mapid == mmap_file->mapid){
      rm_spt_umap(mmap_file);
      file_close(mmap_file->file);
      temp = list_prev(elem);
      list_remove(elem);
      elem = temp;
      free(mmap_file); 
      break;
    }
  }

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

static void check_valid_buffer(const char *buffer, unsigned size, bool to_write) {
  ASSERT(buffer!=NULL);

  unsigned i;
  struct spt_entry* spe;
  for(i=0;i<size;i++){
    check_address((void*)buffer+i);
    spe = find_spe((void*)buffer+i);
    if(spe == NULL) {
      if(to_write){
        if(!(spe->writable)){
            exit(-1);
        }
      }
    }
  }
}

static void rm_spt_umap(struct mmap_file* mmap_file) {
    struct thread *t = thread_current();
    struct list_elem *elem, *temp;
    struct list *spe_list = &(mmap_file->spe_list);
    struct spt_entry *spe;
    void* kaddr;

    elem = list_begin(spe_list);

    for(; elem != list_end(spe_list); elem = list_next(elem)){
      spe = list_entry(elem, struct spt_entry, mmap_elem);
      if(spe->is_loaded==true){
        kaddr = pagedir_get_page(t->pagedir, spe->vaddr);
        // if dirty bit true, write to disk
        if(pagedir_is_dirty(t->pagedir, spe->vaddr)==true){
          lock_acquire(&filesys_lock);
          file_write_at(spe->file, spe->vaddr, spe->read_bytes, spe->offset);
          lock_release(&filesys_lock);
          spe->is_loaded = false;
        }
        // clear page table
        pagedir_clear_page(t->pagedir, spe->vaddr);
        //printf("before free page?\n");
        free_page(kaddr);
        //printf("after free page?\n");
      }
      temp = list_prev(elem);
      list_remove(elem);
      elem = temp;
      delete_spe(&t->spt, spe);
    }
}