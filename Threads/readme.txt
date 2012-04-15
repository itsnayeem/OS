			+--------------------+
			|        CS 326      |
			| PROJECT 1: THREADS |
			|   DESIGN DOCUMENT  |
			+--------------------+

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

			     ALARM CLOCK
			     ===========

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

In thread.h 
	struct thread 
	{
		...
		
		struct list_elem sleeping_elem		/* Elem to keep track of position in sleeping_list */
		int64_t wake_tick;                  /* ALARM: tick number the thread will wake up at */
    	struct semaphore sem;               /* ALARM: dummy semaphore thread waits on to go to sleep */
	}

In timer.c
	static struct list sleeping_list; 		/* List that stores the threads currently sleeping */


---- ALGORITHMS ----

>> A2: Briefly describe what happens in a call to timer_sleep(),
>> including the effects of the timer interrupt handler.

	When a thread needs to sleep it adds itself (in order by wake-up time) to the list of sleeping threads and waits on its own semaphore. The timer interrput handler checks to see if the first thread in the list should be woken up on the current tick. If so, the thread will be removed from the list and its semaphore will be upped. The interrupt handler will continue to check threads to see if they should be woken up. If not, it yields upon return. 

>> A3: What steps are taken to minimize the amount of time spent in
>> the timer interrupt handler?

	The interrupt handler will only ever check the first element in the list of sleeping threads. We are able to do this because the threads are inserted in order, therefore eliminating the need to traverse the entire list. 

---- SYNCHRONIZATION ----

>> A4: How are race conditions avoided when multiple threads call
>> timer_sleep() simultaneously?

	When accessing the list of sleeping threads, we disable interrupts disallowing any concurrent access. 

>> A5: How are race conditions avoided when a timer interrupt occurs
>> during a call to timer_sleep()?
	
	Because interrupts are disabled when accessing the list of sleeping threads, there is no chance of race conditions. 

---- RATIONALE ----

>> A6: Why did you choose this design?  In what ways is it superior to
>> another design you considered?

	Since the interruput handler is called on every tick, timer_sleep() is called less often then timer_interrupt(), Therefore, the ordering of the list should happen on timer_sleep(), rather than timer_interrupt(). 

			 PRIORITY SCHEDULING
			 ===================

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

In thread.h
	/* Donation node */
	struct donation
	{
	    struct list_elem elem;
	    struct lock *resource;  /* when donation is active, points to lock */
	    int *priority;          /* always points to donating thread's active_priority */
	};

	struct thread 
	{
		...
		
		int base_priority;                  /* DONATION: stores base priority of thread */
	    struct lock *resource;              /* DONATION: pointer to resource that another thread donated for */
	    struct list donation_list;          /* DONATION: list of all donations to thread */
	    struct donation donation_content;   /* DONATION: thread's donation node which is added to another
	    												  thread's donation list when it donates */
	};

>> B2: Explain the data structure used to track priority donation.
>> Use ASCII art to diagram a nested donation.  (Alternately, submit a
>> .png file.)

	The donation node stores the data thread_a gives to thread_b when thread_a donates. The donation node of a thread resides in its own context and is added to another thread's donation_list when it donates.

---- ALGORITHMS ----

>> B3: How do you ensure that the highest priority thread waiting for
>> a lock, semaphore, or condition variable wakes up first?

	When a lock, semaphore, or condition variable is released, the thread with the highest priority will be selected via the list_max() function comparing threads' active_priority.

>> B4: Describe the sequence of events when a call to lock_acquire()
>> causes a priority donation.  How is nested donation handled?

	If the lock has a holder, the acquring thread's donation node is added to the holder's donation_list. The holder's active_priority is reevaluated via thread_set_max_donation() which will go through a thread's base priority and all donations, choosing the max priority and setting it to active.  

	The acquring thread will then wait on the semaphore until the lock is released. Once the lock is released, a thread will remove its donation from the lock's donation_list (explained below). The acquiring thread will go through the lock's donation_list and add any donations that lock may have to its own donation_list. 

	The acquring thread will go through each lock and lock-holder and reevaluate their active_priorities, and resetting them to the first donee's possible new active_priority. 

>> B5: Describe the sequence of events when lock_release() is called
>> on a lock that a higher-priority thread is waiting for.

	A thread will go through its own donation_list and move any donations that match the currently releasing lock to the lock's donation_list, effectively passing along multiple donations to the next acquring thread, and finally, releases the lock. 

---- SYNCHRONIZATION ----

>> B6: Describe a potential race in thread_set_priority() and explain
>> how your implementation avoids it.  Can you use a lock to avoid
>> this race?

	If a donation is added during the execution of thread_set_priority(), then the resulting priority will be inconsistent. Our implementation uses an interrupt handler to avoid this race condition. Using locks could potentially alter the priority of a thread. 

---- RATIONALE ----

>> B7: Why did you choose this design?  In what ways is it superior to
>> another design you considered?

	Each thread will only donate its priority once before it waits, therefore there is only one possible donation for every thread. The donation node contains a pointer to the donating thread's active priority, so if it changes (if another thread donates to it while it is waiting), any donee of that thread will use the new priority. 

	Since multiple threads can donate to a single resource, a list of all donations must be mainted by either the resource-holder or the resource itself. 

	A thread does not directly modify another thread's priority because that thread may contain a higher priority donation. 

			  ADVANCED SCHEDULER
			  ==================

---- DATA STRUCTURES ----

>> C1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

In thread.h
	struct thread 
	{
		...
		
		int nice;                           /* MLFQS: thread's niceness */
    	fixed_point_t recent_cpu;           /* MLFQS: thread's recent_cpu usage */
	}

In thread.c
	static fixed_point_t load_avg;	

---- ALGORITHMS ----

>> C2: Suppose threads A, B, and C have nice values 0, 1, and 2.  Each
>> has a recent_cpu value of 0.  Fill in the table below showing the
>> scheduling decision and the priority and recent_cpu values for each
>> thread after each given number of timer ticks:

timer  recent_cpu    priority   thread
ticks   A   B   C   A   B   C   to run
-----  --  --  --  --  --  --   ------
 0      0   0   0  64  62  60     A
 4	4   0   0  63  62  60 	  A
 8      8   0   0  62  62  60     B
12      8   4   0  62  61  60     A
16     12   4   0  61  61  60     B
20     12   8   0  61  60  60     A
24     16  12   0  60  60  60     C
28     16  12   4  60  60  59     A
32     20  12   4  59  60  59     B
36     20  16   4  59  59  59     C

>> C3: Did any ambiguities in the scheduler specification make values
>> in the table uncertain?  If so, what rule did you use to resolve
>> them?  Does this match the behavior of your scheduler?

	It was unspecified how the scheduler should handle threads that result in a "tie". Our solution was to implement the scheduler in a "round-robin" fashion. 

>> C4: How is the way you divided the cost of scheduling between code
>> inside and outside interrupt context likely to affect performance?

	We decided to calculate the the load_averages and recent_cpu on every 100th tick, whereas the priority was calculated every 4th tick with a remainder of 1. This way, the priority calculation will never overlap with the other calculations. 

---- RATIONALE ----

>> C5: Briefly critique your design, pointing out advantages and
>> disadvantages in your design choices.  If you were to have extra
>> time to work on this part of the project, how might you choose to
>> refine or improve your design?

	A disadvantage of our design choice is that we take away a lot cpu-time from the currently running thread. In order to improve, we could decide to recalculate the priorities less frequently if there is a lower number of running threads. 

>> C6: The assignment explains arithmetic for fixed-point math in
>> detail, but it leaves it open to you to implement it.  Why did you
>> decide to implement it the way you did?  If you created an
>> abstraction layer for fixed-point math, that is, an abstract data
>> type and/or a set of functions or macros to manipulate fixed-point
>> numbers, why did you do so?  If not, why not?

	We used the provided fixed-point.h, which simulates floating point values by using the lower half of the integer value as the fractional part of the floating point value and the upper half as the integer part of the floating point value. With the use of bit shifting, floating point values can be simulated.

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
