#include "userprog/syscall.h"
#include <stdio.h>
#include <debug.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "userprog/exception.h"

static int statuses[128];
static struct file *fds[128];
struct file *file;
int x;

static int get_next_fd();
static void syscall_handler (struct intr_frame *); // declaration of function that will be implemented

void
syscall_init (void) 
{
  //hash_init(&statuses, status_hash, status_less_than, NULL); 
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  int i;
  for(i = 2; i < 128; i++) {
    fds[i] = NULL;
  }
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{ 
  debug_backtrace_all();
  printf ("system call!\n");
  printf("current thread %d\n", thread_current()->tid); 
/* may want to use a switch table to decide which system call to execute
   may also want to fetch the arguments from f's stack and put them in a list  
*/
  switch (*(int*)(f->esp)) {

    case SYS_HALT: {
      shutdown_power_off();
      break;
    }
    case SYS_EXIT: { 
      int status = *(int*)(f->esp + 4); 
      statuses[thread_current()->tid] = status;
  //    kill(f);
      break;
    } 
    case SYS_EXEC: {
      const char *cmd_line = *(char**)(f->esp + 4);
  //    if (!(is_user_vaddr(cmd_line)))
   //     page_fault(); 
      tid_t t = process_execute(cmd_line);
      struct thread *thread = thread_get_by_id(t);
      if (thread == NULL)
        f->eax = -1;
      else { 
        sema_down(thread->loaded);
        if(!thread->load_status)
          f->eax = -1;
        else
          f->eax = (uint32_t)t; 
      }
      break; 
    }
    case SYS_WAIT: {
      break;
    }
    case SYS_CREATE: {
      const char *file = *(char**)(f->esp + 4);
      unsigned size = *(unsigned*)(f->esp + 8);
      bool created = filesys_create(file, size);
      if(created)
        f->eax = 1;
      else
        f->eax = 0;
      break;
    } 
    case SYS_REMOVE: {
      const char *file = *(char**)(f->esp + 4);
      bool removed = filesys_remove(file);
      if(removed)
        f->eax = 1;
      else
        f->eax = 0;
      break;
    }
    case SYS_OPEN: {
      const char *filename = *(char**)(f->esp + 4);
      int fd = get_next_fd();
      file = filesys_open(filename);
      if (file == NULL)
         f->eax = -1;
      else {
         fds[fd] = file;
         f->eax = fd;
      }
      break;
    }
    case SYS_FILESIZE: { 
      int fd = *(int *)(f->esp + 4);
      f->eax = (int)file_length(fds[fd]); 
      break; 
    } 
    case SYS_READ: {
      int fd = *(int *)(f->esp + 4);
      char *buffer = *(char**)(f->esp + 8); 
      off_t size = *(int*)(f->esp + 12); 
      if(!fd){ // keyboard input
        for (x=0; x<size; x++) {
          buffer[x] = input_getc();
        }
        f->eax = size;
      }
      else
        f->eax = (int)file_read(fds[fd], (void*)buffer, size);
      break;
    } 
    case SYS_WRITE: {
      int fd = *(int *)(f->esp + 4);
      char *buffer = *(char**)(f->esp + 8);
      off_t size = *(int*)(f->esp + 12);
      if(fd == 1) {
        for (x = 0 ; x < size; x++) 
          input_putc(buffer[x]);
        f->eax = size;
      }
      else 
        f->eax = (int)file_write(fds[fd], buffer, size);
      break;
    }
    case SYS_SEEK: {
      int fd = *(int*)(f->esp + 4);
      off_t size = *(int*)(f->esp + 8);
      file_seek(fds[fd], size); 
      break;
    }
    case SYS_TELL: {
      int fd = *(int*)(f->esp + 4);
      f->eax = (int)file_tell(fds[fd]); 
      break;
    }
    case SYS_CLOSE: {
      int fd = *(int*)(f->esp + 4);
      file_close(fds[fd]);
      fds[fd] = NULL; 
      break;
    }
    default: {     	  	 
//  thread_exit ();
    }
  }
}

static int get_next_fd() {
  int j;
  for(j = 2; j < 128; j++) {
    if(fds[j] == NULL)
      return j;
  return NULL;
  }
}

