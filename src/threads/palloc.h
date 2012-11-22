#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>
#include <list.h>
#include <hash.h>
#include "threads/synch.h"
#include "filesys/file.h"

/* How to allocate pages. */
enum palloc_flags
  {
    PAL_ASSERT = 001,           /* Panic on failure. */
    PAL_ZERO = 002,             /* Zero page contents. */
    PAL_USER = 004              /* User page. */
  };

#define FRAME_LIMIT 500
#define SWAP_LIMIT 1<<13
#define FRAME_MAGIC 0xDEADBEEF

struct page
{
  bool readonly;
  bool zeroed;
  uint32_t sector;
  struct thread *owner;
  void* upage;
  struct hash_elem elem;
  struct file *file;
  off_t ofs;
  int frame_index;
};

unsigned char *swap_map;
struct lock swap_lock;
unsigned int swap_pointer;

struct page **frame_list;
struct lock frame_lock;
int frame_pointer;

void add_page_to_frames(struct page*, const int);
int allocate_frame_index(void);
void deallocate_frame_index(const int);
void restore_page(struct page*);

void palloc_init (size_t user_page_limit);
void *palloc_get_page (enum palloc_flags);
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt);
void palloc_free_page (void *);
void palloc_free_multiple (void *, size_t page_cnt);

#endif /* threads/palloc.h */
