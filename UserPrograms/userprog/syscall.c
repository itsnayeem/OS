#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "userprog/debugf.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "lib/user/syscall.h"
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/palloc.h"

#define NUM_SYS_ARGS 13

typedef int syscall_function (uint32_t, uint32_t, uint32_t);
static void syscall_handler (struct intr_frame *);

/* ***** BEG ADDED ***** */
static int get_user (const uint8_t *);
static bool put_user (uint8_t *, uint8_t);
static bool user_to_kernel_memcopy (int *, uint32_t *, size_t);
/* ***** END ADDED ***** */
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

struct lock file_lock;

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

  lock_init (&file_lock);
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

  ret_val = sys_calls[args[0]] (args[1], args[2], args[3]);

  DEBUGF ("syscall finished, returning %d\n", ret_val);
  f->eax = ret_val;

  return;
}

/* ***** BEG ADDED ***** */

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

/* The following functions are provided in the project
 * documentation. They are helper functions for reading
 * from and writing to the user stack. If a page fault
 * occurs, indicating the user pointer was invalid, then
 * the return value will reflect this.
 */

static int
get_user (const uint8_t * uaddr)
{
  int result;
asm ("movl $1f, %0; movzbl %1, %0; 1:": "=&a" (result):"m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
 * UDST must be below PHYS_BASE.
 * Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t * udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:": "=&a" (error_code), "=m" (*udst):"q" (byte));
  return error_code != -1;
}

/* ***** END ADDED ***** */


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

  lock_acquire (&file_lock);
  f = filesys_open (file_path);
  lock_release (&file_lock);

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
    lock_acquire (&file_lock);
    retval = file_read (file, buffer, size);
    lock_release (&file_lock);    
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
      lock_acquire (&file_lock);     
      retval = file_write (file, buffer, size);
      lock_release (&file_lock);
    }

  return retval;
}

static void
sys_seek (int fd, unsigned position)
{
  struct file *file = get_file_by_fd (fd);

  if (file != NULL)
    {
      lock_acquire (&file_lock);
      file_seek (file, position);
      lock_release (&file_lock); 
    }  
}

static unsigned
sys_tell (int fd)
{
  struct file *file = get_file_by_fd (fd);
  int retval = -1;

  if (file != NULL)
    {  
      lock_acquire (&file_lock);
      retval = file_tell (file);
      lock_release (&file_lock);
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

          lock_acquire (&file_lock);
          file_close (f->f_ptr);
          lock_release (&file_lock);
          
          list_remove (e);
          free (f);
          return;
        }
    }
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
