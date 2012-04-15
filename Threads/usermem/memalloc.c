#include <stdio.h>
#include <stdarg.h>
#include "memalloc.h"
#include "list.h"
#include <debug.h>
#include <pthread.h>

//#define debug
//#define verbose
static struct list free_list;   // list of free blocks

static pthread_mutex_t lock;

/* Comparator for Pintos list functions. */
static bool mem_block_less(const struct list_elem *a, const struct list_elem *b, void *aux) {
    return (a < b);
}

/* Return number of elements in free list. */
size_t mem_sizeof_free_list(void) {
    return list_size(&free_list);
}

/* Dump a list to screen. */
static void mem_dump_list(struct list *l) {
    struct list_elem* elem_p;
    struct free_block* free_p;
    int i = 0;

    for (elem_p = list_begin(l); elem_p != list_end(l); elem_p = list_next(elem_p)) {
        free_p = list_entry(elem_p, struct free_block, elem);
        printf("\tlist[%d]: @%d; len=%zu\n", i++, (int)free_p, free_p->length);
    }
}

/* Dump free list to screen. */
void mem_dump_free_list(void) {
    printf("Dumping free list:\n");
    mem_dump_list(&free_list);
}

/* Initialize allocator. */
void mem_init(uint8_t *base, size_t length) {
    //initialize list
    list_init(&free_list);

    //initialize block and push
    struct free_block* first_node = (struct free_block*) base;
    first_node->length = length;
    list_push_front(&free_list, &first_node->elem);

    //initialize lock
    pthread_mutex_init(&lock, NULL);

#ifdef debug
    printf("Initializing memory of size: %d\nAddress space: ", length);
    printf("@%d to @%d (total bytes: %d)\n", (int)base, (int)((void*)base + length), (int)((void*)base + length) - (int)base);
    printf("Other info:\n\tsizeof(int): %zu\n", sizeof(int));
    printf("\tsizeof(size_t): %zu\n", sizeof(size_t));
    printf("\tsizeof(size_t*): %zu\n", sizeof(size_t*));
    printf("\tsizeof(struct free_block): %zu\n", sizeof(struct free_block));
    printf("\tsizeof(struct used_block): %zu\n\n", sizeof(struct used_block));
#endif
    return;
}

/* Allocate by finding free block using first-fit, in address order. */
void * mem_alloc(size_t length) {
    struct list_elem* elem_p = NULL;
    struct free_block* free_p = NULL;
    struct used_block* used_p = NULL;
    size_t actual_length;

    ASSERT (length % 4 == 0);

    actual_length = length + sizeof(struct used_block);

#ifdef debug
    printf("MALLOC: length=%zu actual=%d\n", length, actual_length);
#endif

    // if actual length is too small
    if (actual_length < sizeof(struct free_block)) {
        // set actual length to size of free block header
        actual_length = sizeof(struct free_block);
        // set user space to size of free block header minus size of used block header
        length = actual_length - sizeof(struct used_block);
#ifdef debug
        printf("*** requested length too small. new length: %zu; actual_length: %zu\n", length, actual_length);
#endif
    }

#ifdef verbose
    mem_dump_free_list();
#endif

    pthread_mutex_lock(&lock);
    for (elem_p = list_begin(&free_list); elem_p != list_end(&free_list); elem_p = list_next(elem_p)) {

        free_p = list_entry(elem_p, struct free_block, elem);

        if (free_p->length >= actual_length) {
          if (free_p->length == actual_length || (free_p->length - actual_length) < sizeof(struct free_block)) {
              actual_length = free_p->length;
              length = actual_length - sizeof(struct used_block);
              list_remove(elem_p);
              used_p = (struct used_block*) free_p;
#ifdef debug
              printf("\tUsing a full block. @%d len=%d\n", (int) used_p, length);
#endif
          } else if (free_p->length > actual_length) {
              free_p->length = free_p->length - actual_length;
              used_p = (struct used_block*)((void*) free_p + free_p->length);
#ifdef debug
              printf("\tSplitting block.\n\t\tfree block: @%d len=%d\n", (int) free_p, free_p->length);
              printf("\t\tused block: @%d leng=%d\n", (int) used_p, length);
#endif
          }
          used_p->length = length;
          pthread_mutex_unlock(&lock);
          return (void*) used_p->data;
        }
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

/* Determine whether two blocks are adjacent. */
static bool mem_block_is_adjacent(const struct free_block *left, const struct free_block *right) {
    return ((void*)left + left->length == (void*)right);
}

/* Coalesce two adjacent blocks. */
static void mem_coalesce(struct free_block *left, struct free_block *right) {
    left->length = left->length + right->length;
    list_remove(&right->elem);
}

/* Free memory, coalescing free list if necessary. */
void mem_free(void *ptr) {
    struct list_elem* temp_elem_p;
    struct used_block* used_p;
    struct free_block* free_p;
    struct free_block* temp_free_p;
    size_t actual_length;

#ifdef debug
    printf("FREE: %d\n",(int) ptr);
#endif
#ifdef verbose
    printf("Before: ");
    mem_dump_free_list();
#endif

    pthread_mutex_lock(&lock);

    used_p = (struct used_block*) (ptr - sizeof(struct used_block));
    actual_length = used_p->length + sizeof(struct used_block);

    free_p = (struct free_block*) used_p;
    free_p->length = actual_length;
    list_insert_ordered(&free_list, &free_p->elem, mem_block_less, NULL);


#ifdef debug
    printf("\tAdded free block: @%d len=%d\n", (int) free_p, free_p->length);
#endif

#ifdef verbose
    printf("After: ");
    mem_dump_free_list();
#endif

    temp_elem_p = list_prev(&free_p->elem);
    if (temp_elem_p != list_head(&free_list)) {
        temp_free_p = list_entry(temp_elem_p, struct free_block, elem);
#ifdef verbose
        printf("Checking before @%d+%d == %d\n", (int) temp_free_p, temp_free_p->length, (int) free_p);
#endif
        if (mem_block_is_adjacent(temp_free_p, free_p)) {
#ifdef verbose
            printf("Merging before @%d+%d == %d\n", (int) temp_free_p, temp_free_p->length, (int) free_p);
#endif
            mem_coalesce(temp_free_p, free_p);
            free_p = temp_free_p;
        }
    }

    temp_elem_p = list_next(&free_p->elem);
    if (temp_elem_p != list_tail(&free_list)) {
#ifdef verbose
        printf("Checking after @%d+%d == %d\n", (int) free_p, free_p->length, (int) temp_free_p);
#endif
        temp_free_p = list_entry(temp_elem_p, struct free_block, elem);
        if (mem_block_is_adjacent(free_p, temp_free_p)) {
#ifdef verbose
            printf("Merging after @%d+%d == %d\n", (int) free_p, free_p->length, (int) temp_free_p);
#endif
            mem_coalesce(free_p, temp_free_p);
        }
    }
    pthread_mutex_unlock(&lock);
}
