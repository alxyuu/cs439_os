#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/interrupt.h"

int statuses[128];
bool is_valid_addr(void *);
void syscall_init (void);
void syscall_handler (struct intr_frame *); // declaration of function that will be implemented

#endif /* userprog/syscall.h */
