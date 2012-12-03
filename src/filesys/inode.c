#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Total number of direct block poniters.
 *
 * BLOCK_SECTOR_SIZE 512bytes / uint32_t block_sector_t 4bytes = 128 blocks
 *
 * For an inode on disk to hold a file with size of 8MB = 8388608bytes.
 *
 * Max file size: (126 + 127 + 127*127)* 512bytes = 8387584bytes ~ 7.99902MB
 *
 * 8388608bytes/ 512(bytes/sector) = 16,384 sectors.
 *
 * BLOCK_SECTOR_SIZE / sizeof (block_sector_t) = 512 / 4 = 128. */
#define DIRECT_BLOCK_CNT 124

/* Max block_sector number. */
#define MAX_BLOCK_CNT 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
	off_t length;                                /* File size in bytes. */
	block_sector_t blocks[DIRECT_BLOCK_CNT]; /* Indexes of direct blocks and indirect blocks.*/
	block_sector_t indirect;
	block_sector_t secondIndirect;

	unsigned magic;                              /* Magic number. */
	//uint32_t unused[125];                      /* Not used. */
};

struct indirect
{
	block_sector_t blocks[MAX_BLOCK_CNT];
};

/* In-memory inode. */
struct inode 
{
	struct list_elem elem;              /* Element in inode list. */
	block_sector_t sector;              /* Sector number of disk location. */
	int open_cnt;                       /* Number of openers. */
	bool removed;                       /* True if deleted, false otherwise. */
	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	struct inode_disk data;             /* Inode content. */
	struct indirect *indirect;
	struct indirect *secondIndirectBlocks;
	struct indirect **secondIndirect;
};


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
	return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

static bool
read_indirect(struct indirect *indirect, block_sector_t block) {
	if(block == -1) {
		return false;
	}
	block_read( fs_device, block, indirect );
	return true;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos)
{
	ASSERT (inode != NULL);
	if (pos < inode->data.length) {
		int index = pos / BLOCK_SECTOR_SIZE;
		if( index < DIRECT_BLOCK_CNT ) { // in inode
			return inode->data.blocks[index];
		} else if ( index < DIRECT_BLOCK_CNT + MAX_BLOCK_CNT ) {
			index -= DIRECT_BLOCK_CNT;
			if( inode->data.indirect == -1 ) {
				return -1;
			}
			if( inode->indirect == NULL ) {
				inode->indirect = malloc(sizeof(struct indirect));
				ASSERT ( inode->indirect != NULL );
				if( ! read_indirect(inode->indirect, inode->data.indirect) ) {
					free(inode->indirect);
					inode->indirect = NULL;
					return -1;
				}
			}
			return inode->indirect->blocks[index];
		} else {
			index -= DIRECT_BLOCK_CNT + MAX_BLOCK_CNT;
			int secondIndex = index / MAX_BLOCK_CNT;
			index -= secondIndex * MAX_BLOCK_CNT;

			if( inode->data.secondIndirect == -1 ) {
				return -1;
			}
			if( inode->secondIndirect == NULL || inode->secondIndirectBlocks == NULL ) {
				inode->secondIndirect = calloc( sizeof(struct indirect*), MAX_BLOCK_CNT );
				ASSERT ( inode->secondIndirect != NULL );
				inode->secondIndirectBlocks = malloc(sizeof(struct indirect));
				ASSERT ( inode->secondIndirectBlocks != NULL );
				if( ! read_indirect(inode->secondIndirectBlocks, inode->data.secondIndirect) ) {
					free(inode->secondIndirectBlocks);
					inode->secondIndirectBlocks = NULL;
					free(inode->secondIndirect);
					inode->secondIndirect = NULL;
					return -1;
				}
			}
			if( inode->secondIndirectBlocks->blocks[secondIndex] == -1 ) {
				return -1;
			}
			if( inode->secondIndirect[secondIndex] == NULL ) {
				inode->secondIndirect[secondIndex] = malloc( sizeof(struct indirect) );
				ASSERT ( inode->secondIndirect[secondIndex] != NULL );
				if( ! read_indirect(inode->secondIndirect[secondIndex], inode->secondIndirectBlocks->blocks[secondIndex])) {
					free(inode->secondIndirect[secondIndex]);
					inode->secondIndirect[secondIndex] = NULL;
					return -1;
				}
			}
			return inode->secondIndirect[secondIndex]->blocks[index];
		}
	} else {
		return -1;
	}
}

static block_sector_t allocate_sector(struct inode *inode, off_t offset) {

	ASSERT (inode != NULL);

	if (offset < inode->data.length) {
		unsigned index = offset / BLOCK_SECTOR_SIZE;
		if( index < DIRECT_BLOCK_CNT ) { // in inode
			 if(free_map_allocate(1, &inode->data.blocks[index])) {
				 return inode->data.blocks[index];
			 } else {
				 return -1;
			 }
		} else if ( index < DIRECT_BLOCK_CNT + MAX_BLOCK_CNT ) {
			index -= DIRECT_BLOCK_CNT;
			if( inode->data.indirect == -1 ) {
				if( free_map_allocate(1, &inode->data.indirect) ) {
					inode->indirect = malloc(sizeof(struct indirect));
					ASSERT ( inode->indirect != NULL );
					memset(inode->indirect, -1, MAX_BLOCK_CNT);
				} else {
					return -1;
				}
			}
			if(free_map_allocate(1, &inode->indirect->blocks[index])) {
				block_write (fs_device, inode->data.indirect, inode->indirect);
				return inode->indirect->blocks[index];
			} else {
				return -1;
			}
		} else {
			index -= DIRECT_BLOCK_CNT + MAX_BLOCK_CNT;
			int secondIndex = index / MAX_BLOCK_CNT;
			index -= secondIndex * MAX_BLOCK_CNT;

			if( inode->data.secondIndirect == -1 ) {
				if( free_map_allocate(1, &inode->data.secondIndirect) ) {
					inode->secondIndirect = calloc( sizeof(struct indirect*), MAX_BLOCK_CNT );
					ASSERT ( inode->secondIndirect != NULL );
					inode->secondIndirectBlocks = malloc(sizeof(struct indirect));
					ASSERT ( inode->secondIndirectBlocks != NULL );
					memset(inode->secondIndirectBlocks, -1, MAX_BLOCK_CNT);
				} else {
					return -1;
				}
			}

			if( inode->secondIndirectBlocks->blocks[secondIndex] == -1 ) {
				if( free_map_allocate(1, &inode->secondIndirectBlocks->blocks[secondIndex]) ) {
					inode->secondIndirect[secondIndex] = malloc(sizeof(struct indirect));
					ASSERT( inode->secondIndirect[secondIndex] != NULL );
					memset( inode->secondIndirect[secondIndex], -1, MAX_BLOCK_CNT );
					block_write( fs_device, inode->data.secondIndirect, inode->secondIndirectBlocks );
				}
			}

			if(free_map_allocate(1, &inode->secondIndirect[secondIndex]->blocks[index])) {
				block_write( fs_device, inode->secondIndirectBlocks->blocks[secondIndex], inode->secondIndirect[secondIndex] );
				return inode->secondIndirect[secondIndex]->blocks[index];
			} else {
				return -1;
			}
		}
	} else {
		return -1;
	}
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
	list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL)
	{
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		disk_inode->indirect = -1; //allocate later
		disk_inode->secondIndirect = -1; //allocate later
		memset(disk_inode->blocks, -1, DIRECT_BLOCK_CNT);
		/*int i;
		//static char zero[BLOCK_SECTOR_SIZE];
		for( i = 0; i < DIRECT_BLOCK_CNT; i++) {
			if(!free_map_allocate(1, &disk_inode->blocks[i])) {
				//TODO: free already allocated
				return false;
			}
		}*/
		/*if( sectors > DIRECT_BLOCK_CNT + MAX_BLOCK_CNT ) { // second indirection
    	  if( !free_map_allocate(1, &disk_inode->secondIndirect) ) {
    		  goto done;
    	  }
      }
      if ( sectors > DIRECT_BLOCK_CNT ) {
    	  if( !free_map_allocate(1, &disk_inode->indirect) ) {
    		  if( sectors > DIRECT_BLOCK_CNT + MAX_BLOCK_CNT ) {
    			  free_map_release( disk_inode->secondIndirect, 1 );
    		  }
    		  goto done;
    	  }
      }*/
		block_write (fs_device, sector, disk_inode);
		success = true;
		done:
		free (disk_inode);
	}
	return success;
}
//
//static block_sector_t
//allocate_sector (block_sector_t *block_content)
//{
//  block_sector_t allocated_sector = -1;
//  if (free_map_allocate (1, &allocated_sector))
//    block_write(fs_device, allocated_sector, *block_content); // how to write to disk?
//  return allocated_sector;
//}
//
//static void
//expand_inode (struct inode *inode, off_t pos)
//{
//  block_sector_t *empty_block = calloc (1, BLOCK_SECTOR_SIZE);
//  struct inode_disk *inode_disk_block = calloc (1, BLOCK_SECTOR_SIZE);
//  block_read(inode->sector,inode_disk_block, ????); // how to read the sector number from the disk?
//
//  int i;
//
//  // this part should be changed since disk writes by 512bytes instead of off_t size which is 4 bytes
//  while (inode_disk_block->end < pos)
//  {
//	// make sure the pos block is less than MAX_BLOCK_CNT
//    for (i = inode_disk_block->end + 1; i <= pos; i++)
//      inode_disk_block->blocks[i] = allocate_sector (empty_block);
//  }
//
//  // update length and end
//  inode_disk_block->end = pos;
//  inode_disk_block->length = inode_disk_block->end;
//
//  // update inode on the disk
//}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e))
	{
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector)
		{
			inode_reopen (inode);
			return inode;
		}
	}

	/* Allocate memory. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	inode->indirect = NULL;
	inode->secondIndirect = NULL;
	inode->secondIndirectBlocks = NULL;
	block_read (fs_device, inode->sector, &inode->data);
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
	return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0)
	{
		/* Remove from inode list and release lock. */
		list_remove (&inode->elem);

		/* Deallocate blocks if removed. */
		if (inode->removed)
		{
			free_map_release (inode->sector, 1);
			int i;
			for( i = 0; i < DIRECT_BLOCK_CNT; i++) {
				if( inode->data.blocks[i] != -1 ) {
					free_map_release(inode->data.blocks[i], 1);
				}
			}
			if( inode->indirect != NULL ) {
				for( i = 0 ; i < MAX_BLOCK_CNT; i++) {
					if( inode->indirect->blocks[i] != -1 ) {
						free_map_release(inode->indirect->blocks[i], 1);
					}
				}
			}
			if( inode->secondIndirectBlocks != NULL ) {
				ASSERT (inode->secondIndirect != NULL);
				int k;
				for(i = 0; i < MAX_BLOCK_CNT; i++) {
					if( inode->secondIndirectBlocks->blocks[i] != -1 ) {
						ASSERT( inode->secondIndirect[i] != NULL );
						for(k = 0; k < MAX_BLOCK_CNT; k++) {
							if( inode->secondIndirect[i]->blocks[k] != NULL ) {
								free_map_release( inode->secondIndirect[i]->blocks[k], 1 );
							}
						}
						free_map_release( inode->secondIndirectBlocks->blocks[i], 1 );
					}
				}
			}
		}

		if( inode->indirect != NULL ) {
			free(inode->indirect);
		}
		if( inode->secondIndirectBlocks != NULL ) {
			ASSERT (inode->secondIndirect != NULL);
			int i;
			for(i = 0; i < MAX_BLOCK_CNT; i++) {
				if(inode->secondIndirect[i] != NULL) {
					free(inode->secondIndirect[i]);
				}
			}
			free(inode->secondIndirect);
			free(inode->secondIndirectBlocks);
		}
		free (inode);
	}
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
	ASSERT (inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0)
	{
		/* Disk sector to read, starting byte offset within sector. */
		block_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if( sector_idx != -1 ) {
			if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
			{
				/* Read full sector directly into caller's buffer. */
				block_read (fs_device, sector_idx, buffer + bytes_read);
			}
			else
			{
				/* Read sector into bounce buffer, then partially copy
				 into caller's buffer. */
				if (bounce == NULL)
				{
					bounce = malloc (BLOCK_SECTOR_SIZE);
					if (bounce == NULL)
						break;
				}
				block_read (fs_device, sector_idx, bounce);
				memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
			}
		} else {
			memset(buffer + bytes_read, 0, chunk_size);
		}
		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);

	return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset)
{
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	while (size > 0)
	{
		/* Sector to write, starting byte offset within sector. */
		block_sector_t sector_idx = byte_to_sector (inode, offset);
		if( sector_idx == -1 ) {
			sector_idx = allocate_sector(inode, offset);
			//printf("allocated sector: %u\n", sector_idx);
		}
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
		{
			/* Write full sector directly to disk. */
			block_write (fs_device, sector_idx, buffer + bytes_written);
		}
		else
		{
			/* We need a bounce buffer. */
			if (bounce == NULL)
			{
				bounce = malloc (BLOCK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left)
				block_read (fs_device, sector_idx, bounce);
			else
				memset (bounce, 0, BLOCK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			block_write (fs_device, sector_idx, bounce);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);

	return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
	return inode->data.length;
}
