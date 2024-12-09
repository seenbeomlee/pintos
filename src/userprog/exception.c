#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

// 스택 확장 검증
static bool validate_stack_growth(void* access_addr, void* current_esp);

// Supplementary Page Table Entry 초기화
static void initialize_spt_entry(struct spt_entry* entry, void* aligned_addr, struct frame_entry* frame);

/** 
 * 페이지 폴트 발생 시, 스택 확장이 필요한 경우 호출.
 * 새로운 스택 페이지를 생성하고, 이를 SPT와 연결.
 */
static bool grow_user_stack(void* target_addr);

/**
 * 페이지 폴트의 일반적인 처리 루틴.
 * SPT 엔트리를 기반으로 페이지를 적재하거나 복구.
 * 내부적으로 process_page_type을 호출하여 페이지 유형별 작업을 수행.
 */
static bool resolve_page_fault(struct spt_entry* page_entry);

/**
 * 페이지의 유형에 따라 데이터를 로드하거나 복구.
 * resolve_page_fault에서 호출되어, 실제 페이지 데이터를 메모리에 적재.
 */
static bool process_page_type(struct spt_entry* entry, struct frame_entry* frame);


/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

    // 페이지 폴트 원인 확인
    not_present = (f->error_code & PF_P) == 0; // 페이지가 메모리에 없어서 발생
    write = (f->error_code & PF_W) != 0; // 쓰기 접근 시 발생
    user = (f->error_code & PF_U) != 0; // 사용자 영역에서 발생

    // 가상 주소의 유효성 검증
    if (!is_user_vaddr(fault_addr)) {
        exit(-1);
    }
    if (!not_present) {
        exit(-1);
    }

    // spt에서 엔트리 검색
    struct spt_entry* page_entry = lookup_spt_entry(fault_addr);
    if (page_entry == NULL) { // spt 엔트리가 없는 경우의 페이지 폴트 처리
        if (!validate_stack_growth(fault_addr, f->esp)) { // 스택 확장이 가능한지 '논리적' 확인
            exit(-1); // 스택 확장이 불가능하면 종료
        }
        if (!grow_user_stack(fault_addr)) { // 실제로 스택 확장을 진행한다.
            exit(-1); // 스택 확장이 논리적으로 가능하나, 실제로는 불가능한 경우 종료
        }
        return;
    }

    if (!resolve_page_fault(page_entry)) { // spt 엔트리가 있는 경우의 페이지 폴트 처리
        printf("Page fault resolution failed.\n");
        exit(-1);
    }
}

static bool validate_stack_growth(void* access_addr, void* current_esp) {
    // 접근 주소가 사용자 주소인지 확인
    if (!is_user_vaddr(access_addr)) {
        return false;
    }

    // 스택 크기 제한 확인
    if (access_addr < MAX_STACK_LIMIT) {
        return false;
    }

    // esp와 fault_addr 간 거리 확인
    uintptr_t distance = (uintptr_t)current_esp - (uintptr_t)access_addr;
    if (distance > 32) {
        return false;
    }

    // 모든 조건 만족 시, 스택 확장이 가능하므로 true를 반환
    return true;
}

static void initialize_spt_entry(struct spt_entry* entry, void* aligned_addr, struct frame_entry* frame) {
    ASSERT(entry != NULL);
    ASSERT(frame != NULL);

    entry->type = VM_ANON;
    entry->writable = true;
    entry->is_loaded = true;
    entry->vaddr = aligned_addr;
    frame->spt_entry = entry;
}

// 사용자 스택 확장 처리
static bool grow_user_stack(void* target_addr) {
  void* aligned_addr = pg_round_down(target_addr); // 대상 주소를 페이지 단위로 정렬
  struct frame_entry* new_frame = allocate_frame(PAL_USER | PAL_ZERO); // 사용자 페이지를 위한 물리 메모리 할당
  struct spt_entry* new_entry = malloc(sizeof(struct spt_entry)); // SPT 엔트리 생성

  if (!new_frame || !new_entry) { // 메모리 할당 실패 처리
    if (new_frame) free_page(new_frame->frame_addr);
    if (new_entry) free(new_entry);
    return false;
  }

  // 물리 메모리와 가상 주소 매핑
  if (!install_page(aligned_addr, new_frame->frame_addr, true)) { // 페이지 매핑 실패 처리
    free_page(new_frame->frame_addr);
    free(new_entry);
    return false;
  }

  /**
   * 스택 확장(stack growth)은 가상 메모리의 특정 주소를 기준으로 새로운 페이지를 추가하는 작업이다.
   * 1. 새로 할당된 페이지의 정보를 spt_entry로 초기화한 후, 
   * 2. insert_spt_entry를 호출하여 spt에 등록한다.
   * 이를 통해 스택 확장된 메모리도 프로세스의 메모리 관리 영역(spt)에 포함되어 관리된다.
   */
  initialize_spt_entry(new_entry, aligned_addr, new_frame); // 1. SPT 엔트리 초기화
  insert_spt_entry(&(thread_current()->spt), new_entry); // 2. SPT에 엔트리 추가

  add_frame_to_lru(new_frame); // 프레임을 LRU 리스트에 추가

  return true; // 스택 확장 성공
}

// 페이지 폴트 처리 루틴 : spt에 엔트리가 있다면, 파일 데이터 로드 & 스왑에서 복구 등 페이지 적재를 처리
static bool resolve_page_fault(struct spt_entry* page_entry) {
  struct frame_entry* allocated_frame = allocate_frame(PAL_USER); // 사용자 페이지를 위한 프레임 할당
  if (!allocated_frame) { // 물리 메모리 페이지 할당 실패 시
    return false;
  }

  if (!process_page_type(page_entry, allocated_frame)) { // 페이지 유형에 따른 데이터 로드/ 복구
    return false;
  }

  if (!install_page(page_entry->vaddr, allocated_frame->frame_addr, page_entry->writable)) { // 가상 주소와 물리 페이지 매핑
    free_page(allocated_frame->frame_addr);
    return false;
  }

  // spt 엔트리와 프레임 상태 업데이터
  page_entry->is_loaded = true; // 페이지 로드 상태 업데이트
  allocated_frame->spt_entry = page_entry; // 프레임과 SPT 엔트리 연결

  add_frame_to_lru(allocated_frame); // 프레임을 LRU 리스트에 추가

  return true; // 페이지 폴트 처리 성공
}

// 페이지 유형에 따라 데이터 처리
static bool process_page_type(struct spt_entry* entry, struct frame_entry* frame) {
  ASSERT(entry != NULL);
  ASSERT(frame != NULL);

  switch (entry->type) { // 페이지 유형에 따라 처리
    case VM_BIN: // 실행 파일의 페이지 데이터 로드
    case VM_FILE: // 메모리 매핑 파일의 데이터 로드
      if (!load_file(frame->frame_addr, entry)) { // 파일에서 페이지 데이터 로드
        free_page(frame->frame_addr);
        return false;
      }
      entry->is_loaded = true; // 로드 상태 업데이트
      break;

    case VM_ANON: // 익명 메모리 영역의 데이터 복구
      swap_in(entry->swap_slot, frame->frame_addr); // 스왑에서 복구
      entry->is_loaded = true; // 로드 상태 업데이트
      break;

    default: // 잘못된 페이지 유형 처리
      free_page(frame->frame_addr);
      return false;
  }

  return true; // 페이지 처리 성공
}
