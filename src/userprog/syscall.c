#include "userprog/syscall.h"
#include <stdio.h>
#include <debug.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <hash.h>

struct hash statuses;
struct status{
  struct hash_elem hash_elem;
  tid_t tid;
  int status;
}

bool status_less_than(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
  return hash_entry(a, struct status, hash_elem)->tid < hash_entry(b, struct status, hash_elem)->tid;

static void syscall_handler (struct intr_frame *); // declaration of function that will be implemented
 //less_than();
 
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{ 
  debug_backtrace_all();
  printf ("system call!\n");
  printf("current thread %d\n", thread_current()->tid); 
/* may want to use a switch table to decide which system call to execute
   may also want to fetch the arguments from f's stack and put them in a list 

  switch (*(f->esp)) {
    case SYS_HALT:
      halt(); // does this push to the current process's stack?
      shutdown_power_off();
      break;
    case SYS_EXIT:
      int status = *(int*)(f->esp + 4); 
      exit(status);
      <return status to kernel>;
      kill(f);
      break;
    case SYS_EXEC:
    case SYS_WAIT:  
    case SYS_CREATE:
    case SYS_REMOVE:
    case SYS_OPEN:
    case SYS_FILESIZE:
    case SYS_READ:
    case SYS_WRITE:
    case SYS_SEEK:
    case SYS_TELL:
    case SYS_CLOSE:

    default: 
*/ 	  	 

  thread_exit ();
}

