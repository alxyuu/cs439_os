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
    inode = inode_open(thread_current()->current_dir);
    if(inode == NULL) {
      return false;
    }
    current = dir_open(inode);
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
        success = token == NULL;
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
//  printf("opening %s\n",name);
  struct dir* current;
  char* filename = malloc(sizeof(char) * (strlen(name) + 1));
  strlcpy(filename, name, strlen(name) + 1);
  if(*filename == '/') { //absolute
    current = dir_open_root();
  } else {
    if(!thread_current()->current_dir) {
      thread_current()->current_dir = ROOT_DIR_SECTOR;
    }
    inode = inode_open(thread_current()->current_dir);
//    printf("opened inode at sector %u\n", inode_get_inumber(inode));
    if(inode == NULL) {
      return NULL;
    }
    current = dir_open(inode);
  }
  bool success = false;
  char *token;
  char *save_ptr;
  char *newfile;
  bool isdir = false;
  if(!strcmp(filename,"/")) {
    newfile = ".";
    success = true;
  } else {
  for (token = strtok_r(filename, "/", &save_ptr); token != NULL; token = strtok_r(NULL, "/", &save_ptr)) {
    if(*token != '\0') {
      if(!dir_lookup(current, token, &inode)) {
        break;
      } else {
        if(!inode_isdir(inode)) {
          newfile = token;
          token = strtok_r(NULL, "/", &save_ptr);
          isdir = false;
          if(token == NULL) {
            success = true;
          }
          break;
        } else {
          dir_close(current);
          current = dir_open(inode);
          isdir = true;
        }
      }
    }
  }
  }

  if (success && current != NULL) {
    if(isdir){
      dir_lookup(current, "..", &inode);
      dir_close(current);
      current = dir_open(inode);
    }
    dir_lookup (current, newfile, &inode);
  }
/*  if(inode == NULL) {
    printf("success: %u\n", success);
    printf("current: %p\n", current);
    printf("inode: %p\n", dir_get_inode(current));
    printf("sector: %u\n", inode_get_inumber(dir_get_inode(current)));
    printf("current dir: %u\n", thread_current()->current_dir);
    //debug_filesys();
  }*/
  free(filename);
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
    inode = inode_open(thread_current()->current_dir);
    if(inode == NULL) {
      return false;
    }
    current = dir_open(inode);
  }
  bool success = false;
  bool isdir = true;
  char *token;
  char *save_ptr;
  char *toremove;
  for (token = strtok_r(filename, "/", &save_ptr); token != NULL; token = strtok_r(NULL, "/", &save_ptr)) {
    if(*token != '\0') {
      if(!dir_lookup(current, token, &inode)) {
        success = false;
        break;
      } else {
        toremove = token;
        if(!inode_isdir(inode)) {
          token = strtok_r(NULL, "/", &save_ptr);
          isdir = false;
          success = token == NULL;
          break;
        } else {
          dir_close(current);
          current = dir_open(inode);
          success = dir_isempty(current);
        }
      }
    }
  }

  if(success && isdir){
    dir_lookup(current, "..", &inode);
    dir_close(current);
    current = dir_open(inode);
  }
  success = success && current != NULL && toremove != NULL & dir_remove(current, toremove);
  dir_close (current);
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

static void debug_folder(struct dir * dir, int tabs, block_sector_t sector) {
  char name[15];
  int i;
  thread_current()->current_dir = sector;

  while(dir_readdir(dir, name)) {
    for(i = 0; i < tabs; i++) {
      printf("\t");
    }
    if(name[0] == '.') {
      printf("%s\n", name);
      continue;
    }
    printf("ASDFADSFASDF\n");
    struct file *file = filesys_open(name);
    printf("opening %s\n", name);
    printf("file: %p\n", file);
    printf("inode: %p\n", file_get_inode(file));
    if(inode_isdir(file_get_inode(file))) {
      printf("HI\n");
      printf("d:%s:%u\n", name, inode_get_inumber(file_get_inode(file)));
      debug_folder((struct dir*)file, tabs+1, inode_get_inumber(file_get_inode(file)));
      thread_current()->current_dir = sector;
    } else {
      printf("HI\n");
      printf("f:%s:%u\n", name, inode_get_inumber(file_get_inode(file)));
    }
    file_close(file);
  }
  dir_close(dir);
}

void debug_filesys() {
  char name[15];
  struct dir* dir = dir_open_root();
  printf("root:\n");
  while(dir_readdir(dir, name)) {
    printf("%s\n", name);
  }
  thread_current()->current_dir = ROOT_DIR_SECTOR;
  debug_folder(dir_open_root(), 0, ROOT_DIR_SECTOR);
}

