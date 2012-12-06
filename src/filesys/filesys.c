#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();

  initialized = true;
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}


/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;

  struct dir* current;
  struct inode* inode;
  char* filename = malloc(sizeof(char) * (strlen(name) + 1));
  strlcpy(filename, name, strlen(name) + 1);
  if(*filename == '/') { //absolute
    current = dir_open_root();
  } else {
    if(!thread_current()->current_dir) {
      thread_current()->current_dir = ROOT_DIR_SECTOR;
    }
    current = dir_open(inode_open(thread_current()->current_dir));
  }
  bool success = false;
  char *token;
  char *save_ptr;
  char *newfile;
  for (token = strtok_r(filename, "/", &save_ptr); token != NULL; token = strtok_r(NULL, "/", &save_ptr)) {
    if(*token != '\0') {
      if(!dir_lookup(current, token, &inode)) {
        newfile = token;
        token = strtok_r(NULL, "/", &save_ptr);
        success = true;
        while(token != NULL) {
          if(*token != '\0') {
            success = false;
            break;
          }
          token = strtok_r(NULL, "/", &save_ptr);
        }
        break;
      } else {
        if(!inode_isdir(inode)) {
          break;
        } else {
          dir_close(current);
          current = dir_open(inode);
        }
      }
    }
  }

  if(success) {
       success = (current != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (current, newfile, inode_sector));
  }
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);

  free(filename);
  dir_close (current);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct inode *inode = NULL;

  struct dir* current;
  char* filename = malloc(sizeof(char) * (strlen(name) + 1));
  strlcpy(filename, name, strlen(name) + 1);
  if(*filename == '/') { //absolute
    current = dir_open_root();
  } else {
    if(!thread_current()->current_dir) {
      thread_current()->current_dir = ROOT_DIR_SECTOR;
    }
    current = dir_open(inode_open(thread_current()->current_dir));
  }
  bool success = false;
  char *token;
  char *save_ptr;
  char *newfile;
  for (token = strtok_r(filename, "/", &save_ptr); token != NULL; token = strtok_r(NULL, "/", &save_ptr)) {
    if(*token != '\0') {
      if(!dir_lookup(current, token, &inode)) {
        break;
      } else {
        if(!inode_isdir(inode)) {
          newfile = token;
          token = strtok_r(NULL, "/", &save_ptr);
          if(token == NULL) {
            success = true;
          }
          break;
        } else {
          dir_close(current);
          current = dir_open(inode);
        }
      }
    }
  }

  if (success && current != NULL) {
    dir_lookup (current, newfile, &inode);
  }
  dir_close (current);
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
