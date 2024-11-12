#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "devices/timer.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  int size = strlen(file_name);
  char* parsed_fn[size + 1]; // 왜냐하면, size는 문자열의 길이이므로 '\0'을 삽입하기 위해서는 +1을 해주어야 한다.

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

	/** 2
	 * 	Project2: for Test Case - 직접 프로그램을 실행할 때에는 이 함수를 사용하지 않지만 make check에서
	 *  이 함수를 통해 process_create를 실행하기 때문에 이 부분을 수정해주지 않으면 Test Case의 Thread_name이
	 *  커맨드 라인 전체로 바뀌게 되어 Pass할 수 없다.
	 */
  parse_filename(file_name, parsed_fn);
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (parsed_fn, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
/** 2
 * 유저가 입력한 명령어를 수행할 수 있도록, 프로그램을 메모리에 적재하고 실행하는 함수이다.
 * filename을 f_name이라는 인자로 받아서 file_name에 저장한다.
 * 초기에 file_name은 실행 프로그램 파일명과 옵션이 분리되지 않은 상황(통 문자열)이다.
 * thread의 이름을 실행 파일명으로 저장하기 위해 실행 프로그램 파일명만 분리하기 위해 parsing해야 한다.
 * 실행파일명은 cmd line 안에서 첫번째 공백 전의 단어에 해당한다.
 * 다른 인자들 역시 프로세스를 실행하는데 필요하므로, 함께 user stack에 담아줘야한다.
 * arg_list라는 배열을 만들어서, 각 인자의 char*을 담아준다.
 * 실행 프로그램 파일명은 arg_list[0]에 들어간다.
 * 2번째 인자 이후로는 arg_list[i]에 들어간다.
 * load ()가 성공적으로 이루어졌을 때, argument_stack 함수를 이용하여, user stack에 인자들을 저장한다.
*/
static void
start_process (void *file_name_)
{
// 유저가 입력한 명령어를 수행하도록 프로그램을 메모리에 적재하고 실행하는 함수. 
// 여기에 파일 네임 인자로 받아서 저장(문자열) => 근데 실행 프로그램 파일과 옵션이 분리되지 않은 상황.
  char *file_name = file_name_; // f_name은 문자열인데 위에서 (void *)로 넘겨받음! -> 문자열로 인식하기 위해서 char* 로 변환해줘야한다.
  struct intr_frame if_;
  bool success;

  int size = strlen(file_name);
  char* parsed_fn[size + 1]; // 왜냐하면, size는 문자열의 길이이므로 '\0'을 삽입하기 위해서는 +1을 해주어야 한다.

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  /** 2
	 * if_에는 intr_frame 내 구조체 멤버에 필요한 정보를 담는다. 
	 * 여기서 intr_frame은 인터럽트 스택 프레임이다. 
	 * 즉, 인터럽트 프레임은 인터럽트와 같은 요청이 들어와서 기존까지 실행 중이던 context(레지스터 값 포함)를 스택에 저장하기 위한 구조체이다!
	 */
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  parse_filename(file_name, parsed_fn);
  success = load (parsed_fn, &if_.eip, &if_.esp);
// file_name, _if를 현재 프로세스에 load 한다.
// success는 bool type이니까 load에 성공하면 1, 실패하면 0 반환.
// 이때 file_name: f_name의 첫 문자열을 parsing하여 넘겨줘야 한다!

  /* 파일 로드에 성공하면, setting_esp 을 진행한다. */
  if (success) {
    // printf("successed!\n");
    setting_esp(file_name, &if_.esp);
  }

  /* If load failed, quit. */
/** 2
 * 어라, 근데 page를 할당해준 적이 없는데 왜 free를 하는 거지? 
 * => palloc()은 load() 함수 내에서 file_name을 메모리에 올리는 과정에서 page allocation을 해준다. 
 * 이때, 페이지를 할당해주는 걸 임시로 해주는 것.
 * file_name: 프로그램 파일 받기 위해 만든 임시변수. 따라서 load 끝나면 메모리 반환.
*/
  palloc_free_page (file_name);
  if (!success) {
    // printf("not successed!\n");
    thread_exit ();
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  struct list_elem* elem;
  int child_exit_status = -1;

  struct thread* child_thread = find_child_thread(child_tid);

  if(child_thread == NULL) { // 자식이 아니라면 -1을 반환한다.
    return child_exit_status;
  }
  else {
    sema_down(&(child_thread->exit_sema)); // 자식 프로세스가 종료될 때 까지 대기한다. (process_exit에서 자식이 종료될 때 sema_up 해줄 것이다.)
    child_exit_status = child_thread->exit_status;
    /** 
     * child_thread의 exit_status를 받기 위해서, child thread의 memory를 삭제하는 단계를 child thread_exit() 시가 아니라,
     * 부모의 process_wait()가 재개된 시점으로 한다.. 맞나?
     */
    list_remove(&(child_thread->child_thread_list_elem)); // 자식이 종료됨을 알리는 'wait_sema' signal을 받으면 현재 스레드(부모)의 자식 리스트에서 제거한다.
    return child_exit_status; // 자식의 exit_status를 반환한다.
  }
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

/**
 * 0 ; STDIN
 * 1 ; STDOUT
 * 2 ; STDERR
 */
  for(int i = 3; i < FDTABLE_SIZE; i++) {
    process_file_close(i); // syscall close에서 fd를 받아 단일 파일을 close하는 동작이 필요하므로, 불가피하게 캡슐화
  }

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  // 각 프로세스가 실행이 될 때, 각 프로세스에 해당하는 VM(virtual memory)이 만들어져야 하므로,
	// 이를 위해 페이지 테이블 엔트리를 생성하는 과정이 우선된다.
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
// 그 뒤, 파일을 실제로 VM에 올리는 과정이 진행된다. 
// 파일이 제대로 된 ELF 인지 검사하는 과정이 동반되며, 
// 세그먼트 단위로 PT_LOAD의 헤더 타입을 가진 부분을 하나씩 메모리로 올리는 작업을 진행한다.
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
/* esp (stack pointer)를 세팅하는 함수이다. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

void 
parse_filename(char *src, char *dest)
{
    int i = 0;
    while (src[i] != '\0' && src[i] != ' ') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void 
setting_esp(char* file_name, void** esp)
{
  char** argv;

  int i = 0;
  int argc = 0;

  argc = parse_argc(file_name);
  argv = (char** )malloc(sizeof(char* ) * argc);
  parse_argv(argv, argc, file_name);

 /*
• argv[2][...]
• argv[1][...]
• argv[0][...]
• <word-align> -- 데이터의 접근 속도를 빠르게 하기 위해서 4의 배수로 맞춘다.
• NULL * 포인터
• argv[2] * 포인터
• argv[1] * 포인터
• argv[0] * 포인터
• argv **
• argc int
• return address
 */

  init_esp(argv, argc, esp);
  // 이것을 안하면 메모리 누수가 발생할텐데, 하면 테스트가 fail됨.. parse_argv에서 동적할당을 안하고 할 수는 없나?
  // free_argv(argv, argc);
  // for (int i = 0; i < argc; i++) {
  //   free(argv[i]); // 각 토큰에 대한 메모리 해제
  // }
  free(argv);
}

int
parse_argc(char* file_name) {
  char* token;
  char* next_ptr;
  int argc = 0;

  char* dest_file_name[strlen(file_name) + 1];
  strlcpy(dest_file_name, file_name, strlen(file_name) + 1);

  token = strtok_r(dest_file_name, " ", &next_ptr);

  while (token != NULL)
  {
    argc++;
    token = strtok_r(NULL, " ", &next_ptr);
  }

  return argc;
}

void
parse_argv(char** argv, int argc, char* file_name) {
  char* token;
  char* next_ptr;

  int i = 0;

  char* dest_file_name = malloc(strlen(file_name) + 1);
  strlcpy(dest_file_name, file_name, strlen(file_name) + 1);

  for(token = strtok_r(dest_file_name, " ", &next_ptr); i < argc; i++, token = strtok_r(NULL, " ", &next_ptr)) {
    argv[i] = malloc(strlen(token) + 1);
    strlcpy(argv[i], token, strlen(token) + 1);
  }

  free(dest_file_name);
}

void init_esp(char** argv, char* argc, void** esp) {
  int i = 0;
  int argv_len = 0;
  int sum_argv_len = 0;
  /* push argv[argc-1] ~ argv[0] */

  // 인자를 역순으로 스택에 복사
  for (i = argc; i > 0; i--) {
    argv_len = strlen(argv[i-1]); // argc = 3이면 argv[2]부터 넣는다.
    *esp = *esp - (argv_len + 1);
    sum_argv_len = sum_argv_len + (argv_len + 1);
    strlcpy(*esp, argv[i-1], argv_len + 1);
    argv[i-1] = *esp;
  }

  /* push word align */
  // 스택 포인터가 4바이트 배수가 되도록 감소시키고, 정렬을 위해 추가된 바이트에 0을 채움
  if (sum_argv_len % 4 != 0) *esp -= 4 - (sum_argv_len % 4);

  /* push NULL */
  // 인자 주소 리스트의 끝을 표시
  *esp -= 4;
  **(uint32_t **)esp = 0;

  /* push address of argv[argc-1] ~ argv[0] */
  // 인자 주소 넣기
  for (i = argc - 1; i >= 0; i--) {
    *esp -= 4;
    **(uint32_t **)esp = argv[i];
  }

  /* push address of argv */
  // 인자 주소 리스트의 주소를 표시
  *esp -= 4;
  **(uint32_t **)esp = *esp + 4;

  /* push argc */
  *esp -= 4;
  **(uint32_t **)esp = argc;
  
  /* push return address */
  // 리턴 주소를 0으로 설정하여 스택의 최상단에 삽입. 이는 main 함수 종료 후 돌아갈 주소가 없음을 나타냄.
  *esp -= 4;
  **(uint32_t **)esp = 0;
}

// void free_argv(char** argv, int argc) {
//   for (int i = 0; i < argc; i++) {
//     free(argv[i]); // 각 토큰에 대한 메모리 해제
//   }
//   free(argv);
// }

void process_file_close(int fd_idx) {
  struct thread* t = thread_current();

  // process_exit에서는 애초에 for문에서 걸러져서 들어오지만, close syscall에서는 fd를 받아 단일 파일에 대해 수행하므로, 검토 조건 필요하다.
  if(fd_idx < 3 || fd_idx >= FDTABLE_SIZE) {
    return;
  }

  if(t->fd_table[fd_idx] != NULL) {
    file_close(t->fd_table[fd_idx]);
  }

  return;
}