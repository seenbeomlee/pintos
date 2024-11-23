#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
struct lock filesys_lock;

void
syscall_init (void)
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int syscall_num = *(uint32_t *)(f->esp);
  switch (syscall_num) {
    case SYS_HALT:                   /* Halt the operating system. */
    halt();
    break;
    case SYS_EXIT:                   /* Terminate this process. */
    check_address(f->esp+4);
    exit(*(int*)(f->esp+4));
    break;
    case SYS_EXEC:                   /* Start another process. */
    check_address(f->esp+4);
    f->eax=exec((char*)*(uint32_t*)(f->esp+4));
    break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
    check_address(f->esp+4);
    f->eax = wait(*(uint32_t*)(f->esp+4));
    break;
    case SYS_CREATE:                 /* Create a file. */
    check_address(f->esp+4);
    check_address(f->esp+8);
    f->eax = create((char*)*(uint32_t*)(f->esp+4), *(uint32_t*)(f->esp+8));
    break;
    case SYS_REMOVE:                 /* Delete a file. */
    check_address(f->esp+4);
    f->eax = remove((char*)*(uint32_t*)(f->esp+4));
    break;
    case SYS_OPEN:                   /* Open a file. */
    check_address(f->esp+4);
    f->eax = open((char*)*(uint32_t*)(f->esp+4));
    break;
    case SYS_FILESIZE:               /* Obtain a file's size. */
    check_address(f->esp+4);
    f->eax = filesize(*(uint32_t*)(f->esp+4));
    break;
    case SYS_READ:                   /* Read from a file. */
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
    f->eax = write((int)*(uint32_t*)(f->esp+4), (const void*)*(uint32_t*)(f->esp+8),
					(unsigned)*(uint32_t*)(f->esp+12));
    break;
    case SYS_SEEK:                   /* Change position in a file. */
    check_address(f->esp+4);
    check_address(f->esp+8);
    seek((int)*(uint32_t*)(f->esp+4), (unsigned)*(uint32_t*)(f->esp+8));
    break;
    case SYS_TELL:                   /* Report current position in a file. */
    check_address(f->esp+4);
    f->eax = tell((int)*(uint32_t*)(f->esp+4));
    break;
    case SYS_CLOSE:                  /* Close a file. */
    check_address(f->esp+4);
    close(*(uint32_t*)(f->esp+4));
    break;
  }
}

void 
halt(void) {
  shutdown_power_off();
}

void 
exit (int status) 
{
  /* document의 요구사항에 따라, 스레드가 종료될 때에는 종료 메세지를 출력한다. */
  struct thread* t = thread_current();
  t->exit_status = status;
  printf("%s: exit(%d)\n", thread_name(), t->exit_status);
  thread_exit ();
}

pid_t
exec(const char *cmd_line) 
{
  return process_execute(cmd_line);
}

int
wait(pid_t pid)
{
  return process_wait(pid);
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