#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/thread.h"
#include "vm/page.h"

struct frame_node
{
  // just for debugging
  unsigned frame_id;

  struct list_elem elem;
  void *kpage;

  //which page_entry has this frame
  struct page_entry *upage_entry;

  //used for page replacement
  bool used;
  bool has_chance;
  struct semaphore pin;
};

void frame_init (void);
void *frame_get_page (struct page_entry *, bool pin);
void frame_free_page (void *);
void frame_pin_frame (void *);
void frame_unpin_frame (void *);

#endif /* vm/frame.h */
