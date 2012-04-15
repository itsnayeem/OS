#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include <hash.h>

#ifndef MAPID_T_DEFINED
#define MAPID_T_DEFINED
typedef int mapid_t;
#endif

struct semaphore file_lock;

struct fd_node
{
  struct list_elem elem;
  int fd;
  struct file* f_ptr;
};

struct mmap_table
{
  struct hash table;
  mapid_t last_id;
};

void syscall_init (void);
void sys_exit(int status);
void mmap_init(struct mmap_table *mt);
void mmap_free_all (void);

#endif /* userprog/syscall.h */
