#include "userprog/syscall.h"
#include <stdio.h>
#include <debug.h>
#include <syscall-nr.h>
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "userprog/exception.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "threads/interrupt.h"
#include <list.h>
#include "threads/palloc.h"

static int get_next_fd(void);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Makes sure that p is a user-virtual address, non-null, and already mapped into physical memory */
bool is_valid_addr(void *p) {
  return p != NULL && is_user_vaddr(p) && pagedir_get_page(thread_current()->pagedir, p) != NULL;
}

/* Makes sure that string p is valid, based on the same criteria above */
static bool is_valid_str(char *p) {
  if( p == NULL ) return false;
  while(*p) {
    if (!is_valid_addr(p))
      return false;
    p++;
  }
  return true;
}

/* Fetches the syscall number from f's stack pointer, as well as other arguments above it for certain sys calls, then executes the correct system call.  Exits if any bad pointers are accessed*/
void
syscall_handler (struct intr_frame *f)
{
  int sys_num;
  int status = -1;
  struct thread* t = thread_current();

  if(!is_valid_addr(f->esp)) {
    sys_num = SYS_EXIT;
  } else {
    sys_num = *(int*)(f->esp);
  }

  switch (sys_num) {

    case SYS_HALT: {
      shutdown_power_off();
      break;
    }
    case SYS_EXEC: {
      if(!is_valid_addr(f->esp+4)) {
        goto exit;
      } else {
        const char *cmd_line = *(char**)(f->esp+4);
        if(!is_valid_str(cmd_line)) {
          goto exit;
        }
        tid_t t = process_execute(cmd_line);
        struct thread *thread = thread_get_by_id(t);
        if (thread == NULL) {
          f->eax = -1;
        } else {
          sema_down(&thread->loaded);
          if(!thread->load_status) {
            f->eax = -1;
          } else {
            f->eax = (uint32_t)t; 
          }
        }
        break;
      }
    }
    case SYS_WAIT: {
      if(!is_valid_addr(f->esp+4)) {
        goto exit;
      } else {
        int pid = *(int*)(f->esp+4);
        int ret = process_wait(pid);
        f->eax = ret;
        break;
      }
    }
    case SYS_CREATE: {
      if(!is_valid_addr(f->esp+4) || !is_valid_addr(f->esp+8)) {
        goto exit;
      } else {
        const char *file = *(char**)(f->esp+4);
        unsigned size = *(unsigned*)(f->esp+8);
        if(!is_valid_str(file)) {
          goto exit;
        }
        bool created = filesys_create(file, size);
        if(created)
          f->eax = 1;
        else
          f->eax = 0;
        break;
      }
    } 
    case SYS_REMOVE: {
      if(!is_valid_addr(f->esp+4)) {
        goto exit;
      } else {
        const char *file = *(char**)(f->esp+4);
        if(!is_valid_str(file)) {
          goto exit;
        }
        bool removed = filesys_remove(file);
        if(removed)
          f->eax = 1;
        else
          f->eax = 0;
        break;
      }
    }
    case SYS_OPEN: {
      if(!is_valid_addr(f->esp+4)) {
        goto exit;
      } else {
        const char *filename = *(char**)(f->esp+4);
        if(!is_valid_str(filename)) {
          goto exit;
        }
        int fd = get_next_fd();
        struct file *file = filesys_open(filename);
        if (file == NULL)
           f->eax = -1;
        else {
           t->fds[fd] = file;
           f->eax = fd;
        }
        break;
      }
    }
    case SYS_FILESIZE: {
      if(!is_valid_addr(f->esp+4)) {
        goto exit;
      } else {
        int fd = *(int*)(f->esp+4);
        if(t->fds[fd] == NULL) {
          goto exit;
        }
        f->eax = (int)file_length(t->fds[fd]);
        break;
      }
    } 
    case SYS_READ: {
      if(!is_valid_addr(f->esp+4) || !is_valid_addr(f->esp+8) || !is_valid_addr(f->esp+12)) {
        goto exit;
      } else {
        int fd = *(int*)(f->esp+4);
        char *buffer = *(char**)(f->esp+8);
        off_t size = *(int*)(f->esp+12);
        if(!is_valid_addr(buffer)) {
          goto exit;
        }
        if(!fd) { // keyboard input
          int x;
          for (x=0; x<size; x++) {
            buffer[x] = input_getc();
          }
          f->eax = size;
        } else {
          if(t->fds[fd] == NULL) {
            goto exit;
          }
          f->eax = (int)file_read(t->fds[fd], (void*)buffer, size);
        }
        break;
      }
    }
    case SYS_WRITE: {
      if(!is_valid_addr(f->esp+4) || !is_valid_addr(f->esp+8) || !is_valid_addr(f->esp+12)) {
        goto exit;
      } else {
        int fd = *(int*)(f->esp+4);
        char *buffer = *(char**)(f->esp+8);
        off_t size = *(int*)(f->esp+12);
        int i;
        for(i = 0; i < size; i++) {
          if(!is_valid_addr(buffer+i)) {
            goto exit;
          }
        }
        if(fd == 1) { // write to the console; must write all of the text from buffer
          putbuf(buffer, size);
          f->eax = size;
        } else {
           if(t->fds[fd] == NULL) {
              goto exit;
            }
           f->eax = (int)file_write(t->fds[fd], buffer, size);
        }
        break;
      }
    }
    case SYS_SEEK: {
      if(!is_valid_addr(f->esp+4) || !is_valid_addr(f->esp+8)) {
        goto exit;
      } else {
        int fd = *(int*)(f->esp+4);
        off_t size = *(int*)(f->esp+8);
        if(t->fds[fd] == NULL) {
          goto exit;
        }
        file_seek(t->fds[fd], size);
        break;
      }
    }
    case SYS_TELL: {
      if(!is_valid_addr(f->esp+4)) {
        goto exit;
      } else {
        int fd = *(int*)(f->esp+4);
        if(t->fds[fd] == NULL) {
          goto exit;
        }
        f->eax = (int)file_tell(t->fds[fd]);
        break;
      }
    }
    case SYS_CLOSE: {
      if(!is_valid_addr(f->esp+4)) {
        goto exit;
      } else {
        int fd = *(int*)(f->esp+4);
        if(t->fds[fd] == NULL) {
          goto exit;
        }
        file_close(t->fds[fd]);
        t->fds[fd] = NULL;
        break;
      }
    }
    case SYS_EXIT: {
      if(is_valid_addr(f->esp+4)) {
        status = *(int*)(f->esp + 4);
      }
    }
    default:
      exit:
      file_allow_write(t->exec);
      file_close(t->exec);
      statuses[t->tid] = status;
      printf("%s: exit(%d)\n",t->name, status);
      thread_exit();
  }
}

static int get_next_fd() {
  int j;
  for(j = 2; j < 16; j++) {
    if(thread_current()->fds[j] == NULL)
      return j;
  }
  return -1; // free index not found
}

