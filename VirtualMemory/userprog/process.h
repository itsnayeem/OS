#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <list.h>

struct child_info
{
  struct list_elem elem;
  struct wait_info* info;
};

struct wait_info
{
  struct semaphore sem;
  int exit_status;
  tid_t thread_id;
  uint8_t num_ptrs;
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
