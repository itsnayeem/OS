#include <stdio.h>
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/swap.h"
#include "userprog/syscall.h"
#include "userprog/debugf.h"

#define PAGE_SECTORS (PGSIZE / BLOCK_SECTOR_SIZE)

static struct block *swap_device;
static size_t num_slots;
static bool *free_map;

static size_t total_blocks;
struct semaphore swap_lock;

void
swap_init (void)
{
  unsigned i;

  sema_init(&swap_lock, 1);

  swap_device = block_get_role (BLOCK_SWAP);
  total_blocks = block_size (swap_device);
  num_slots = total_blocks / PAGE_SECTORS;
  free_map = malloc (sizeof (bool) * num_slots);

  for (i = 0; i < num_slots; i++) {
    free_map[i] = false;
  }

  DEBUGB ("swap_init:: total_blocks: %d; page_sector_size: %d; num_slots: %d\n",
	  total_blocks, PAGE_SECTORS, num_slots);
}

int
swap_dump_page (void *kpage)
{
  unsigned i;

  int swap_num = 0;

  sema_down(&swap_lock);
  while (free_map[swap_num]) {
    swap_num++;
  }
  free_map[swap_num] = true;
  sema_up(&swap_lock);

  block_sector_t sector = swap_num * PAGE_SECTORS;

  DEBUGB ("swap_dump_page:: dumping %p to swap_num: %d; starting sector: %d\n",
      kpage, swap_num, sector);

  if (sector + PAGE_SECTORS >= total_blocks)
    {
      DEBUGB ("swap_dump_page:: ERROR: last sector %d out of range (greater than %d)",
          sector + PAGE_SECTORS, total_blocks);
      return -1;
    }

  sema_down(&swap_lock);
  for (i = 0; i < PAGE_SECTORS; i++) {
    DEBUGB ("swap_dump_page:: writing data at address %p to sector %d\n", kpage + (i * BLOCK_SECTOR_SIZE), sector + i);
    //hex_dump (kpage + (i * BLOCK_SECTOR_SIZE), kpage + (i * BLOCK_SECTOR_SIZE), BLOCK_SECTOR_SIZE, true);
    block_write (swap_device, sector + i, kpage + (i * BLOCK_SECTOR_SIZE));
  }

  sema_up(&swap_lock);

  return swap_num;
}

void
swap_load_page (void *kpage, int swap_num)
{
  unsigned i;

  block_sector_t sector = swap_num * PAGE_SECTORS;
  DEBUGB ("swap_load_page:: loading to %p from swap_num: %d; starting sector: %d\n", kpage, swap_num, sector);

  if (sector + PAGE_SECTORS >= total_blocks)
    {
      DEBUGB ("swap_load_page:: ERROR: last sector %d out of range (greater than %d)",
          sector + PAGE_SECTORS, total_blocks);
      return;
    }

  sema_down (&swap_lock);
  for (i = 0; i < PAGE_SECTORS; i++)
    {
      block_read (swap_device, sector + i, kpage + (i * BLOCK_SECTOR_SIZE));
      DEBUGB ("swap_load_page:: writing data at address %p to sector %d\n", kpage + (i * BLOCK_SECTOR_SIZE), sector + i);
      //hex_dump (kpage + (i * BLOCK_SECTOR_SIZE), kpage + (i * BLOCK_SECTOR_SIZE), BLOCK_SECTOR_SIZE, true);
    }
  //sema_up (&swap_lock);

  //sema_down(&swap_lock);
  free_map[swap_num] = false;
  sema_up(&swap_lock);
}


void
swap_free_page (int swap_num)
{
  sema_down(&swap_lock);
  free_map[swap_num] = false;
  sema_up(&swap_lock);
}

