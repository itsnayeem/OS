#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hash.h>
#include "filesys/file.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/debugf.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

static unsigned page_hash (const struct hash_elem *, void *aux UNUSED);
static bool page_less (const struct hash_elem *, const struct hash_elem *, void *aux UNUSED);
static struct page_entry *page_lookup (void *, struct hash *);
static void page_free_entry (struct hash_elem *e, void *aux UNUSED);

void
page_init (struct thread* t)
{
  hash_init(&t->page_table.table, page_hash, page_less, NULL);
  sema_init(&t->page_table.lock, 1);
}

bool
page_alloc_page (void *uaddr, bool writable, bool stack, bool mmap, bool zero_page, struct file *file, size_t start_offset, size_t read_bytes)
{
  void *upage = (void *) pg_round_down (uaddr);
  struct thread *t = thread_current ();
  struct supp_page_table *spt = &t->page_table;

  DEBUGA("[%s] page_alloc_page:: Allocating page for upage %p\n", t->name, upage);

  if (page_lookup (upage, &spt->table) == NULL)
    {
      struct page_entry *p = malloc (sizeof (struct page_entry));

      p->upage = upage;
      p->pagedir = t->pagedir;
      p->lock_p = &t->page_table.lock;

      p->kpage = NULL;
      p->swap_num = -1;

      p->force_swap = false;

      p->writable = writable;
      //if (p->writable)
      //  p->force_swap = true;

      p->stack = stack;
      if (p->stack)
        p->force_swap = true;

      p->mmap = mmap;
      p->zero_page = zero_page;

      p->file = file;
      p->start_offset = start_offset;
      p->read_bytes = read_bytes;

      sema_down (&spt->lock);
      hash_insert (&spt->table, &p->elem);
      sema_up (&spt->lock);

      DEBUGB("[%s] page_alloc_page:: Added page entry: upage; %p, writable: %d\n", t->name, upage, writable);
      return true;
    }
  DEBUGB ("%s: Cannot load page to %p, address in use\n", t->name, upage);
  return false;
}

void
page_free_page (void *uaddr)
{
  void *upage = (void *) pg_round_down (uaddr);
  struct thread *t = thread_current ();
  struct supp_page_table *spt = &t->page_table;

  struct page_entry *p = page_lookup (upage, &spt->table);

  if (p != NULL)
    {

      if (p->kpage != NULL)
        frame_free_page (p->kpage);

      if (p->swap_num > -1)
        swap_free_page (p->swap_num);

      sema_down (&spt->lock);
      hash_delete (&spt->table, &p->elem);
      sema_up (&spt->lock);

      free (p);
      return;
    }
  PANIC ("[%s] No page allocated for address %p\n", t->name, upage);
}

bool
page_fix_page (void *uaddr, bool stack, bool write, bool pin)
{
  void *upage = (void *) pg_round_down (uaddr);
  struct thread *t = thread_current ();
  struct supp_page_table *spt = &t->page_table;
  struct page_entry *p = page_lookup (upage, &spt->table);

  if (p == NULL)
    {
      if (write && stack)
        {
          DEBUGB("[%s] page_fix_page:: stack access, allocating page\n", t->name);
          page_alloc_page (upage, true, true, false, false, NULL, 0, 0);
          p = page_lookup (upage, &spt->table);
        }
      else
        {
          DEBUGA("[%s] page_fix_page:: bad pointer\n", t->name);
          return false;
        }
    }

  DEBUGC("[%s] page_fix_page:: fixing page\n", t->name);
  if (write && !p->writable)
    return false;

  DEBUGC("[%s] page_fix_page:: getting a frame\n", t->name);
  p->kpage = frame_get_page (p, pin);

  sema_down (&spt->lock);
  if (p->swap_num > -1)
    {
      DEBUGB("[%s] page_fix_page:: load page from swap %d to kpage: %p; upage %p\n", t->name, p->swap_num, p->kpage, p->upage);
      swap_load_page (p->kpage, p->swap_num);
      p->swap_num = -1;
    }
  else if (!p->stack && !p->zero_page) // not in swap, not a new stack page, not a zero page, has to be file
    {
      DEBUGB("[%s] page_fix_page:: reading %u bytes from file %p\n", t->name, p->read_bytes, p->file);
      if (file_read_at(p->file, p->kpage, p->read_bytes, p->start_offset) < p->read_bytes)
      {
        file_reopen(p->file);
        file_read_at(p->file, p->kpage, p->read_bytes, p->start_offset);
      }

      size_t zero_bytes = PGSIZE - p->read_bytes;
      if (zero_bytes > 0)
        {
          memset (p->kpage + p->read_bytes, 0, zero_bytes);
        }
    }
  else //if (p->stack || p->zero_page) //if it's stack and not in swap, give a new page OR if it's supposed to be 0, zero out
    {
      DEBUGB("[%s] page_fix_page:: writing zeros to kpage; p->stack: %d; p->zero_page: %d\n", t->name, p->stack, p->zero_page);
      memset(p->kpage, 0, PGSIZE);
    }
  DEBUGB("[%s] page_fix_page:: adding entry to pagedir: upage: %p, kpage: %p, %s: pagedir: %p\n", t->name, p->upage, p->kpage, t->name, p->pagedir);
  pagedir_set_page (p->pagedir, p->upage, p->kpage, p->writable);
  sema_up (&spt->lock);

  return true;
}

void
page_free_all (void)
{
  struct thread *t = thread_current();
  struct supp_page_table *spt = &t->page_table;

  hash_destroy (&spt->table, page_free_entry);
}

static void
page_free_entry (struct hash_elem *e, void *aux UNUSED)
{
  struct page_entry *p = hash_entry(e, struct page_entry, elem);

  if (p->kpage != NULL)
    frame_free_page (p->kpage);

  if (p->swap_num > -1)
    swap_free_page (p->swap_num);

  free(p);
}

bool
page_unmap (void* upage)
{
  struct thread *t = thread_current ();
  struct supp_page_table *spt = &t->page_table;
  struct page_entry *p = page_lookup (upage, &spt->table);

  if (p != NULL)
    {
      p->mmap = false;
      return true;
    }
  return false;
}

/* Returns a hash value for page p. */
static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page_entry *p = hash_entry (p_, struct page_entry, elem);
  return hash_bytes (&p->upage, sizeof (p->upage));
}

/* Returns true if page a precedes page b. */
static bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
  const struct page_entry *a = hash_entry (a_, struct page_entry, elem);
  const struct page_entry *b = hash_entry (b_, struct page_entry, elem);

  return a->upage < b->upage;
}

/*
 * Returns the page containing the given virtual address, or a null pointer
 * if no such page exists.
 */
static struct page_entry *
page_lookup (void *upage, struct hash *page_table)
{
  struct page_entry p;
  struct hash_elem *e;

  p.upage = upage;
  e = hash_find (page_table, &p.elem);
  DEBUGA("page_lookup:: %p was %sfound\n", upage, (e != NULL) ? "" : "NOT ");
  return e != NULL ? hash_entry (e, struct page_entry, elem) : NULL;
}

/*
void
page_pin_frame (void* upage)
{
  struct thread *t = thread_current ();
  DEBUGB("[%s] page_pin_frame:: pinning page %p\n", t->name, upage);
  page_fix_page(upage, true, true, true);
}
*/

bool
page_pin_frame (void *uaddr, bool write)
{
  void *upage = (void *) pg_round_down (uaddr);
  struct thread *t = thread_current ();
  struct supp_page_table *spt = &t->page_table;
  DEBUGB("[%s] page_pin_frame:: pinning page %p\n", t->name, upage);
  struct page_entry *p = page_lookup (upage, &spt->table);
  DEBUGB("[%s] page_pin_frame:: p: %p; p->kpage: %p\n", t->name, p, p->kpage);
  if (p != NULL && p->kpage != NULL)
    {
      frame_pin_frame (p->kpage);
      return true;
    }
  page_fix_page(upage, ((upage > t->last_stack) ? true: false), write, true);
  return false;
}

void
page_unpin_frame (void *uaddr)
{
  void *upage = (void *) pg_round_down (uaddr);
  struct thread *t = thread_current ();
  struct supp_page_table *spt = &t->page_table;
  DEBUGB("[%s] page_unpin_frame:: unpinning page %p\n", t->name, upage);

  struct page_entry *p = page_lookup (upage, &spt->table);

  //if (p != NULL && p->kpage != NULL)
  frame_unpin_frame (p->kpage);
}

