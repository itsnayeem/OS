#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>

struct fd_node {
  struct list_elem elem;
  int fd;
  struct file* f_ptr;
};

void syscall_init (void);
void sys_exit(int status);

#endif /* userprog/syscall.h */
