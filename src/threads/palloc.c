#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "devices/block.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "userprog/exception.h"

/* Page allocator.  Hands out memory in page-size (or
   page-multiple) chunks.  See malloc.h for an allocator that
   hands out smaller chunks.

   System memory is divided into two "pools" called the kernel
   and user pools.  The user pool is for user (virtual) memory
   pages, the kernel pool for everything else.  The idea here is
   that the kernel needs to have memory for its own operations
   even if user processes are swapping like mad.

   By default, half of system RAM is given to the kernel pool and
   half to the user pool.  That should be huge overkill for the
   kernel pool, but that's just fine for demonstration purposes. */

/* A memory pool. */
struct pool
  {
    struct lock lock;                   /* Mutual exclusion. */
    struct bitmap *used_map;            /* Bitmap of free pages. */
    uint8_t *base;                      /* Base of pool. */
  };

/* Two pools: one for kernel data, one for user pages. */
static struct pool kernel_pool, user_pool;

static void init_pool (struct pool *, void *base, size_t page_cnt,
                       const char *name);
static bool page_from_pool (const struct pool *, void *page);

static unsigned get_swap_sector(void);

/* Initializes the page allocator.  At most USER_PAGE_LIMIT
   pages are put into the user pool. */
void
palloc_init (size_t user_page_limit)
{
  /* Free memory starts at 1 MB and runs to the end of RAM. */
  uint8_t *free_start = ptov (1024 * 1024);
  uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
  size_t free_pages = (free_end - free_start) / PGSIZE;
  size_t user_pages = free_pages / 2;
  size_t kernel_pages;
  if (user_pages > user_page_limit)
    user_pages = user_page_limit;
  kernel_pages = free_pages - user_pages;

  /* Give half of memory to kernel, half to user. */
  init_pool (&kernel_pool, free_start, kernel_pages, "kernel pool");
  init_pool (&user_pool, free_start + kernel_pages * PGSIZE,
             user_pages, "user pool");

  lock_init(&frame_lock);
  list_init(&frame_list);
  frame_size = 0;
  lock_init(&swap_lock);
  swap_map = (unsigned char*)malloc(sizeof(char) * SWAP_LIMIT);
  int i;
  for( i = 0; i < SWAP_LIMIT; i++) {
    swap_map[i] = 0;
  }
  swap_pointer = 1;
}

/* Obtains and returns a group of PAGE_CNT contiguous free pages.
   If PAL_USER is set, the pages are obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the pages are filled with zeros.  If too few pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
  void *pages;
  size_t page_idx;

  if (page_cnt == 0)
    return NULL;

  lock_acquire (&pool->lock);
  page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
  lock_release (&pool->lock);

  if (page_idx != BITMAP_ERROR)
    pages = pool->base + PGSIZE * page_idx;
  else
    pages = NULL;

  if (pages != NULL) 
    {
      if (flags & PAL_ZERO)
        memset (pages, 0, PGSIZE * page_cnt);
    }
  else 
    {
      if (flags & PAL_ASSERT)
        PANIC ("palloc_get: out of pages");
    }
    
  return pages;
}

/* Obtains a single free page and returns its kernel virtual
   address.
   If PAL_USER is set, the page is obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the page is filled with zeros.  If no pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_page (enum palloc_flags flags) 
{
  return palloc_get_multiple (flags, 1);
}

/* Frees the PAGE_CNT pages starting at PAGES. */
void
palloc_free_multiple (void *pages, size_t page_cnt) 
{
  struct pool *pool;
  size_t page_idx;

  ASSERT (pg_ofs (pages) == 0);
  if (pages == NULL || page_cnt == 0)
    return;

  if (page_from_pool (&kernel_pool, pages))
    pool = &kernel_pool;
  else if (page_from_pool (&user_pool, pages))
    pool = &user_pool;
  else
    NOT_REACHED ();

  page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG
  memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

  ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
  bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
}

/* Frees the page at PAGE. */
void
palloc_free_page (void *page) 
{
  palloc_free_multiple (page, 1);
}

/* Initializes pool P as starting at START and ending at END,
   naming it NAME for debugging purposes. */
static void
init_pool (struct pool *p, void *base, size_t page_cnt, const char *name) 
{
  /* We'll put the pool's used_map at its base.
     Calculate the space needed for the bitmap
     and subtract it from the pool's size. */
  size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (page_cnt), PGSIZE);
  if (bm_pages > page_cnt)
    PANIC ("Not enough memory in %s for bitmap.", name);
  page_cnt -= bm_pages;

  printf ("%zu pages available in %s.\n", page_cnt, name);

  /* Initialize the pool. */
  lock_init (&p->lock);
  p->used_map = bitmap_create_in_buf (page_cnt, base, bm_pages * PGSIZE);
  p->base = base + bm_pages * PGSIZE;
}

/* Returns true if PAGE was allocated from POOL,
   false otherwise. */
static bool
page_from_pool (const struct pool *pool, void *page) 
{
  size_t page_no = pg_no (page);
  size_t start_page = pg_no (pool->base);
  size_t end_page = start_page + bitmap_size (pool->used_map);

  return page_no >= start_page && page_no < end_page;
}

void add_page_to_frames(struct page *p) // which file typedefs uintptr_t? :S
{
  lock_acquire( &frame_lock );
  struct frame *frame = (struct frame*) malloc(sizeof (struct frame));
  frame->placer = thread_current();
  frame->page = p;
  p->frame = frame;
  //printf("page %p with uaddr %p added to frames by thread id: %d\n",p, p->entry->upage,thread_current()->tid);
  list_push_back(&frame_list, &frame->elem);
  lock_release( &frame_lock );
}

void evict_frame() {
  ASSERT ( lock_held_by_current_thread(&frame_lock) );
  ASSERT ( frame_size >= FRAME_LIMIT );

  struct frame *f = list_entry( list_pop_front(&frame_list), struct frame, elem );
  struct page *p = f->page;
  void *upage = p->upage;
  void *page = pagedir_get_page( f->placer->pagedir, upage );
  if(pagedir_is_dirty( f->placer->pagedir, upage ) || (!p->zeroed && p->file == NULL && p->sector == 0)) {
    if( p->file != NULL ) {
      file_close(p->file);
      p->file = NULL;
    }
    p->zeroed = false;
    struct block* swap = block_get_role(BLOCK_SWAP);
//    printf("page table index: %u\n", pt_no(page));
//    printf("page directory index: %u\n", pd_no(page));
//    printf("thread pagedir: %u\n", pd_no(thread_current()->pagedir));
//    block_sector_t sector = get_swap_sector();
    if(p->sector == 0) {
      p->sector = get_swap_sector();
    }
//    printf("sector: %u\n", sector);

    int i;
    for( i = 0; i < 8; i++) {
      block_write(swap, p->sector + i, page + i * 512);
    }
    swap_write_cnt++;
  }

  p->frame = NULL;
//  printf("uaddr %p swapped to sector %u by thread id: %d\n", upage, sector, f->placer->tid);
//  unsigned *kvals = (unsigned*)page;
//  unsigned *uvals = (unsigned*)upage;
//  printf("kernel: %x %x %x %x\n", *kvals, *(kvals+1), *(kvals+2), *(kvals+3));
  //printf("user: %x %x %x %x\n", *uvals, *(uvals+1), *(uvals+2), *(uvals+3));
//  p->swapped = 1;


  pagedir_clear_page( f->placer->pagedir, upage);
  palloc_free_page( page );

  free(f);
  frame_size--;
}

static unsigned get_swap_sector() {
  lock_acquire(&swap_lock);
  unsigned i;
  for( i = swap_pointer; i < (SWAP_LIMIT); i++ ) {
    if( !swap_map[i] ) {
      swap_map[i] = 1;
      swap_pointer++;
      lock_release(&swap_lock);
      return i<<3;
    }
  }
  lock_release(&swap_lock);
  return -1;
}

void restore_page( struct page *p ) {
  ASSERT ( p != NULL );

  uint8_t *kpage = palloc_get_page( PAL_USER );
//  printf("kpage: %p\n", kpage);
//  printf("restoring page %p\n", p->upage);


  ASSERT ( kpage != NULL );

  if( p->zeroed ) {
//  printf("restoring zeroed page %p\n", p->upage);
//    printf("restoring zeroed page\n");
//    p->zeroed = false;
    memset( kpage, 0, PGSIZE );
    zero_cnt++;
  } else if ( p->file != NULL ) {
//  printf("restoring demand page %p\n", p->upage);
//    printf("demand paging\n");
    file_seek(p->file, p->ofs);
    if (file_read (p->file, kpage, PGSIZE) != (int) PGSIZE) {
      PANIC("file read size mismatch\n");
    }
//    unsigned *pvals = kpage;
//    printf("demanded page: %x %x %x %x\n",*pvals, *(pvals+1), *(pvals+2), *(pvals+3));
//    if(!p->readonly) {
//      file_close(p->file);
//      p->file = NULL;
//    }
    demand_cnt++;
//    file_close(p->file);
  } else {
//  printf("restoring swapped page %p\n", p->upage);

//    printf("restoring swapped page\n");
//    ASSERT ( p->swapped );
    struct block* swap = block_get_role(BLOCK_SWAP);
    block_sector_t sector = p->sector;

    //printf("thread %d is restoring page uaddr: %p from sector %u\n", thread_current()->tid, p->entry->upage, sector);
    int i;
    for( i = 0; i < 8; i++ ) {
      // what is the destination of this read from block? We need to load the data from disk/exec file
      // into the designated frame page rather than the VM page.
      block_read( swap, sector + i, kpage + i * 512 );
    }
    /*lock_acquire( &swap_lock );
    unsigned swap_index = sector>>3;
    swap_map[swap_index] = 0;
    if(swap_index < swap_pointer) {
      swap_pointer = swap_index;
    }
    lock_release( &swap_lock );
    */
  //  unsigned *kvals = (unsigned*)kpage;
  //  unsigned *uvals = (unsigned*)upage;
  //  printf("kernel: %x %x %x %x\n", *kvals, *(kvals+1), *(kvals+2), *(kvals+3));
  //  printf("user: %x %x %x %x\n", *uvals, *(uvals+1), *(uvals+2), *(uvals+3));
    swap_read_cnt++;
  }

  pagedir_set_page( thread_current()->pagedir, p->upage, kpage, !p->readonly);
  pagedir_set_dirty( thread_current()->pagedir, p->upage, false );
  add_page_to_frames(p);
//  printf("done restoring\n");
}

