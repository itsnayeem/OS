#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/debugf.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/swap.h"

#define MAX_ARGS_NUM 10
#define MAX_CL_SIZE 128

struct name_cl
{
  char file_name[24];
  struct thread* parr_t;
  char cl_copy[0];
};

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static bool move_stack(uint32_t *bottom, uint32_t **esp, size_t size);
static bool init_stack_args(uint32_t **esp, char *cl_copy);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *command_line)
{
  struct name_cl *cl_data;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  cl_data = palloc_get_page (0);
  if (cl_data == NULL)
    return TID_ERROR;

  strlcpy (cl_data->cl_copy, command_line, MAX_CL_SIZE);
  *((char*)cl_data + PGSIZE - sizeof(struct name_cl) - 1) = '\0';

  size_t file_name_len = 1;
  char *cl_p = cl_data->cl_copy;

  DEBUGF("*** Extracting file_name from %s\n", cl_data->cl_copy);

  while (*cl_p != ' ' && *cl_p != '\0' && file_name_len < 23)
    {
      cl_p++;
      file_name_len++;
    }
  strlcpy(cl_data->file_name, cl_data->cl_copy, file_name_len);
  cl_data->file_name[file_name_len] = '\0';
  cl_data->file_name[23] = '\0';

  /* Create a new thread to execute FILE_NAME. */
  cl_data->parr_t = thread_current ();

  tid = thread_create (cl_data->file_name, PRI_DEFAULT, start_process, (void*)cl_data);
  sema_down(&cl_data->parr_t->load_sem);

  if (cl_data->parr_t->load_status < 0)
  {
    DEBUGF("Load status is %d\n", cl_data->parr_t->load_status);
    tid = TID_ERROR;
  }

  palloc_free_page (cl_data);
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *cl_data_)
{
  struct thread *t = thread_current ();
  struct name_cl *cl_data = cl_data_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (cl_data->file_name, &if_.eip, &if_.esp);

  if (success)
    {
      success = init_stack_args ((uint32_t**)&if_.esp, cl_data->cl_copy);
      t->exec = filesys_open (cl_data->file_name);
      file_deny_write (t->exec);
    }
  //sema up here
  cl_data->parr_t->load_status = (success) ? 1 : -1;
  sema_up(&cl_data->parr_t->load_sem);

  /* If load failed, quit. */
  if (!success)
    thread_exit ();


  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid)
{
  struct list_elem* e;
  struct thread* parent = thread_current();
  struct child_info* c_info;
  struct wait_info* w_info;


  for (e = list_begin(&parent->children); e != list_end(&parent->children); e = list_next(e))
  {
    c_info = list_entry(e, struct child_info, elem);
    w_info = c_info->info;
    if (w_info->thread_id == child_tid)
    {
      DEBUGF("%s: Found child: %d\n", parent->name, w_info->thread_id);
      list_remove(e);
      sema_down(&w_info->sem);
      DEBUGF("%s: Returning child exit status: %d\n", parent->name, w_info->exit_status);
      w_info->num_ptrs --;
      int exit_status = w_info->exit_status;
      if (w_info->num_ptrs == 0)
        free(w_info);
      free(c_info);
      return exit_status;
    }
  }

  return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  struct list_elem *e;
  struct fd_node *f;

  mmap_free_all();

  while (!list_empty(&cur->fd_table))
    {
      e = list_pop_front (&cur->fd_table);
      f = list_entry (e, struct fd_node, elem);
      file_close(f->f_ptr);
      free(f);
    }

  page_free_all();

  if (cur->exec != NULL)
  {
    file_close (cur->exec);
  }

  cur->info->num_ptrs--;
  if (cur->info->num_ptrs == 0)
    free(cur->info);
  sema_up(&cur->info->sem);

  DEBUGF("%s: Setting exit status to %d and returning\n", cur->name, cur->info->exit_status);
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      //cur->pagedir = NULL;
      //pagedir_activate (NULL);
      //pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      bool zero_page = (page_zero_bytes == PGSIZE);

      /* Set page entry */
      page_alloc_page (upage, writable, false, false, zero_page, file, ofs, page_read_bytes);
      DEBUGA("load_segment:: Allocated page at upage: %p\n", upage);

      /* Advance. */
      read_bytes -= page_read_bytes;
      ofs += page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  DEBUGA("** %s: Finished load_segment\n", thread_current()->name);
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp)
{
  page_alloc_page(((uint8_t *) PHYS_BASE) - PGSIZE, true, true, false, false, NULL, 0, 0);

  DEBUGA("setup_stack:: %p\n", ((uint8_t *) PHYS_BASE) - PGSIZE);

  *esp = PHYS_BASE;

  return true;
}

static bool
init_stack_args(uint32_t **esp, char *cl_copy)
{
  bool success = true;
  uint32_t *bottom = *esp - PGSIZE;
  uint32_t argc, cl_length;
  char *ptr = NULL;
  uint32_t* argv_p;
  char *save_ptr;

  cl_length = (uint32_t)strlen(cl_copy) + 1;
  argc = 0;

  //write cl
  if (success && (success = move_stack(bottom, esp, cl_length)))
  {
    DEBUGF("Pushing cl: %s to %p\n",cl_copy, *esp);
    ptr = memcpy(*esp, cl_copy, cl_length);
  }

  strtok_r(cl_copy, " ", &save_ptr);
  argc++;
  while (strtok_r(NULL, " ", &save_ptr))
    argc++;


  //pad
  if (success)
    *esp = (uint32_t*)ROUND_DOWN((uint32_t)*esp, sizeof(uint32_t));
  DEBUGF("Padding esp, moved to: %p\n", *esp);


  //push null
  if (success && (success = move_stack(bottom, esp, sizeof(uint32_t))))
  {
    DEBUGF("pushing null to %p\n", *esp);
    **esp = (uint32_t)NULL;
  }


  //write argv
  if (success && (success = move_stack(bottom, esp, (argc) * sizeof(uint32_t))))
    {
      uint32_t *temp_esp = *esp;

      argv_p = temp_esp;
      DEBUGF("writing pointers starting at %p\n", temp_esp);

      //tokenize
      ptr = strtok_r(ptr, " ", &save_ptr);
      do
        {
          // move passed all whitespace
          DEBUGF("writing %p to %p\n", ptr, temp_esp);
          *temp_esp = (uint32_t)ptr;
          temp_esp++;
        }
      while ((ptr = strtok_r(NULL, " ", &save_ptr)) != NULL);
    }

  if (success && (success = move_stack(bottom, esp, sizeof(uint32_t))))
  {
    DEBUGF("pushing argv_p: %p to %p\n", argv_p, *esp);
    **esp = (uint32_t)argv_p;
  }

  if (success && (success = move_stack(bottom, esp, sizeof(uint32_t))))
  {
    DEBUGF("pushing argc: %d to %p\n", argc, *esp);
    **esp = argc;
  }

  //push null
  if (success && (success = move_stack(bottom, esp, sizeof(uint32_t))))
  {
    DEBUGF("pushing null to %p\n", *esp);
    **esp = (uint32_t)NULL;
  }


  #ifdef SHOW_HEX
  hex_dump(*esp, *esp, PHYS_BASE - *(void**)esp, true);
  #endif
  return success;
}

static bool
move_stack(uint32_t *bottom, uint32_t **esp, size_t size)
{
  if ((*(uint8_t**)esp - size) > (uint8_t*)bottom)
    {
      *(uint8_t**)esp -= size;
      DEBUGF("*** moved esp to %p\n", *esp);
      return true;
    }
  return false;
}

