#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "threads/synch.h"

struct supp_page_table
{
  struct hash table;
  struct semaphore lock;
};

struct page_entry
{
  struct hash_elem elem;
  void *upage;
  uint32_t *pagedir;
  struct semaphore *lock_p;

  // if loaded into frame
  void *kpage;

  // if swapped out
  int swap_num;

  //metadata for reloading a frame
  bool writable;

  //if load from disk
  bool stack;
  bool mmap;
  bool force_swap;
  bool zero_page;
  struct file *file;
  size_t start_offset;
  size_t read_bytes;
};

void page_init (struct thread *t);
bool page_alloc_page (void *uaddr, bool writable, bool stack, bool mmap, bool zero_page, struct file *file, size_t start_offset, size_t read_bytes);
void page_free_page (void *uaddr);
bool page_fix_page (void *uaddr, bool stack, bool write, bool pin);
void page_free_all(void);
bool page_unmap (void* upage);
bool page_pin_frame (void* upage, bool write);
void page_unpin_frame (void* upage);

#endif /* vm/page.h */
