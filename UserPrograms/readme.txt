		     +--------------------------+
       	     |         CS 326		    |
		     | PROJECT 3: USER PROGRAMS	|
		     | 	   DESIGN DOCUMENT     	|
		     +--------------------------+

---- GROUP ----

TEAM++

Simon Piel <spiel@dons.usfca.edu>
Chris Corea <ccorea@dons.usfca.edu>
Shah El-Rahman <snelrahman@cs.usfca.edu>

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

			   ARGUMENT PASSING
			   ================

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

struct name_cl
{
  char file_name[24];
  struct thread* parr_t;
  char cl_copy[0];
};
  This struct has a pointer to the parent thread, a char array containing a copy of the file_name,
  and a copy of the command line.

 
---- ALGORITHMS ----

>> A2: Briefly describe how you implemented argument parsing.  How do
>> you arrange for the elements of argv[] to be in the right order?
>> How do you avoid overflowing the stack page?
  
  We first copied the command line to the stack and word-align it by moving the stack pointer. We then tokenize the string while simultaneously writing the pointers backward to the stack; we then write a pointer to the pointer array. Finally, we push NULL as the return value. 

  We avoid overflowing the stack page by checking if PHYS_BASE minus PG_SIZE, the total amount of space we have, is less than the value of our stack pointer mius the size of what we want to push to the stack. 

---- RATIONALE ----

>> A3: Why does Pintos implement strtok_r() but not strtok()?
  
  strtok_r() is implemented in Pintos rather than strtok() because strtok_r() uses a SAVE_PTR to help maintain position when making successive calls to the same string. For example, when parsing each argument from the command line, strtok_r() allows us to maintain a current position when tokenizing the string.

>> A4: In Pintos, the kernel separates commands into an executable name
>> and arguments.  In Unix-like systems, the shell does this
>> separation.  Identify at least two advantages of the Unix approach.

  One advantage is that validation occurs in user-space rather than kernel-space, ultimately saving time by not context switching. A second advantage is that tokenizing can happen within the shell, as opposed to using kernel-time. 

			     SYSTEM CALLS
			     ============

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

struct child_info
{
  struct list_elem elem;
  struct wait_info* info;
};
  This struct allows us to maintain a thread's child in a list and keep a pointer to that child's wait_info struct. 

struct wait_info
{
  struct semaphore sem;
  int exit_status;
  tid_t thread_id;
  uint8_t num_ptrs;
};
  This struct contains the information for each thread. In order to wait, we block on the semaphore. The exit_status holds the value of the thread's exit_status so that we know if the thread has successfully terminated. thread_id allows us to identify a thread and num_ptrs keeps track of how many threads are still pointing to the struct (when 0, we free the memory).

struct fd_node {
  struct list_elem elem;
  int fd;
  struct file* f_ptr;
};
  This struct acts as a single entry in our file descriptor table. 

>> B2: Describe how file descriptors are associated with open files.
>> Are file descriptors unique within the entire OS or just within a
>> single process?

  When a file is open, an entry is added to the file descriptor list. File descriptors are only unique with a single process.

---- ALGORITHMS ----

>> B3: Describe your code for reading and writing user data from the
>> kernel.
  
  We ensure that the virtual address is valid and read or write to memory.

>> B4: Suppose a system call causes a full page (4,096 bytes) of data
>> to be copied from user space into the kernel.  What is the least
>> and the greatest possible number of inspections of the page table
>> (e.g. calls to pagedir_get_page()) that might result?  What about
>> for a system call that only copies 2 bytes of data?  Is there room
>> for improvement in these numbers, and how much?

  pagedir_get_page() is only called when setting up the stack. At the moment, the size of the data does not matter, as we only allocate one page for a user program.  

>> B5: Briefly describe your implementation of the "wait" system call
>> and how it interacts with process termination.

  We give every thread an "info" struct that keeps all of the information about a thread. When a thread exits, it sets the exit_status and allows any other thread waiting on it to continue. If a thread is waiting on another thread and that thread is a child, then it will wait on the semaphore in the info struct until the child has exited. 

>> B6: Any access to user program memory at a user-specified address
>> can fail due to a bad pointer value.  Such accesses must cause the
>> process to be terminated.  System calls are fraught with such
>> accesses, e.g. a "write" system call requires reading the system
>> call number from the user stack, then each of the call's three
>> arguments, then an arbitrary amount of user memory, and any of
>> these can fail at any point. I like big butts and this poses a design and
>> error-handling problem: how do you best avoid obscuring the primary
>> function of code in a morass of error-handling?  Furthermore, when
>> an error is detected, how do you ensure that all temporarily
>> allocated resources (locks, buffers, etc.) are freed?  In a few
>> paragraphs, describe the strategy or strategies you adopted for
>> managing these issues.  Give an example.

  When a user provides a pointer, we make sure that the pointer points to memory in user space. If it is not, the syscall will return failure. If a user calls read with an invalid buffer pointer, the read will return -1.

---- SYNCHRONIZATION ----

>> B7: The "exec" system call returns -1 if loading the new executable
>> fails, so it cannot return before the new executable has completed
>> loading.  How does your code ensure this?  How is the load
>> success/failure status passed back to the thread that calls "exec"?

  We block on wait_info semaphore until the loading has completed, by which time the exit_status will be set. 

>> B8: Consider parent process P with child process C.  How do you
>> ensure proper synchronization and avoid race conditions when P
>> calls wait(C) before C exits?  After C exits?  How do you ensure
>> that all resources are freed in each case?  How about when P
>> terminates without waiting, before C exits?  After C exits?  Are
>> there any special cases?

  We use a semaphore to to avoid race conditions between parent and child threads. 

---- RATIONALE ----

>> B9: Why did you choose to implement access to user memory from the
>> kernel in the way that you did?

  This method takes advantage of the processor's MMU. 

>> B10: What advantages or disadvantages can you see to your design
>> for file descriptors?

  The lookup time would be better with an array, but it would not be as memory efficient as malleable list. 

>> B11: The default tid_t to pid_t mapping is the identity mapping.
>> If you changed it, what advantages are there to your approach?

  No change.

			   SURVEY QUESTIONS
			   ================

Answering these questions is optional, but it will help us improve the
course in future quarters.  Feel free to tell us anything you
want--these questions are just to spur your thoughts.  You may also
choose to respond anonymously in the course evaluations at the end of
the quarter.

>> In your opinion, was this assignment, or any one of the three problems
>> in it, too easy or too hard?  Did it take too long or too little time?

>> Did you find that working on a particular part of the assignment gave
>> you greater insight into some aspect of OS design?

>> Is there some particular fact or hint we should give students in
>> future quarters to help them solve the problems?  Conversely, did you
>> find any of our guidance to be misleading?

>> Do you have any suggestions for the TAs to more effectively assist
>> students, either for future quarters or the remaining projects?

>> Any other comments?
