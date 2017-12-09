#ifndef _DIR_H_
#define _DIR_H_

#include "fs.h"

struct sfs_dir_entry {
  uint64_t inumber;
  char name[256];
};

/**
 * writes the inode of the root directory of |fs| into |inode|
 */
int sfs_dir_root(void* fs, struct sfs_fs_inode* inode);

/**
 * creates an iterator to the |directory| in |fs|
 *
 * the iterator is only valid as long as |directory| is valid
 */
void* sfs_dir_iterate(void* fs, struct sfs_fs_inode* directory);

/**
 * advances |iterator| and writes to |direntry| a valid pointer to the next
 * direntry
 *
 * returns NULL on failure or end of iteration (NULL return means
 * `sfs_dir_iterclose()` is unnecessary)
 */
void* sfs_dir_iternext(void* iterator, struct sfs_dir_entry** direntry,
                       struct sfs_fs_inode* inode);

/**
 * unlinks |direntry| during directory iteration. must only be called on the
 * most recent `sfs_dir_entry` gotten from `sfs_dir_iternext`.
 *
 * NOTE: if the link count drops to 0, this function WILL deallocate the
 * underlying inode
 */
int sfs_dir_iter_unlink(void* iterator, struct sfs_dir_entry* direntry);

/**
 * close |iterator| before it is exhausted by `sfs_dir_iternext()`
 */
int sfs_dir_iterclose(void* iterator);

/**
 * adds entry |name| in |directory| pointing to |inumber|'s |inode|
 */
int sfs_dir_link(void* fs, struct sfs_fs_inode* directory, const char* name,
                 struct sfs_fs_inode* inode);

#endif  // _DIR_H_
