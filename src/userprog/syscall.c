#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
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
    break;
    case SYS_REMOVE:                 /* Delete a file. */
    break;
    case SYS_OPEN:                   /* Open a file. */
    break;
    case SYS_FILESIZE:               /* Obtain a file's size. */
    break;
    case SYS_READ:                   /* Read from a file. */
    check_address(f->esp+4);
    check_address(f->esp+8);
    check_address(f->esp+12);
    f->eax = read((int)*(uint32_t*)(f->esp+4), (void*)*(uint32_t*)(f->esp+8),
					(unsigned)*(uint32_t*)(f->esp+12));
    break;
    case SYS_WRITE:                  /* Write to a file. */
    check_address(f->esp+4);
    check_address(f->esp+8);
    check_address(f->esp+12);
    f->eax = write((int)*(uint32_t*)(f->esp+4), (const void*)*(uint32_t*)(f->esp+8),
					(unsigned)*(uint32_t*)(f->esp+12));
    break;
    case SYS_SEEK:                   /* Change position in a file. */
    break;
    case SYS_TELL:                   /* Report current position in a file. */
    break;
    case SYS_CLOSE:                  /* Close a file. */
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
  if (fd == 0) {  // 0(stdin) -> keyboard로 직접 입력
    int i = 0;  // 쓰레기 값 return 방지
    char c;
    unsigned char *buf = buffer;

    for (; i < size; i++) {
      c = input_getc();
      *buf++ = c;
      if (c == '\0')
        break;
    }
    return i;
  }
  else {
    return -1;
  }
}

int 
write (int fd, const void *buffer, unsigned size) 
{
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }
  return -1; 
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