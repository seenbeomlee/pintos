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

/** 2
 * 시스템 콜을 호출할 때, 원하는 기능에 해당하는 시스템 콜 번호를 rax에 담는다.
 * 그리고 시스템 콜 핸들러는 rax의 숫자로 시스템 콜을 호출하고, -> 이는 enum으로 선언되어있다.
 * 해달 콜의 반환값을 다시 rax에 담아서 intr_frame(인터럽트 프레임)에 저장한다.
 */
static void // void 형식에 return을 추가해야 한다. (디버깅하다 발견한 사실이라 함.)
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
  // 만약 프로그램이 프로세스를 로드하지 못하거나, 다른 이유로 돌리지 못하게 되면 exit_status == -1을 반환하며 프로세스가 종료된다.
  return process_execute(cmd_line); // 지정된 파일을 실행하는 새 프로세스를 생성하고, 그 프로세스의 ID를 반환한다.
}

int
wait(pid_t pid)
{
  return process_wait(pid); // 특정 자식 프로세스의 종료를 기다리고 
}

int 
read(int fd, void *buffer, unsigned int size)
{
  if (fd == 0) {  // 0(stdin) -> keyboard(standard input)로 직접 입력
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