#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <hash.h>
#include <string.h>
#include "devices/shutdown.h"
#include "devices/block.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "lib/user/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/debugf.h"
#include "userprog/syscall.h"
#include "vm/page.h"

#define NUM_SYS_ARGS 15

typedef int syscall_function (uint32_t, uint32_t, uint32_t);
static void syscall_handler (struct intr_frame *);

static int get_user (const uint8_t *);
static bool user_to_kernel_memcopy (int *, uint32_t *, size_t);

static char *copy_to_kernel (const char *str);
static struct file *get_file_by_fd (int fd);

static syscall_function *sys_calls[NUM_SYS_ARGS];

static void sys_halt (void);
static pid_t sys_exec (const char *command_line);
static int sys_wait (pid_t pid);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_write (int fd, const char *buffer, unsigned size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);
static mapid_t sys_mmap (int fd, void *uaddr);
static void sys_munmap (mapid_t mmap);

//struct lock file_lock;

struct hash mapped_files;
static unsigned mmap_hash (const struct hash_elem *, void *aux UNUSED);
static bool mmap_less (const struct hash_elem *, const struct hash_elem *, void *aux UNUSED);
static void mmap_free_entry (struct hash_elem *e, void *aux UNUSED);

struct mmap_entry
{
  struct hash_elem elem;
  mapid_t mid;
  void* start_upage;
  struct file* file;
  unsigned num_pages;
};

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  sys_calls[SYS_HALT] = (syscall_function *) sys_halt;
  sys_calls[SYS_EXIT] = (syscall_function *) sys_exit;
  sys_calls[SYS_EXEC] = (syscall_function *) sys_exec;
  sys_calls[SYS_WAIT] = (syscall_function *) sys_wait;
  sys_calls[SYS_CREATE] = (syscall_function *) sys_create;
  sys_calls[SYS_REMOVE] = (syscall_function *) sys_remove;
  sys_calls[SYS_OPEN] = (syscall_function *) sys_open;
  sys_calls[SYS_FILESIZE] = (syscall_function *) sys_filesize;
  sys_calls[SYS_READ] = (syscall_function *) sys_read;
  sys_calls[SYS_WRITE] = (syscall_function *) sys_write;
  sys_calls[SYS_SEEK] = (syscall_function *) sys_seek;
  sys_calls[SYS_TELL] = (syscall_function *) sys_tell;
  sys_calls[SYS_CLOSE] = (syscall_function *) sys_close;
  sys_calls[SYS_MMAP] = (syscall_function *) sys_mmap;
  sys_calls[SYS_MUNMAP] = (syscall_function *) sys_munmap;

  sema_init (&file_lock, 1);
}

static void
syscall_handler (struct intr_frame *f)
{
  uint8_t *stack_ptr = f->esp;
  uint32_t args[4];
  size_t i;
  int ret_val;

#ifdef SHOW_HEX
  hex_dump (f->esp, f->esp, PHYS_BASE - f->esp, true);
#endif

  DEBUGF ("Stack ptr: %p; Stack ptr + 4: %p\n", stack_ptr, stack_ptr + 4);
  if (stack_ptr + 4 >= (uint8_t *) PHYS_BASE)
    sys_exit (-1);

  for (i = 0; i < 4; i++)
    {
      if (!user_to_kernel_memcopy
	        ((int *) stack_ptr + i, &args[i], sizeof (int)))
        {
	        //sys_exit(-1);
	      }
      DEBUGF ("args[%d] = %p\n", i, args[i]);
    }

  DEBUGF ("system call number = %d\n", args[0]);

  struct thread *t = thread_current();
  t->last_stack = f->esp;

  ret_val = sys_calls[args[0]] (args[1], args[2], args[3]);

  DEBUGF ("syscall finished, returning %d\n", ret_val);
  f->eax = ret_val;

  return;
}

static bool
user_to_kernel_memcopy (int *uaddr, uint32_t * kaddr, size_t size)
{
  size_t i;

  for (i = 0; i < size; i++)
    {
      //if (get_user ((void *) uaddr + i) < 0)
      if (!is_user_vaddr (uaddr + i))
	      return false;
      *((int *) ((void *) kaddr + i)) = get_user ((void *) uaddr + i);
    }
  return true;
}

static int
get_user (const uint8_t * uaddr)
{
  int result;
asm ("movl $1f, %0; movzbl %1, %0; 1:": "=&a" (result):"m" (*uaddr));
  return result;
}

static void
sys_halt (void)
{
  shutdown_power_off ();
}

void
sys_exit (int status)
{
  struct thread *cur = thread_current ();

  printf ("%s: exit(%d)\n", cur->name, status);
  cur->info->exit_status = status;
  thread_exit ();
}

static pid_t
sys_exec (const char *command_line)
{
  return process_execute (command_line);
}

static int
sys_wait (pid_t pid)
{
  return process_wait (pid);
}

static bool
sys_create (const char *file, unsigned initial_size)
{
  char *file_path = copy_to_kernel (file);

  bool retval = filesys_create (file_path, initial_size);

  palloc_free_page(file_path);

  return retval;
}

static bool
sys_remove (const char *file)
{
  char *file_path = copy_to_kernel (file);

  bool retval = filesys_remove (file_path);

  palloc_free_page(file_path);

  return retval;
}

static int
sys_open (const char *file)
{
  struct thread *t;
  char *file_path = copy_to_kernel (file);
  struct file *f = NULL;
  struct fd_node *fd_n;

  DEBUGF ("file = %s\n", file);
  DEBUGF ("file_path = %s\n", file_path);

  sema_down (&file_lock);
  f = filesys_open (file_path);
  sema_up (&file_lock);

  DEBUGF ("f = %p\n", f);
  if (f != NULL)
    {
      t = thread_current ();
      fd_n = malloc (sizeof (struct fd_node));

      fd_n->f_ptr = f;

      fd_n->fd = t->next_fd;

      list_push_back (&t->fd_table, &fd_n->elem);

      struct list_elem *e;
      int next_fd = 2;
      struct fd_node *temp;

      for (e = list_begin (&t->fd_table); e != list_end (&t->fd_table);
	        e = list_next (e))
        {
          temp = list_entry (e, struct fd_node, elem);
          if (next_fd != temp->fd)
            {
              break;
            }
          next_fd++;
        }

      t->next_fd = next_fd;
      palloc_free_page(file_path);
      return fd_n->fd;
    }
  palloc_free_page(file_path);
  return -1;
}

static int
sys_filesize (int fd)
{
  struct file *file = get_file_by_fd (fd);
  int retval = -1;

  if (file != NULL)
      retval = file_length (file);

  return retval;
}

static int
sys_read (int fd, void *buffer, unsigned size)
{
  int retval = -1;

  if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + size))
    sys_exit(-1);

  struct file *file = get_file_by_fd (fd);

  if (file != NULL)
  {
    size_t bytes_left = size;
    void *upage = pg_round_down (buffer);
    struct thread *t = thread_current();

    DEBUGB("[%s] sys_read:: writing to filesystem; buffer %p; size %d; end buff: %p\n", t->name, buffer, size, buffer + size);
    t->keep_pin = true;
    memset(buffer, 0, size);
    while (bytes_left > 0) {
      page_pin_frame (upage, true);
      bytes_left -= (bytes_left < PGSIZE) ? bytes_left : PGSIZE;
      upage += PGSIZE;
    }
    t->keep_pin = false;
    DEBUGB("[%s] sys_read:: pinning complete\n", t->name);


    sema_down (&file_lock);
    retval = file_read (file, buffer, size);
    sema_up (&file_lock);

    bytes_left = size;
    upage = pg_round_down (buffer);
    DEBUGB("[%s] sys_read:: unpinning all the pages\n", t->name);
    while (bytes_left > 0) {
      page_unpin_frame (upage);
      bytes_left -= (bytes_left < PGSIZE) ? bytes_left : PGSIZE;
      upage += PGSIZE;
    }
  }

  return retval;
}

static int
sys_write (int fd, const char *buffer, unsigned size)
{
  if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + size))
    sys_exit(-1);

  unsigned i;

  if (fd == 1)
    {
      for (i = 0; i < size; i++)
	      printf ("%c", *(buffer + i));

      return size;
    }

  int retval = -1;

  struct file *file = get_file_by_fd (fd);
  if (file != NULL)
    {
      size_t bytes_left = size;
      void *upage = pg_round_down (buffer);
      struct thread *t = thread_current ();
      t->keep_pin = true;
      DEBUGB("[%s] sys_write:: writing to filesystem; buffer %p; size %d; end buff: %p\n", t->name, buffer, size, buffer + size);
      while (bytes_left > 0 || upage - PGSIZE < buffer + size) {
        int dumm = *(int*)upage;
        page_pin_frame (upage, false);
        bytes_left -= (bytes_left < PGSIZE) ? bytes_left : PGSIZE;
        upage += PGSIZE;
      }
      t->keep_pin = false;
      DEBUGB("[%s] sys_write:: pinning complete\n", t->name);

      sema_down (&file_lock);
      retval = file_write (file, buffer, size);
      sema_up (&file_lock);

      bytes_left = size;
      upage = pg_round_down (buffer);
      DEBUGB("[%s] sys_write:: unpinning all the pages\n", t->name);
      while (bytes_left > 0) {
        page_unpin_frame (upage);
        bytes_left -= (bytes_left < PGSIZE) ? bytes_left : PGSIZE;
        upage += PGSIZE;
      }
    }

  return retval;
}

static void
sys_seek (int fd, unsigned position)
{
  struct file *file = get_file_by_fd (fd);

  if (file != NULL)
    {
      sema_down (&file_lock);
      file_seek (file, position);
      sema_up (&file_lock);
    }
}

static unsigned
sys_tell (int fd)
{
  struct file *file = get_file_by_fd (fd);
  int retval = -1;

  if (file != NULL)
    {
      sema_down (&file_lock);
      retval = file_tell (file);
      sema_up (&file_lock);
    }

  return retval;
}

static void
sys_close (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  struct fd_node *f;

  for (e = list_begin (&t->fd_table); e != list_end (&t->fd_table);
       e = list_next (e))
    {
      f = list_entry (e, struct fd_node, elem);
      if (f->fd == fd)
        {
          t->next_fd = f->fd;


          struct hash_iterator iter;
          struct mmap_entry *mme = NULL;

          hash_first (&iter, &t->m_table.table);
          while (hash_next (&iter))
            {
              mme = hash_entry (hash_cur (&iter), struct mmap_entry, elem);
              if (mme->file == f->f_ptr)
                {
                  unsigned i;
                  void *upage;

                  for (i = 0, upage = mme->start_upage; i < mme->num_pages; i++, upage += PGSIZE) {
                    page_unmap (upage);
                    page_fix_page (upage, false, false, false);
                  }
                  break;
                }
              }

          sema_down (&file_lock);
          file_close (f->f_ptr);
          sema_up (&file_lock);

          list_remove (e);
          free (f);
          return;
        }
    }
}

void
mmap_init (struct mmap_table *mt)
{
  hash_init (&mt->table, mmap_hash, mmap_less, NULL);
  mt->last_id = 0;
}

static mapid_t
sys_mmap (int fd, void *upage)
{
  if (upage > PHYS_BASE - 8 * 1024 * 1024 || fd == 0 || fd == 1 || upage == 0 || pg_ofs (upage))
    return -1;

  struct file *f = get_file_by_fd (fd);
  if (f == NULL)
    return -1;

  size_t read_bytes = file_length (f);
  if (read_bytes == 0)
    return -1;

  size_t ofs = 0;
  bool writable = file_is_allow_write(f);

  struct thread *t = thread_current ();

  struct mmap_entry *mme = malloc(sizeof(struct mmap_entry));
  mme->mid = t->m_table.last_id++;
  mme->start_upage = upage;
  mme->num_pages = 0;
  mme->file = f;

  DEBUGB("[%s] sys_mmap:: mapping file %p to upage %p; file_length: %u\n", t->name, mme->file, mme->start_upage, read_bytes);

  while (read_bytes > 0) {
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;

    if (!page_alloc_page (upage, writable, false, true, false, f, ofs, page_read_bytes))
      return -1;

    read_bytes -= page_read_bytes;
    ofs += page_read_bytes;
    upage +=PGSIZE;
    mme->num_pages++;
  }

  hash_insert (&t->m_table.table, &mme->elem);
  return mme->mid;
}

void
mmap_free_all (void)
{
  struct thread *t = thread_current();

  hash_destroy (&t->m_table.table, mmap_free_entry);
}

static void
mmap_free_entry (struct hash_elem *e, void *aux UNUSED)
{
  struct mmap_entry *mme = hash_entry (e, struct mmap_entry, elem);
  unsigned i;
  void* upage;

  for (i = 0, upage = mme->start_upage; i < mme->num_pages; i++, upage += PGSIZE) {
    page_free_page (upage);
  }

  free(mme);
}


static void
sys_munmap (mapid_t mid)
{
  struct thread *t = thread_current ();
  struct mmap_entry m;
  m.mid = mid;
  struct hash_elem *e = hash_find (&t->m_table.table, &m.elem);
  unsigned i;
  void *upage;

  if (e == NULL)
    return;

  struct mmap_entry *mme = hash_entry (e, struct mmap_entry, elem);

  for (i = 0, upage = mme->start_upage; i < mme->num_pages; i++, upage += PGSIZE) {
    page_free_page (upage);
  }
  hash_delete(&t->m_table.table, &mme->elem);
  free (mme);
}

static char *
copy_to_kernel (const char *str)
{
  int size = strlen (str) + 1;
  char *retval = palloc_get_page (1);

  if (size > PGSIZE)
    {
      size = PGSIZE;
    }

  strlcpy (retval, str, size);
  retval[PGSIZE - 1] = '\0';

  return retval;
}

static struct file *
get_file_by_fd (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  struct fd_node *f;

  struct file *file = NULL;

  for (e = list_begin (&t->fd_table); e != list_end (&t->fd_table);
       e = list_next (e))
    {
      f = list_entry (e, struct fd_node, elem);
      if (f->fd == fd)
        {
          file = f->f_ptr;
        }
    }
  return file;
}

/* Returns a hash value for page p. */
static unsigned
mmap_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct mmap_entry *mme = hash_entry (p_, struct mmap_entry, elem);
  return hash_bytes (&mme->mid, sizeof (mme->mid));
}

/* Returns true if page a precedes page b. */
static bool
mmap_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
  const struct mmap_entry *a = hash_entry (a_, struct mmap_entry, elem);
  const struct mmap_entry *b = hash_entry (b_, struct mmap_entry, elem);

  return a->mid < b->mid;
}

