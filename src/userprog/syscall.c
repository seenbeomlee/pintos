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
    break;
    case SYS_EXIT:                   /* Terminate this process. */
    exit(*(int*)(f->esp+4));
    break;
    case SYS_EXEC:                   /* Start another process. */
    break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
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
    break;
    case SYS_WRITE:                  /* Write to a file. */
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
exit (int status) 
{
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_exit ();
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