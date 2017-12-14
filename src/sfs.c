/*
  Simple File System

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.

*/

#include "block.h"
#include "log.h"
#include "params.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "dir.h"
#include "filedescriptor.h"
#include "fs.h"
#include "log.h"

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *sfs_init(struct fuse_conn_info *conn) {
  DECL_SFS_DATA(sfs_data);
  SFS_LOCK_OR_FAIL(sfs_data, NULL);

  log_msg("initializing");
  log_conn(conn);

  // create a pool of filedescriptors for associating with inodes
  sfs_data->fd_pool = sfs_filedescriptor_pool_init();
  if (sfs_data->fd_pool == NULL) {
    perror("sfs_filedescriptor_init()");
    kill(getpid(), SIGTERM);
    SFS_UNLOCK_OR_FAIL(sfs_data, NULL);
    return sfs_data;
  }

  // opens `diskfile` and creates a new filesystem if none is detected
  sfs_data->fs = sfs_fs_open_disk(sfs_data->disk, true);
  if (sfs_data->fs == NULL) {
    perror("sfs_fs_open_diskfile()");
    kill(getpid(), SIGTERM);
    SFS_UNLOCK_OR_FAIL(sfs_data, NULL);
    return sfs_data;
  }

  SFS_UNLOCK_OR_FAIL(sfs_data, NULL);
  return sfs_data;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void sfs_destroy(void *userdata) {
  DECL_SFS_DATA(sfs_data);

  if (sfs_data == NULL) return;

  SFS_LOCK_OR_FAIL(sfs_data, );

  log_msg("userdata=%p", userdata);

  if (sfs_data->fs != NULL) {
    sfs_fs_close(sfs_data->fs);
  }

  if (sfs_data->disk >= 0) {
    close(sfs_data->disk);
  }

  if (sfs_data->fd_pool != NULL) {
    sfs_filedescriptor_pool_deinit(sfs_data->fd_pool);
  }

  log_msg("successfully cleaned up");
  fclose(sfs_data->logfile);
  SFS_UNLOCK_OR_FAIL(sfs_data, );
  pthread_mutex_destroy(&sfs_data->mu);
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *statbuf) {
  DECL_SFS_DATA(sfs_data);
  SFS_LOCK_OR_FAIL(sfs_data, -1);

  log_msg("path=\"%s\", statbuf=%p", path, statbuf);

  // no additional directories yet
  char *path_copy = strdup(path);
  if (strcmp(dirname(path_copy), "/") != 0) {
    free(path_copy);
    log_msg("returning ENOENT");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENOENT;
  }
  free(path_copy);

  // get the name we'll put in the directory
  char name_buf[PATH_MAX];
  strncpy(name_buf, path, PATH_MAX);
  const char *name = basename(name_buf);
  if (strlen(name) > 255) {
    log_msg("returning ENAMETOOLONG");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENAMETOOLONG;
  }

  // start at the root directory's inode
  struct sfs_fs_inode directory;
  struct sfs_fs_inode file;
  if (sfs_dir_root(sfs_data->fs, &directory)) {
    log_msg("couldn't get root inode");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENOENT;  // technically "/" couldn't be found?
  }

  // short circuit here
  if (strcmp(path, "/") == 0) {
    sfs_fs_inode_to_stat(sfs_data->fs, &directory, statbuf);
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return 0;
  }

  // TODO: navigate down the directory tree

  // find `name` in the directory
  uint64_t found_inumber = 0;
  void *iter = sfs_dir_iterate(sfs_data->fs, &directory);
  if (iter == NULL) {
    log_msg("sfs_dir_iterate failed");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }
  struct sfs_dir_entry *direntry;
  while ((iter = sfs_dir_iternext(iter, &direntry, NULL)) != NULL) {
    if (strncmp(direntry->name, name, 256) == 0) {
      if (sfs_dir_iterclose(iter)) {
        log_msg("sfs_dir_iterclose failed");
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -1;
      }

      if (sfs_fs_read_inode(sfs_data->fs, direntry->inumber, &file)) {
        log_msg("error opening inode %" PRIu64, found_inumber);
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -1;
      }
      sfs_fs_inode_to_stat(sfs_data->fs, &file, statbuf);

      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return 0;
    }
  }

  log_msg("returning ENOENT");
  SFS_UNLOCK_OR_FAIL(sfs_data, -1);
  return -ENOENT;
}

/**
 * Linked List "tokens" that represents each dir/file name in string File Path.
 * Made to make traversal a bit easier
 */
typedef struct _Token {
  char *token;
  struct _Token *next;
} Token;

void printTokens(Token *head) {
  Token *traversal = head;
  printf("Printing Tokens\n");
  while (traversal != NULL) {
    printf("%s -->", traversal->token);
    traversal = traversal->next;
  }
}

void freeTokens(Token *head) {
  Token *prev = head;
  while (head != NULL) {
    head = head->next;
    free(prev->token);
    free(prev);
  }
}

/**
 * Traverse the given path and create tokens for each dir/file in the path
 */
Token *createTokens(char *path) {
  Token *head = malloc(sizeof(Token));
  head->next = NULL;
  head->token = NULL;
  Token *currToken = NULL;
  char *traversePath = strdup(path);
  char currentDir[256];
  int currDirIndex = 0;
  for (int i = 0; i < strlen(path); i++) {
    if (i == 0 && *(traversePath + i) == '/') {  // root inode operation
      // printf("Parsing started. Current character = /\n");
      // TODO:inode operations here
      head->token = malloc(2 * sizeof(char));
      head->token = "/";
      currToken = head;
      continue;
    }
    // printf("Parsing current character: %c \n",*(traversePath + i));
    if (*(traversePath + i) == '/') {
      currentDir[currDirIndex] =
          '\0';  // Null terminating the current directory parsed string
      printf("Parsed string: %s\n", currentDir);
      currToken->next = malloc(sizeof(Token));
      currToken = currToken->next;
      currToken->next = NULL;
      currToken->token = malloc(sizeof(strlen(currentDir)));
      strcpy(currToken->token, currentDir);
      currDirIndex = 0;
      memset(currentDir, 0, 256);
      // TODO: Inode operations here

    } else {
      currentDir[currDirIndex] = *(traversePath + i);
      currDirIndex++;
    }
  }

  printf("Last Directory/File: %s\n", currentDir);
  currToken->next = malloc(sizeof(Token));
  currToken = currToken->next;
  currToken->next = NULL;
  currToken->token = malloc(sizeof(strlen(currentDir)));
  strcpy(currToken->token, currentDir);
  printTokens(head);
  return head;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
  DECL_SFS_DATA(sfs_data);
  SFS_LOCK_OR_FAIL(sfs_data, -1);

  log_msg("path=\"%s\", mode=0%03o, fi=%p", path, mode, fi);

  // no additional directories yet
  char *path_copy = strdup(path);
  if (strcmp(dirname(path_copy), "/") != 0) {
    free(path_copy);
    log_msg("ENOENT");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENOENT;
  }
  free(path_copy);

  // get the name we'll put in the directory
  char name_buf[PATH_MAX];
  strncpy(name_buf, path, PATH_MAX);
  const char *name = basename(name_buf);
  if (strlen(name) > 255) {
    log_msg("returning ENAMETOOLONG");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENAMETOOLONG;
  }

  // start at the root directory's inode
  struct sfs_fs_inode directory;
  struct sfs_fs_inode file;
  if (sfs_dir_root(sfs_data->fs, &directory)) {
    log_msg("couldn't get root inode");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENOENT;  // technically "/" couldn't be found?
  }

  // TODO: navigate down the directory tree

  // search to see if `name` already exists in the directory
  uint64_t found_inumber = 0;
  void *iter = sfs_dir_iterate(sfs_data->fs, &directory);
  if (iter == NULL) {
    log_msg("sfs_dir_iterate failed");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }
  struct sfs_dir_entry *direntry;
  while ((iter = sfs_dir_iternext(iter, &direntry, NULL)) != NULL) {
    if (strncmp(direntry->name, name, 256) == 0) {
      found_inumber = direntry->inumber;

      if (sfs_dir_iterclose(iter)) {
        log_msg("sfs_dir_iterclose failed");
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -1;
      }

      // if `O_EXCL` is specified, fail with EEXIST
      if (fi->flags & O_EXCL) {
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -EEXIST;
      }

      // up the link count
      if (sfs_fs_read_inode(sfs_data->fs, found_inumber, &file)) {
        log_msg("error opening inode %" PRIu64, found_inumber);
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -1;
      }
      ++file.links;
      file.change_time = time(NULL);
      if (sfs_fs_write_inode(sfs_data->fs, &file)) {
        log_msg("error writing inode %" PRIu64, found_inumber);
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -1;
      }

      break;
    }
  }

  // `inode` still represents the directory here
  if (found_inumber == 0) {
    if (sfs_fs_inode_allocate(sfs_data->fs, &file)) {
      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return -EDQUOT;  // no more inodes
    }
    found_inumber = file.inumber;
    file.mode = (((1 << 12) - 1) & mode & (~FUSE_CALLER_UMASK));
    file.mode |= S_IFREG;  // is a regular file
    file.uid = FUSE_CALLER_UID;
    // depends on gid bit in parent directory (see man open(2))
    file.gid = (directory.mode & S_ISGID) ? file.gid : FUSE_CALLER_GID;
    file.links = 1;  // the link is the open file
    file.access_time = time(NULL);
    file.modified_time = time(NULL);
    file.change_time = time(NULL);
    file.size = 0;
    for (int i = 0; i < SFS_N_BLOCKS; ++i) {
      file.block_pointers[i] = 0;
    }
    if (sfs_fs_write_inode(sfs_data->fs, &file)) {
      log_msg("error writing inode");
      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return -1;
    }
    if (sfs_dir_link(sfs_data->fs, &directory, name, &file)) {
      log_msg("error linking file to directory");
      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return -1;
    }
  }

  struct sfs_fd *fd = sfs_filedescriptor_allocate(sfs_data->fd_pool);
  if (fd == NULL) {
    // should fail more gracefully
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENOMEM;
  }
  fd->inumber = found_inumber;
  fd->flags = fi->flags;
  log_msg("file_descriptor:");
  log_struct(fd, fd, "%d");
  log_struct(fd, inumber, "%" PRIu64);
  log_struct(fd, flags, "%" PRIu64);
  fi->fh = fd->fd;

  SFS_UNLOCK_OR_FAIL(sfs_data, -1);
  return 0;
}

/** Remove a file */
int sfs_unlink(const char *path) {
  DECL_SFS_DATA(sfs_data);
  SFS_LOCK_OR_FAIL(sfs_data, -1);

  log_msg("path=\"%s\"", path);

  // no additional directories yet
  char *path_copy = strdup(path);
  if (strcmp(dirname(path_copy), "/") != 0) {
    free(path_copy);
    log_msg("returning ENOENT");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENOENT;
  }
  free(path_copy);

  // get the name we'll put in the directory
  char name_buf[PATH_MAX];
  strncpy(name_buf, path, PATH_MAX);
  const char *name = basename(name_buf);
  if (strlen(name) > 255) {
    log_msg("returning ENAMETOOLONG");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENAMETOOLONG;
  }

  // start at the root directory's inode
  struct sfs_fs_inode directory;
  if (sfs_dir_root(sfs_data->fs, &directory)) {
    log_msg("couldn't get root inode");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENOENT;  // technically "/" couldn't be found?
  }

  // short circuit here
  if (strcmp(path, "/") == 0) {
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -EACCES;  // seems appropriate
  }

  // TODO: navigate down the directory tree

  // find `name` in the directory
  void *iter = sfs_dir_iterate(sfs_data->fs, &directory);
  if (iter == NULL) {
    log_msg("sfs_dir_iterate failed");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }
  struct sfs_dir_entry *direntry;
  while ((iter = sfs_dir_iternext(iter, &direntry, NULL)) != NULL) {
    if (strncmp(direntry->name, name, 256) == 0) {
      // decrease link count (possibly deallocate)
      // TODO: if `file` is actually a directory, we must check if the directory
      // is empty first!
      if (sfs_dir_iter_unlink(iter, direntry)) {
        log_msg("sfs_dir_iter_unlink failed");
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -1;
      }

      if (sfs_dir_iterclose(iter)) {
        log_msg("sfs_dir_iterclose failed");
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -1;
      }

      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return 0;
    }
  }

  log_msg("returning ENOENT");
  SFS_UNLOCK_OR_FAIL(sfs_data, -1);
  return -ENOENT;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int sfs_open(const char *path, struct fuse_file_info *fi) {
  DECL_SFS_DATA(sfs_data);
  SFS_LOCK_OR_FAIL(sfs_data, -1);

  log_msg("path=\"%s\", fi=%p", path, fi);

  // no additional directories yet
  char *path_copy = strdup(path);
  if (strcmp(dirname(path_copy), "/") != 0) {
    free(path_copy);
    log_msg("returning ENOENT");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENOENT;
  }
  free(path_copy);

  // get the name we'll put in the directory
  char name_buf[PATH_MAX];
  strncpy(name_buf, path, PATH_MAX);
  const char *name = basename(name_buf);
  if (strlen(name) > 255) {
    log_msg("returning ENAMETOOLONG");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENAMETOOLONG;
  }

  // start at the root directory's inode
  struct sfs_fs_inode directory;
  struct sfs_fs_inode file;
  if (sfs_dir_root(sfs_data->fs, &directory)) {
    log_msg("couldn't get root inode");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENOENT;  // technically "/" couldn't be found?
  }

  // TODO: navigate down the directory tree

  // search to see if `name` already exists in the directory
  uint64_t found_inumber = 0;
  void *iter = sfs_dir_iterate(sfs_data->fs, &directory);
  if (iter == NULL) {
    log_msg("sfs_dir_iterate failed");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }
  struct sfs_dir_entry *direntry;
  while ((iter = sfs_dir_iternext(iter, &direntry, NULL)) != NULL) {
    if (strncmp(direntry->name, name, 256) == 0) {
      found_inumber = direntry->inumber;

      if (sfs_dir_iterclose(iter)) {
        log_msg("sfs_dir_iterclose failed");
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -1;
      }

      // up the link count
      if (sfs_fs_read_inode(sfs_data->fs, found_inumber, &file)) {
        log_msg("error opening inode %" PRIu64, found_inumber);
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -1;
      }
      // TODO: check permissions and stuff here and revert if not OK
      ++file.links;
      file.change_time = time(NULL);
      if (sfs_fs_write_inode(sfs_data->fs, &file)) {
        log_msg("error writing inode %" PRIu64, found_inumber);
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -1;
      }

      struct sfs_fd *fd = sfs_filedescriptor_allocate(sfs_data->fd_pool);
      if (fd == NULL) {
        // should fail more gracefully
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -ENOMEM;
      }
      fd->inumber = file.inumber;
      fd->flags = fi->flags;
      log_msg("file_descriptor:");
      log_struct(fd, fd, "%d");
      log_struct(fd, inumber, "%" PRIu64);
      log_struct(fd, flags, "%" PRIu64);
      fi->fh = fd->fd;

      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return 0;
    }
  }

  SFS_UNLOCK_OR_FAIL(sfs_data, -1);
  return -ENOENT;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi) {
  DECL_SFS_DATA(sfs_data);
  SFS_LOCK_OR_FAIL(sfs_data, -1);

  log_msg("path=\"%s\", fi=%p", path, fi);

  // look up the filedescriptor data from the file handle number
  struct sfs_fd *fd = sfs_filedescriptor_get_from_fd(sfs_data->fd_pool, fi->fh);
  if (fd == NULL) {
    log_msg("invalid file descriptor");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }
  log_msg("file_descriptor:");
  log_struct(fd, fd, "%d");
  log_struct(fd, inumber, "%" PRIu64);
  log_struct(fd, flags, "%" PRIu64);

  // decrease the link count (deallocate inode if 0 links)
  struct sfs_fs_inode inode;
  log_msg("inumber %" PRIu64, fd->inumber);
  if (sfs_fs_read_inode(sfs_data->fs, fd->inumber, &inode)) {
    log_msg("error reading inode %" PRIu64, fd->inumber);
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }
  assert(inode.links > 0);
  --inode.links;
  inode.change_time = time(NULL);
  if (inode.links == 0) {
    if (sfs_fs_inode_deallocate(sfs_data->fs, &inode)) {
      log_msg("error deallocating inode %" PRIu64, fd->inumber);
      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return -1;
    }
  } else {
    if (sfs_fs_write_inode(sfs_data->fs, &inode)) {
      log_msg("error writing inode %" PRIu64, fd->inumber);
      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return -1;
    }
  }

  // return the filedescriptor to the pool
  sfs_filedescriptor_free(sfs_data->fd_pool, fd);

  SFS_UNLOCK_OR_FAIL(sfs_data, -1);
  return 0;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi) {
  DECL_SFS_DATA(sfs_data);
  SFS_LOCK_OR_FAIL(sfs_data, -1);

  log_msg("path=\"%s\", buf=%p, size=%zu, offset=%zd, fi=%p", path, buf, size,
          offset, fi);

  if (size == 0) {
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return 0;
  }

  struct sfs_fd *fd = sfs_filedescriptor_get_from_fd(sfs_data->fd_pool, fi->fh);
  if (fd == NULL) {
    log_msg("sfs_read() invalid filedescriptor %" PRIu64, fi->fh);
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }

  struct sfs_fs_inode inode;
  if (sfs_fs_read_inode(sfs_data->fs, fd->inumber, &inode)) {
    log_msg("sfs_read() error reading inode %" PRIu64, fd->inumber);
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }
  inode.access_time = time(NULL);
  if (sfs_fs_write_inode(sfs_data->fs, &inode)) {
    log_msg("sfs_read() error writing inode %" PRIu64, fd->inumber);
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }

  uint64_t first_block = offset / BLOCK_SIZE;
  uint64_t first_block_offset = offset % BLOCK_SIZE;
  uint64_t last_block_len = (offset + size) % BLOCK_SIZE;
  last_block_len = last_block_len == 0 ? BLOCK_SIZE : last_block_len;
  uint64_t last_block = (offset + size) / BLOCK_SIZE;
  last_block -= last_block_len == BLOCK_SIZE ? 1 : 0;

  log_msg(
      "first_block=%llu first_block_offset=%llu last_block_len=%llu "
      "last_block=%llu",
      first_block, first_block_offset, last_block_len, last_block);

  // adjust |buf| to be BLOCK_SIZE aligned
  buf -= first_block_offset;

  sfs_block_t tmp_block;
  for (uint64_t iblock = first_block; iblock <= last_block; ++iblock) {
    uint64_t slice_a = 0;
    uint64_t slice_b = BLOCK_SIZE;
    if (iblock == first_block && first_block_offset != 0) {
      slice_a = first_block_offset;
    }
    if (iblock == last_block && last_block_len != BLOCK_SIZE) {
      slice_b = last_block_len;
    }

    void *target = slice_a == 0 && slice_b == BLOCK_SIZE
                       ? (buf + iblock * BLOCK_SIZE)
                       : tmp_block;

    if (sfs_fs_inode_block_read(sfs_data->fs, &inode, iblock, target)) {
      log_msg("sfs_read() error reading iblock %" PRIu64 " from inode %" PRIu64,
              iblock, inode.inumber);
      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return -1;
    }

    if (slice_a == 0 && slice_b != BLOCK_SIZE) {
      memcpy(buf + slice_a, tmp_block + slice_a, slice_b - slice_a);
    }
  }

  SFS_UNLOCK_OR_FAIL(sfs_data, -1);
  return size;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
  DECL_SFS_DATA(sfs_data);
  SFS_LOCK_OR_FAIL(sfs_data, -1);

  log_msg("path=\"%s\", buf=%p, size=%zu, offset=%zd, fi=%p", path, buf, size,
          offset, fi);

  if (size == 0) {
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return 0;
  }

  struct sfs_fd *fd = sfs_filedescriptor_get_from_fd(sfs_data->fd_pool, fi->fh);
  if (fd == NULL) {
    log_msg("invalid filedescriptor %" PRIu64, fi->fh);
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }

  struct sfs_fs_inode inode;
  if (sfs_fs_read_inode(sfs_data->fs, fd->inumber, &inode)) {
    log_msg("error reading inode %" PRIu64, fd->inumber);
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }
  inode.access_time = time(NULL);
  if (offset + size > inode.size) {
    inode.change_time = time(NULL);
    inode.size = offset + size;
  }
  if (sfs_fs_write_inode(sfs_data->fs, &inode)) {
    log_msg("error writing inode %" PRIu64, fd->inumber);
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }

  uint64_t first_block = offset / BLOCK_SIZE;
  uint64_t first_block_offset = offset % BLOCK_SIZE;
  uint64_t last_block_len = (offset + size) % BLOCK_SIZE;
  last_block_len = last_block_len == 0 ? BLOCK_SIZE : last_block_len;
  uint64_t last_block = (offset + size) / BLOCK_SIZE;
  last_block -= last_block_len == BLOCK_SIZE ? 1 : 0;

  log_msg(
      "first_block=%llu first_block_offset=%llu last_block_len=%llu "
      "last_block=%llu",
      first_block, first_block_offset, last_block_len, last_block);

  // adjust |buf| to be BLOCK_SIZE aligned
  buf -= first_block_offset;

  sfs_block_t tmp_block;
  for (uint64_t iblock = first_block; iblock <= last_block; ++iblock) {
    uint64_t slice_a = 0;
    uint64_t slice_b = BLOCK_SIZE;
    if (iblock == first_block && first_block_offset != 0) {
      slice_a = first_block_offset;
    }
    if (iblock == last_block && last_block_len != BLOCK_SIZE) {
      slice_b = last_block_len;
    }

    const void *source = slice_a == 0 && slice_b == BLOCK_SIZE
                             ? (buf + iblock * BLOCK_SIZE)
                             : tmp_block;

    if (slice_a == 0 && slice_b != BLOCK_SIZE) {
      if (sfs_fs_inode_block_read(sfs_data->fs, &inode, iblock, tmp_block)) {
        log_msg("error reading iblock %" PRIu64 " from inode %" PRIu64, iblock,
                inode.inumber);
        SFS_UNLOCK_OR_FAIL(sfs_data, -1);
        return -1;
      }
      memcpy(tmp_block + slice_a, buf + slice_a, slice_b - slice_a);
    }

    if (sfs_fs_inode_block_write(sfs_data->fs, &inode, iblock, source)) {
      log_msg("error writing iblock %" PRIu64 " to inode %" PRIu64, iblock,
              inode.inumber);
      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return -1;
    }
  }

  SFS_UNLOCK_OR_FAIL(sfs_data, -1);
  return size;
}

/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode) {
  log_msg("path=\"%s\", mode=0%3o", path, mode);
  return 0;
}

/** Remove a directory */
int sfs_rmdir(const char *path) {
  log_msg("path=\"%s\"", path);

  if (strcmp(path, "/") == 0) {
    return -1;
  }

  return 0;
}

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi) {
  DECL_SFS_DATA(sfs_data);
  SFS_LOCK_OR_FAIL(sfs_data, -1);

  log_msg("path=\"%s\", fi=%p", path, fi);

  if (strcmp(path, "/") != 0) {
    log_msg("returning ENOENT");
    return -ENOENT;
  }

  // start at the root directory's inode
  struct sfs_fs_inode directory;

  // put root dir in &directory
  if (sfs_dir_root(sfs_data->fs, &directory)) {
    log_msg("couldn't get root inode");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENOENT;  // technically "/" couldn't be found?
  }

  // create linked list of tokens
  Token *head = createTokens(strdup(path));
  Token *currToken = head;

  // set up iterators to nav to directory
  void *iter = sfs_dir_iterate(sfs_data->fs, &directory);
  struct sfs_dir_entry *direntry;
  uint64_t found_inumber = 0;
  struct sfs_fs_inode entry;

  while ((iter = sfs_dir_iternext(iter, &direntry, &entry)) != NULL) {
    if (strcmp(direntry->name, currToken->token) == 0) {
      found_inumber = direntry->inumber;
      currToken = currToken->next;

      if (currToken == NULL) {
        // FOUND directory
        if (S_ISDIR(entry.mode)) {
        } else {
          log_msg("attempting open in fake directory");
          return -ENOENT;  // technically "/" couldn't be found?
        }

        // update the link count
        if (sfs_fs_read_inode(sfs_data->fs, found_inumber, &directory)) {
          log_msg("error opening inode %" PRIu64, found_inumber);
          SFS_UNLOCK_OR_FAIL(sfs_data, -1);
          return -1;
        }
        ++directory.links;
        directory.change_time = time(NULL);
        if (sfs_fs_write_inode(sfs_data->fs, &directory)) {
          log_msg("error writing inode %" PRIu64, found_inumber);
          SFS_UNLOCK_OR_FAIL(sfs_data, -1);
          return -1;
        }

        break;
      }

      // iterclose here

      directory = entry;

      iter = sfs_dir_iterate(sfs_data->fs, &directory);
    }
  }

  freeTokens(head);

  // allocate new file descriptor for our newly opened file
  struct sfs_fd *fd = sfs_filedescriptor_allocate(sfs_data->fd_pool);
  if (fd == NULL) {
    // should fail more gracefully
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -ENOMEM;
  }
  fd->inumber = directory.inumber;
  fd->flags = fi->flags;
  log_msg("file_descriptor:");
  log_struct(fd, fd, "%d");
  log_struct(fd, inumber, "%" PRIu64);
  log_struct(fd, flags, "%" PRIu64);
  fi->fh = fd->fd;

  SFS_UNLOCK_OR_FAIL(sfs_data, -1);

  return 0;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi) {
  DECL_SFS_DATA(sfs_data);
  SFS_LOCK_OR_FAIL(sfs_data, -1);

  log_msg("path=\"%s\", buf=%p, filler, offset=%zd, fi=%p", path, buf, offset,
          fi);

  if (strcmp(path, "/") != 0) {
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }

  struct sfs_fs_inode inode;
  if (sfs_dir_root(sfs_data->fs, &inode)) {
    log_msg("could not open root dir");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }

  // for root directory, hardcode dot and dotdot
  if (strcmp(path, "/") == 0) {
    if (filler(buf, ".", NULL, 0)) {
      log_msg("buffer full");
      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return 0;
    }
    if (filler(buf, "..", NULL, 0)) {
      log_msg("buffer full");
      SFS_UNLOCK_OR_FAIL(sfs_data, -1);
      return 0;
    }
  }

  struct stat st;
  struct sfs_fs_inode inode2;
  struct sfs_dir_entry *direntry;

  void *it = sfs_dir_iterate(sfs_data->fs, &inode);
  while ((it = sfs_dir_iternext(it, &direntry, &inode2)) != NULL) {
    sfs_fs_inode_to_stat(sfs_data->fs, &inode2, &st);
    if (filler(buf, direntry->name, &st, 0)) {
      log_msg("buffer full");
      break;
    }
  }

  SFS_UNLOCK_OR_FAIL(sfs_data, -1);
  return 0;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi) {
  DECL_SFS_DATA(sfs_data);
  SFS_LOCK_OR_FAIL(sfs_data, -1);

  log_msg("path=\"%s\", fi=%p", path, fi);

  // look up the filedescriptor data from the file handle number
  struct sfs_fd *fd = sfs_filedescriptor_get_from_fd(sfs_data->fd_pool, fi->fh);
  if (fd == NULL) {
    log_msg("invalid file descriptor");
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }
  log_msg("file_descriptor:");
  log_struct(fd, fd, "%d");
  log_struct(fd, inumber, "%" PRIu64);
  log_struct(fd, flags, "%" PRIu64);

  // decrease the link count (deallocate inode if 0 links)
  struct sfs_fs_inode inode;
  log_msg("inumber %" PRIu64, fd->inumber);
  if (sfs_fs_read_inode(sfs_data->fs, fd->inumber, &inode)) {
    log_msg("error reading inode %" PRIu64, fd->inumber);
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }

  --inode.links;
  inode.change_time = time(NULL);

  if (sfs_fs_write_inode(sfs_data->fs, &inode)) {
    log_msg("error writing inode %" PRIu64, fd->inumber);
    SFS_UNLOCK_OR_FAIL(sfs_data, -1);
    return -1;
  }

  // return the filedescriptor to the pool
  sfs_filedescriptor_free(sfs_data->fd_pool, fd);

  SFS_UNLOCK_OR_FAIL(sfs_data, -1);
  return 0;
}

struct fuse_operations sfs_oper = {.init = sfs_init,
                                   .destroy = sfs_destroy,

                                   .getattr = sfs_getattr,
                                   .create = sfs_create,
                                   .unlink = sfs_unlink,
                                   .open = sfs_open,
                                   .release = sfs_release,
                                   .read = sfs_read,
                                   .write = sfs_write,

                                   .rmdir = sfs_rmdir,
                                   .mkdir = sfs_mkdir,

                                   .opendir = sfs_opendir,
                                   .readdir = sfs_readdir,
                                   .releasedir = sfs_releasedir};

void sfs_usage() {
  fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
  exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
  int fuse_stat;
  struct sfs_state *sfs_data;

  // sanity checking on the command line
  if ((argc < 3) || (argv[argc - 2][0] == '-') || (argv[argc - 1][0] == '-'))
    sfs_usage();

  sfs_data = malloc(sizeof(struct sfs_state));
  if (sfs_data == NULL) {
    perror("main calloc");
    exit(EXIT_FAILURE);
  }

  // Pull the diskfile and save it in internal data
  sfs_data->diskfile = argv[argc - 2];
  argv[argc - 2] = argv[argc - 1];
  argv[argc - 1] = NULL;
  argc--;

  // do this before calling other initialization functions
  sfs_data->logfile = log_open();
  if (sfs_data->logfile < 0) {
    perror("log_open()");
    exit(EXIT_FAILURE);
  }

  if (pthread_mutex_init(&sfs_data->mu, NULL)) {
    perror("pthread_mutex_init()");
    exit(EXIT_FAILURE);
  }

  sfs_data->disk = open(sfs_data->diskfile, O_RDWR, S_IRUSR | S_IWUSR);
  if (sfs_data->disk < 0) {
    perror("open() error; file must exist and be preallocated");
    log_msg("opening file \"%s\" failed", sfs_data->diskfile);
    exit(EXIT_FAILURE);
  }

  // turn over control to fuse
  fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
  fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
  fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

  return fuse_stat;
}
