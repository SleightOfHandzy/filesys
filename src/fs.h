/**
 * describes the bytes in the flat file and how to load them into memory.
 * assumes word sizes and endianness are the same as on the system that made
 * the filesystem. pretty not portable.
 */

#ifndef _FS_H_
#define _FS_H_

#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

#include "block.h"

// magic number to identify proper superblock
#define SFS_FILE_TYPE_SIGNATURE "SFS_IS_THE_BEST"

/**
 * represents a superblock on disk
 *
 * the superblock is the first block in the diskfile
 */
struct sfs_fs_superblock {
  char signature[16];
  uint64_t create_time;  // `time()` when format was run

  uint64_t block_size;          // 512B blocks for sfs project
  uint64_t inode_table_blocks;  // how many consecutive blocks after the
                                // superblock are for inodes
  uint64_t inodes;              // number of inodes
  uint64_t blocks;

  uint64_t free_blocks_head;  // start block of the free blocks index
  uint64_t free_inode_head;   // inode number beginning the free inode list
};

#define SFS_NDIR_BLOCKS 12

#define SFS_IND_BLOCK SFS_NDIR_BLOCKS  // TODO: implement for directories
// number of blocks in indirect block's index (only 1 indirect block)
#define SFS_NIND_BLOCKS (1 * (BLOCK_SIZE / sizeof(uint64_t)))

#define SFS_DIND_BLOCK (SFS_IND_BLOCK + 1)  // TODO: implement

#define SFS_N_BLOCKS (SFS_DIND_BLOCK + 1)

/**
 * represents an inode on disk
 *
 * inode numbers are indices in inode table
 *
 * special inode numbers:
 * 0 => represents NULL inode
 * 1 => root directory inode
 */
struct sfs_fs_inode {
  uint64_t inumber;
  uint32_t mode;  // uses the same mode bits as chmod(2)

  uint32_t uid;
  uint32_t gid;
  uint32_t links;

  // all in seconds since UNIX epoch
  uint64_t access_time;
  uint64_t modified_time;
  uint64_t change_time;  // use change_time semantics

  // if inode is free, this is a pointer to the next free inode
  uint64_t size;

  // first pointers are direct, then indirect, then doubly indirect (see
  // SFS_NDIR_BLOCKS)
  uint64_t block_pointers[SFS_N_BLOCKS];
};

/**
 * opens |diskfile| for rw and if it is unformatted and |maybe_format| is true,
 * formats |diskfile| as an sfs filesystem
 *
 * returns opaque pointer representing the filesystem
 */
void* sfs_fs_open_disk(int disk, bool maybe_format);

/**
 * flushes buffers and closes the underlying filesystem
 */
int sfs_fs_close(void* fs);

/**
 * allocates a fresh inode from |fs|, writes its number to |inumber|, and
 * writes its data to |inode|
 *
 * returns 0 if OK, otherwise -1
 */
int sfs_fs_inode_allocate(void* fs, struct sfs_fs_inode* inode);

/**
 * deallocates |inode| and its blocks
 *
 * returns 0 if OK, otherwise -1
 */
int sfs_fs_inode_deallocate(void* fs, struct sfs_fs_inode* inode);

/**
 * reads inode |inumber| from |fs| and writes to |inode|
 *
 * returns 0 if OK, otherwise -1
 */
int sfs_fs_read_inode(void* fs, uint64_t inumber, struct sfs_fs_inode* inode);

/**
 * writes inode |inumber| to |fs| with contents of |inode|
 *
 * returns 0 if OK, otherwise -1
 */
int sfs_fs_write_inode(void* fs, const struct sfs_fs_inode* inode);

/**
 * reads |inode|'s metadata into |stat|
 */
void sfs_fs_inode_to_stat(void* fs, const struct sfs_fs_inode* inode,
                          struct stat* stat);

/**
 * gets the block number of the |iblock|th logical block in a file and returns
 * it
 */
uint64_t sfs_fs_inode_get_block_number(void* fs, struct sfs_fs_inode* inode,
                                       uint64_t iblock);

/**
 * for an |inode| in |fs|, read the logical block numbered |iblock| and write to
 * |block|
 *
 * returns 0 if OK, otherwise -1
 */
int sfs_fs_inode_block_read(void* fs, const struct sfs_fs_inode* inode,
                            uint64_t iblock, void* block);

/**
 * for an |inode| in |fs|, write to the logical block numbered |iblock| the data
 * in |block|. if the logical block does not exits, one will be allocated.
 *
 * returns 0 if OK, otherwise -1
 */
int sfs_fs_inode_block_write(void* fs, struct sfs_fs_inode* inode,
                             uint64_t iblock, const void* block);

/**
 * for an |inode| in |fs|, punch a hole in the logical file block |iblock|. if
 * that logical block didn't exist, consider action successful
 *
 * returns 0 if OK, otherwise -1
 */
int sfs_fs_inode_block_remove(void* fs, struct sfs_fs_inode* inode,
                              uint64_t iblock);

/**
 * finds a free block in |fs| and writes its block number to |block_number|
 *
 * returns 0 if OK, otherwise -1
 */
int sfs_fs_allocate_block(void* fs, uint64_t* block_number);

/**
 * returns |block_number|'s block to the pool of free blocks in |fs|
 *
 * returns 0 if OK, otherwise -1
 */
int sfs_fs_free_block(void* fs, uint64_t block_number);

#endif  // _FS_H_
