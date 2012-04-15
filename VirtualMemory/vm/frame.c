#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/file.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "userprog/debugf.h"

static struct list frame_table;
struct semaphore frame_lock;
static struct frame_node *next_page;
static int frame_cnt;

void
frame_init (void)
{
  list_init (&frame_table);
  sema_init (&frame_lock, 1);
  frame_cnt = 0;

  void *curr;
  struct frame_node *f_node;

  while ((curr = palloc_get_page (PAL_USER | PAL_ZERO)) != NULL)
    {
      f_node = malloc (sizeof (struct frame_node));
      f_node->frame_id = frame_cnt;
      f_node->kpage = curr;
      f_node->upage_entry = NULL;
      f_node->used = false;
      f_node->has_chance = false;
      sema_init(&f_node->pin, 1);
      list_push_back (&frame_table, &f_node->elem);
      frame_cnt++;
    }
    struct list_elem *e;
    e = list_front (&frame_table);
    next_page = list_entry (e, struct frame_node, elem);
}

void *
frame_get_page (struct page_entry *upage_entry, bool pin)
{
  struct frame_node *f_node;
  struct list_elem *e;
  struct thread *t = thread_current ();

  sema_down(&frame_lock);

  while (true)
    {
      f_node = next_page;
      e = list_next (&f_node->elem);
      if (e == list_end (&frame_table))
        {
          e = list_front (&frame_table);
        }
      next_page = list_entry (e, struct frame_node, elem);

      if (!sema_try_down(&f_node->pin))
        {
          continue;
        }

      if (!f_node->used)
        {
          DEBUGC("[%s] frame_get_page:: frame number: %d is unused\n", t->name, f_node->frame_id);
          break;
        }

      if (f_node->has_chance)
        {
          DEBUGC("[%s] frame_get_page:: frame number: %d is used but has a chance\n", t->name, f_node->frame_id);
          bool accessed = pagedir_is_accessed (f_node->upage_entry->pagedir, f_node->upage_entry->upage);
          if (accessed)
            {
              DEBUGC("[%s] frame_get_page:: frame number: %d was recently accessed, moving to back\n", t->name, f_node->frame_id);
              list_remove (&f_node->elem);
              f_node->has_chance = false;
              list_push_back (&frame_table, &f_node->elem);
              sema_up (&f_node->pin);
            }
          else
            {
              DEBUGC("[%s] frame_get_page:: frame number: %d was NOT recently accessed, using\n", t->name, f_node->frame_id);
              break;
            }
        }
      else
        {
          DEBUGC("[%s] frame_get_page:: frame number: %d has no second chance, using\n", t->name, f_node->frame_id);
          break;
        }
    }


  DEBUGB("[%s] frame_get_page:: Using frame number: %u kpage: %p\n", t->name, f_node->frame_id, f_node->kpage);


  if (f_node->used)
    {
      sema_down(f_node->upage_entry->lock_p);
      bool dirty = pagedir_is_dirty (f_node->upage_entry->pagedir, f_node->upage_entry->upage);

      if (!f_node->upage_entry->mmap && !f_node->upage_entry->stack)
        {
          if (!f_node->upage_entry->force_swap && dirty)
            {
              DEBUGB("***** [%s] frame_get_page:: changing %p to swap only\n", t->name, f_node->upage_entry->upage);
              f_node->upage_entry->force_swap = true;
            }
        }

      DEBUGB("[%s] frame_get_page:: old page: %p dirty: %d stack: %d writable: %d force_swap: %d\n", t->name, f_node->upage_entry->upage, dirty,
          f_node->upage_entry->stack, f_node->upage_entry->writable, f_node->upage_entry->force_swap);

      if (f_node->upage_entry->mmap && dirty)
        {
          DEBUGB("[%s] frame_get_page:: dumping page back to file: upage: %p\n", t->name, f_node->upage_entry->upage);
          file_write_at(f_node->upage_entry->file, f_node->upage_entry->upage, f_node->upage_entry->read_bytes, f_node->upage_entry->start_offset);
          f_node->upage_entry->kpage = NULL;
          pagedir_clear_page (f_node->upage_entry->pagedir, f_node->upage_entry->upage);
        }

      else //if (dirty || f_node->upage_entry->force_swap)
        {
          f_node->upage_entry->swap_num = swap_dump_page (f_node->kpage);
          f_node->upage_entry->kpage = NULL;
          pagedir_clear_page (f_node->upage_entry->pagedir, f_node->upage_entry->upage);
        }
      sema_up(f_node->upage_entry->lock_p);
    }

  f_node->upage_entry = upage_entry;

  f_node->used = true;
  f_node->has_chance = true;
  if (!pin && f_node->pin.value == 0)
    sema_up(&f_node->pin);

  DEBUGB("[%s] frame_get_page:: checking pin after setup: %d\n", t->name, f_node->pin.value);
  sema_up(&frame_lock);
  return f_node->kpage;
}

void
frame_free_page (void *kpage)
{
  struct frame_node *f_node = NULL;
  struct list_elem *e;
  struct thread *t = thread_current ();

  sema_down(&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e)) {
    f_node = list_entry (e, struct frame_node, elem);
    if (f_node->kpage == kpage)
      {
        break;
      }
  }

  if (e == list_end (&frame_table))
    {
      sema_up(&frame_lock);
      return;
    }

  sema_try_down(&f_node->pin);

  if (f_node->used)
    {
      sema_down(f_node->upage_entry->lock_p);
      DEBUGB("[%s] frame_free_page:: old page: %p dirty: %d\n", t->name, f_node->upage_entry->upage, pagedir_is_dirty (f_node->upage_entry->pagedir, f_node->upage_entry->upage));
      bool dirty = pagedir_is_dirty (f_node->upage_entry->pagedir, f_node->upage_entry->upage);
      if (f_node->upage_entry->mmap && f_node->upage_entry->writable && dirty)
        {
          DEBUGB("[%s] frame_free_page:: dumping page back to file: upage: %p\n", t->name, f_node->upage_entry->upage);
          file_write_at(f_node->upage_entry->file, f_node->upage_entry->upage, f_node->upage_entry->read_bytes, f_node->upage_entry->start_offset);
          f_node->upage_entry->kpage = NULL;
          pagedir_clear_page (f_node->upage_entry->pagedir, f_node->upage_entry->upage);
        }
      sema_up(f_node->upage_entry->lock_p);
    }


  f_node->upage_entry = NULL;

  f_node->used = false;

  DEBUGB("[%s] frame_free_page:: frame found, sema: %d\n", t->name, f_node->pin.value);
  if (f_node->pin.value == 0)
    sema_up(&f_node->pin);
  sema_up(&frame_lock);
}


void frame_pin_frame (void *kpage)
{
  struct frame_node *f_node = NULL;
  struct list_elem *e;
  struct thread *t = thread_current ();
  DEBUGB("[%s] frame_pin_frame:: pinning frame %p\n", t->name, kpage);

  sema_down(&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e)) {
    f_node = list_entry (e, struct frame_node, elem);
    if (f_node->kpage == kpage)
      {
        break;
      }
  }

  if (e == list_end (&frame_table))
    {
      sema_up(&frame_lock);
      return;
    }


  if (f_node->pin.value == 1)
    sema_down(&f_node->pin);
  DEBUGB("[%s] frame_pin_frame:: frame found, after change pin: %d\n", t->name, f_node->pin.value);
  sema_up(&frame_lock);
}

void frame_unpin_frame (void *kpage)
{
  struct frame_node *f_node = NULL;
  struct list_elem *e;
  struct thread *t = thread_current ();
  DEBUGB("[%s] frame_unpin_frame:: unpinning frame %p\n", t->name, kpage);

  sema_down(&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e)) {
    f_node = list_entry (e, struct frame_node, elem);
    if (f_node->kpage == kpage)
      {
        break;
      }
  }

  if (e == list_end (&frame_table))
    {
      sema_up(&frame_lock);
      return;
    }


  if (f_node->pin.value == 0)
    sema_up(&f_node->pin);
  DEBUGB("[%s] frame_unpin_frame:: frame found, after chance pin: %d\n", t->name, f_node->pin.value);
  sema_up(&frame_lock);
}
