#ifndef VM_SWAP_H
#define VM_SWAP_H

void swap_init (void);
int swap_dump_page (void *kpage);
void swap_load_page (void *kpage, int swap_num);
void swap_free_page (int swap_num);

#endif /* vm/swap.h */
