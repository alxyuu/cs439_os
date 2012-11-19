#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>
#include <list.h>
#include <hash.h>
#include "threads/synch.h"

/* How to allocate pages. */
enum palloc_flags
  {
    PAL_ASSERT = 001,           /* Panic on failure. */
    PAL_ZERO = 002,             /* Zero page contents. */
    PAL_USER = 004              /* User page. */
  };

#define FRAME_LIMIT 64

struct frame
{
  struct thread *placer; // the thread that put this frame into the frame table
  //uintptr_t addr; // address of this frame.  Should be a physical address in the kernel.  Can't this just be denoted by &frame?
  struct list_elem elem;
  struct page *page; // the page that maps to this frame
};

struct page
{
  bool swapped;
  bool zeroed;
  bool readonly;
  bool dirty;
  struct frame *frame;
  struct page_entry *entry;
};

struct page_entry
{
  void* vaddr;
  struct page* page;
  struct hash_elem elem;
};

//unsigned char swap[1<<20] = {0};

struct lock frame_lock;
struct list frame_list;
int frame_size;

void add_page_to_frames(struct page *p);
void evict_frame(void);

void palloc_init (size_t user_page_limit);
void *palloc_get_page (enum palloc_flags);
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt);
void palloc_free_page (void *);
void palloc_free_multiple (void *, size_t page_cnt);

#endif /* threads/palloc.h */
