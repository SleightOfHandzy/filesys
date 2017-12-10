#include "fs.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "block.h"
#include "dir.h"
#include "log.h"

/**
 * write-back cache of one block of inodes
 */
struct inode_cache {
  bool dirty;
  int block_number;
  sfs_block_t data;
};

struct filesystem {
  int disk;
  struct sfs_fs_superblock superblock;
  struct inode_cache inode_cache;
};

static int write_superblock(int disk,
                            const struct sfs_fs_superblock* superblock) {
  log_msg("writing superblock");
  sfs_block_t tmp_block = {0};
  memcpy(tmp_block, superblock, sizeof(struct sfs_fs_superblock));
  if (block_write(disk, 0, tmp_block) != BLOCK_SIZE) {
    log_msg("error writing block");
    return -1;
  }
  return 0;
}

/**
 * formats an open |disk| as an sfs filesystem. writes initial data to |disk|
 * and puts a copy of the superblock in |superblock|
 */
static int format_fs(int disk, struct sfs_fs_superblock* superblock) {
  assert(disk >= 0);
  assert(superblock != NULL);

  log_msg("formatting filesystem");

  // get the size of the disk
  struct stat st;
  if (fstat(disk, &st) < 0) {
    perror("fstat() error");
    log_msg("format_fs() fstat failure");
    return -1;
  }
  off_t disk_size = st.st_size;
  uint64_t blocks = disk_size / BLOCK_SIZE;
  if (blocks < 3) {
    fprintf(stderr, "disk file too small to use as filesystem\n");
    log_msg("disk had only %" PRIu64 " blocks", blocks);
    return -1;
  }

  log_msg("partitioning %zd bytes into %" PRIu64 " blocks", disk_size, blocks);

  memcpy(&superblock->signature, SFS_FILE_TYPE_SIGNATURE,
         sizeof(SFS_FILE_TYPE_SIGNATURE));
  superblock->create_time = time(NULL);
  superblock->block_size = BLOCK_SIZE;
  // use 6.25% of space for inodes or 1 block, whatever
  superblock->inode_table_blocks = (blocks - 1) / 16;
  if (superblock->inode_table_blocks == 0) {
    superblock->inode_table_blocks = 1;
  }
  uint64_t inodes_per_block = BLOCK_SIZE / sizeof(struct sfs_fs_inode);
  superblock->inodes = superblock->inode_table_blocks * inodes_per_block;
  superblock->blocks = blocks;
  superblock->free_blocks_head = 2 + superblock->inode_table_blocks;
  superblock->free_inode_head = 2;

  log_msg("%" PRIu64 " blocks for inodes (%" PRIu64 " inodes)",
          superblock->inode_table_blocks, superblock->inodes);

  log_msg("zeroing inode table blocks");
  sfs_block_t tmp_block = {0};
  uint64_t next_free = 3;  // follow the white rabbit on this rh
  for (uint64_t i = 1; i < superblock->inode_table_blocks + 1; ++i) {
    struct sfs_fs_inode* inode_arr = (struct sfs_fs_inode*)tmp_block;
    for (uint64_t j = 0; j < inodes_per_block; ++j) {
      if (i == 1 && j == 0) {
        // initialize root inode
        // is a directory, with rwx on ugo
        inode_arr->inumber = 1;
        inode_arr->mode =
            S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
        // set uid and gid to the current user (for convenience)
        inode_arr->uid = getuid();
        inode_arr->gid = getgid();
        // always have 1 link on root (convention it seems)
        inode_arr->links = 1;
        inode_arr->access_time = inode_arr->change_time =
            inode_arr->modified_time = time(NULL);
        inode_arr->size = 0;
      } else {
        memset(inode_arr + j, 0, sizeof(struct sfs_fs_inode));
        inode_arr[j].inumber = next_free - 1;
        inode_arr[j].size = next_free++;
      }
    }

    if (block_write(disk, i, tmp_block) != BLOCK_SIZE) {
      fprintf(stderr, "error initializing inodes\n");
      log_msg("error initializing inode block %" PRIu64, i);
      return -1;
    }
  }

  // here's how this calculation goes:
  // if there is space in a block for N block pointers, there is space for
  // describing N-1 free blocks and myself (from some other index).
  // the following code is spaghetti and i apologize.
  uint64_t free_blocks = superblock->blocks - superblock->free_blocks_head;
  uint64_t slots_per_block = BLOCK_SIZE / sizeof(uint64_t) - 1;
  uint64_t first_free_block =
      superblock->free_blocks_head + free_blocks -
      (free_blocks * slots_per_block / (slots_per_block + 1));
  log_msg("writing free space index");
  uint64_t cur_index_block = superblock->free_blocks_head;
  uint64_t cur_index_block_pos = 0;
  for (uint64_t i = first_free_block; i < superblock->blocks; ++i) {
    if (cur_index_block_pos == BLOCK_SIZE / sizeof(uint64_t)) {
      cur_index_block++;
      cur_index_block_pos = 0;
    }

    assert(cur_index_block < first_free_block);
    uint64_t* free_index = (uint64_t*)tmp_block;

    if (cur_index_block_pos == 0) {
      // point to cur_index_block+1
      if (cur_index_block < first_free_block - 1) {
        free_index[0] = cur_index_block + 1;
      } else {
        // end of list right before first_free_block
        free_index[0] = 0;
      }
      cur_index_block_pos++;
    }
    free_index[cur_index_block_pos++] = i;

    // flush here and advance at the next loop
    // (advance at the next loop for the sole purpose of the next assert)
    if (cur_index_block_pos == BLOCK_SIZE / sizeof(uint64_t)) {
      if (block_write(disk, cur_index_block, tmp_block) != BLOCK_SIZE) {
        fprintf(stderr, "error initializing free blocks index\n");
        log_msg("error initializing free block index %" PRIu64,
                cur_index_block);
        return -1;
      }
    }
  }
  // this assertion makes sure we don't somehow leak blocks
  assert(cur_index_block == first_free_block - 1);

  if (write_superblock(disk, superblock)) {
    fprintf(stderr, "error writing superblock\n");
    return -1;
  }

  return 0;
}

void* sfs_fs_open_disk(int disk, bool maybe_format) {
  assert(disk >= 0);

  // allocate in memory filesystem representation
  struct filesystem* fs = malloc(sizeof(struct filesystem));
  if (fs == NULL) {
    perror("malloc() failure");
    log_msg("malloc failure");
    return NULL;
  }
  // mark unsetup field values
  fs->disk = disk;
  fs->inode_cache.block_number = 0;

  // read the superblock data
  sfs_block_t superblock_data;
  if (block_read(fs->disk, 0, superblock_data) != BLOCK_SIZE) {
    perror("block_read() error; couldn't read superblock");
    log_msg("couldn't read superblock");
    free(fs);
    return NULL;
  }

  // check the singature on the superblock
  // if it matches, we can probably assume the filesystem is valid
  // if it doesn't match and |maybe_format| is set, create a new filesystem
  struct sfs_fs_superblock* superblock =
      (struct sfs_fs_superblock*)superblock_data;
  int signature_cmp = memcmp(superblock->signature, SFS_FILE_TYPE_SIGNATURE,
                             sizeof(SFS_FILE_TYPE_SIGNATURE));
  if (signature_cmp != 0) {
    if (!maybe_format) {
      log_msg(
          "sfs_fs_open_disk() disk was unformatted and maybe_format is "
          "false");
    }
    if (format_fs(fs->disk, &fs->superblock) != 0) {
      free(fs);
      return NULL;
    }
  } else {
    fs->superblock = *superblock;
  }

  time_t create_time = (time_t)superblock->create_time;
  // asctime() has '\n' at the end
  char* t = asctime(localtime(&create_time));
  t[strlen(t)-1] = '\0';
  log_msg("sfs_fs_open_disk() opened fs created at %s", t);

  return fs;
}

int sfs_fs_close(void* arg) {
  struct filesystem* fs = (struct filesystem*)arg;
  assert(fs != NULL);
  assert(fs->disk >= 0);

  if (fs->inode_cache.dirty) {
    if (block_write(fs->disk, fs->inode_cache.block_number,
                    fs->inode_cache.data) != BLOCK_SIZE) {
      log_msg("block_write() write-back failed");
      return -1;
    }
  }

  if (write_superblock(fs->disk, &fs->superblock)) {
    log_msg("failed to write superblock");
  }

  free(fs);
  return 0;
}

int sfs_fs_inode_allocate(void* arg, struct sfs_fs_inode* inode) {
  struct filesystem* fs = (struct filesystem*)arg;
  assert(fs != NULL);
  assert(fs->disk >= 0);
  assert(inode != NULL);

  uint64_t inumber = fs->superblock.free_inode_head;
  if (inumber == 0) {
    log_msg("out of free inodes");
    return -1;
  }
  if (sfs_fs_read_inode(fs, inumber, inode)) {
    log_msg("could not read allocated inode");
    return -1;
  }
  // hide next pointer in `size` member
  fs->superblock.free_inode_head = inode->size;
  if (write_superblock(fs->disk, &fs->superblock)) {
    log_msg("could not write superblock");
    return -1;
  }

  return 0;
}

int sfs_fs_inode_deallocate(void* arg, struct sfs_fs_inode* inode) {
  struct filesystem* fs = (struct filesystem*)arg;
  assert(fs != NULL);
  assert(fs->disk >= 0);
  assert(inode != NULL);

  // hide next pointer in `size` member
  inode->size = fs->superblock.free_inode_head;
  if (sfs_fs_write_inode(fs, inode)) {
    log_msg("could not write allocated inode");
    return -1;
  }
  fs->superblock.free_inode_head = inode->inumber;
  if (write_superblock(fs->disk, &fs->superblock)) {
    log_msg("could not write superblock");
    return -1;
  }

  for (int i = 0; i < SFS_NDIR_BLOCKS; ++i) {
    if (inode->block_pointers[i] != 0) {
      if (sfs_fs_free_block(fs, inode->block_pointers[i])) {
        log_msg("error freeing block %" PRIu64, inode->block_pointers[i]);
        return -1;
      }
    }
  }

  return 0;
}

int sfs_fs_read_inode(void* arg, uint64_t inumber, struct sfs_fs_inode* inode) {
  struct filesystem* fs = (struct filesystem*)arg;
  assert(fs != NULL);
  assert(fs->disk >= 0);
  assert(inumber > 0);  // 0 represents a NULL inode
  assert(inode != NULL);

  uint64_t inumber_index = inumber - 1;
  uint64_t inodes_per_block = BLOCK_SIZE / sizeof(struct sfs_fs_inode);
  // superblock is the first block, inodes start at block index 1
  uint64_t block_number = inumber_index / inodes_per_block + 1;
  uint64_t position_in_block = inumber_index % inodes_per_block;
  if (fs->inode_cache.block_number != block_number) {
    if (fs->inode_cache.dirty) {
      if (block_write(fs->disk, fs->inode_cache.block_number,
                      fs->inode_cache.data) != BLOCK_SIZE) {
        log_msg("write-back failed: %s", strerror(errno));
        return -1;
      }
    }

    fs->inode_cache.block_number = block_number;
    fs->inode_cache.dirty = false;
    if (block_read(fs->disk, block_number, fs->inode_cache.data) !=
        BLOCK_SIZE) {
      log_msg("block_read failed: %s", strerror(errno));
      return -1;
    }
  }

  *inode = ((struct sfs_fs_inode*)fs->inode_cache.data)[position_in_block];
  return 0;
}

int sfs_fs_write_inode(void* arg, const struct sfs_fs_inode* inode) {
  struct filesystem* fs = (struct filesystem*)arg;
  assert(fs != NULL);
  assert(fs->disk >= 0);
  assert(inode != NULL);
  assert(inode->inumber > 0);  // 0 represents a NULL inode

  uint64_t inumber_index = inode->inumber - 1;
  uint64_t inodes_per_block = BLOCK_SIZE / sizeof(struct sfs_fs_inode);
  // superblock is the first block, inodes start at block index 1
  uint64_t block_number = inumber_index / inodes_per_block + 1;
  uint64_t position_in_block = inumber_index % inodes_per_block;
  if (fs->inode_cache.block_number != block_number) {
    if (fs->inode_cache.dirty) {
      if (block_write(fs->disk, fs->inode_cache.block_number,
                      fs->inode_cache.data) != BLOCK_SIZE) {
        log_msg("write-back failed: %s", strerror(errno));
        return -1;
      }
    }

    fs->inode_cache.block_number = block_number;
    if (block_read(fs->disk, block_number, fs->inode_cache.data) !=
        BLOCK_SIZE) {
      log_msg("block_read failed: %s", strerror(errno));
      return -1;
    }
  }
  fs->inode_cache.dirty = true;

  ((struct sfs_fs_inode*)fs->inode_cache.data)[position_in_block] = *inode;
  return 0;
}

void sfs_fs_inode_to_stat(void* arg, const struct sfs_fs_inode* inode,
                          struct stat* st) {
  struct filesystem* fs = (struct filesystem*)arg;
  assert(fs != NULL);
  assert(inode != NULL);
  assert(st != NULL);

  st->st_ino = inode->inumber;
  st->st_mode = inode->mode;
  st->st_nlink = inode->links;
  st->st_uid = inode->uid;
  st->st_gid = inode->gid;
  st->st_atime = inode->access_time;
  st->st_mtime = inode->modified_time;
  st->st_ctime = inode->change_time;
  st->st_size = inode->size;
}

uint64_t sfs_fs_inode_get_block_number(void* fs, struct sfs_fs_inode* inode,
                                       uint64_t iblock) {
  assert(fs != NULL);
  assert(inode != NULL);

  if (iblock >= SFS_NDIR_BLOCKS) {
    log_msg(
        "sfs_fs_inode_get_block_number() indirection not yet implemented");
    return 0;
  }

  return inode->block_pointers[iblock];
}

int sfs_fs_inode_block_read(void* arg, const struct sfs_fs_inode* inode,
                            uint64_t iblock, void* block) {
  struct filesystem* fs = (struct filesystem*)arg;
  assert(fs != NULL);
  assert(fs->disk >= 0);
  assert(inode != NULL);
  assert(block != NULL);

  if (iblock >= SFS_NDIR_BLOCKS) {
    log_msg("indirection not yet implemented");
    return -1;
  }

  uint64_t block_number = inode->block_pointers[iblock];
  if (block_number == 0) {
    memset(block, 0, BLOCK_SIZE);
    return 0;
  }

  if (block_number < fs->superblock.inode_table_blocks + 1 ||
      block_number >= fs->superblock.blocks) {
    log_msg(
        "block INSIDE inode outside range? "
        "(iblock=%" PRIu64 ", block_number=%" PRIu64 ") (range is %" PRIu64
        " to %" PRIu64 ")",
        iblock, block_number, fs->superblock.inode_table_blocks + 1,
        fs->superblock.blocks);
    return -1;
  }

  if (block_read(fs->disk, block_number, block) != BLOCK_SIZE) {
    log_msg("unable to read block %" PRIu64 ": %s", block_number,
            strerror(errno));
    return -1;
  }

  return 0;
}

int sfs_fs_inode_block_write(void* arg, struct sfs_fs_inode* inode,
                             uint64_t iblock, const void* block) {
  struct filesystem* fs = (struct filesystem*)arg;
  assert(fs != NULL);
  assert(fs->disk >= 0);
  assert(inode != NULL);
  assert(block != NULL);

  if (iblock >= SFS_NDIR_BLOCKS) {
    log_msg("indirection not yet implemented");
    return -1;
  }

  uint64_t block_number = inode->block_pointers[iblock];
  if (block_number == 0) {
    if (sfs_fs_allocate_block(fs, &block_number)) {
      log_msg("could not allocate block");
      return -1;
    }
    inode->block_pointers[iblock] = block_number;
    if (sfs_fs_write_inode(fs, inode)) {
      log_msg("could not update inode");
      return -1;
    }
  }

  if (block_number < fs->superblock.inode_table_blocks + 1 ||
      block_number >= fs->superblock.blocks) {
    log_msg(
        "block INSIDE inode outside range? "
        "(iblock=%" PRIu64 ", block_number=%" PRIu64 ") (range is %" PRIu64
        " to %" PRIu64 ")",
        iblock, block_number, fs->superblock.inode_table_blocks + 1,
        fs->superblock.blocks);
    return -1;
  }

  if (block_write(fs->disk, block_number, block) != BLOCK_SIZE) {
    log_msg("error writing block %" PRIu64 ": %s", block_number,
            strerror(errno));
    return -1;
  }

  return 0;
}

int sfs_fs_inode_block_remove(void* arg, struct sfs_fs_inode* inode,
                              uint64_t iblock) {
  struct filesystem* fs = (struct filesystem*)arg;
  assert(fs != NULL);
  assert(fs->disk >= 0);
  assert(inode != NULL);

  if (iblock >= SFS_NDIR_BLOCKS) {
    log_msg("indirection not yet implemented");
    return -1;
  }

  if (inode->block_pointers[iblock] == 0) {
    return 0;
  }

  if (sfs_fs_free_block(fs, inode->block_pointers[iblock])) {
    log_msg("sfs_fs_inode_block_remove() error freeing logical block %" PRIu64,
            iblock);
    return -1;
  }

  return 0;
}

int sfs_fs_allocate_block(void* arg, uint64_t* block_number) {
  struct filesystem* fs = (struct filesystem*)arg;
  assert(fs != NULL);
  assert(fs->disk >= 0);

  if (fs->superblock.free_blocks_head == 0) {
    log_msg("failed; no free blocks available");
    return -1;
  }

  uint64_t node = fs->superblock.free_blocks_head;
  uint64_t found_free_block = 0;
  sfs_block_t tmp_block;

  if (block_read(fs->disk, node, tmp_block) != BLOCK_SIZE) {
    log_msg("error reading block %" PRIu64, node);
    return -1;
  }

  // search through the index node for a free block number
  uint64_t* index = (uint64_t*)tmp_block;
  for (uint64_t i = 1; i < BLOCK_SIZE / sizeof(uint64_t); ++i) {
    if (index[i] != 0) {
      found_free_block = index[i];
      index[i] = 0;
      break;
    }
  }

  if (found_free_block) {
    // write the zeroed slot to the index
    if (block_write(fs->disk, node, tmp_block) != BLOCK_SIZE) {
      log_msg("error writing block %" PRIu64, node);
      return -1;
    }
  } else {
    // if there are no free blocks in this index node, use the index node as
    // the free block
    // we can assume this node's previous node was the superblock
    found_free_block = node;
    fs->superblock.free_blocks_head = index[0];
    if (write_superblock(fs->disk, &fs->superblock)) {
      log_msg("error writing superblock");
      return -1;
    }
  }

  *block_number = found_free_block;
  return 0;
}

int sfs_fs_free_block(void* arg, uint64_t block_number) {
  struct filesystem* fs = (struct filesystem*)arg;
  assert(fs != NULL);
  assert(fs->disk >= 0);

  sfs_block_t tmp_block = {0};
  uint64_t* index = (uint64_t*)tmp_block;

  // case where there are no other nodes
  if (fs->superblock.free_blocks_head == 0) {
    if (block_write(fs->disk, block_number, tmp_block) != BLOCK_SIZE) {
      log_msg("error zeroing block %" PRIu64, block_number);
      return -1;
    }
    fs->superblock.free_blocks_head = block_number;
    if (write_superblock(fs->disk, &fs->superblock)) {
      log_msg("error writing superblock");
      return -1;
    }
    return 0;
  }

  uint64_t node = fs->superblock.free_blocks_head;
  uint64_t prev_node = 0;
  while (node != 0) {
    if (block_read(fs->disk, node, tmp_block) != BLOCK_SIZE) {
      log_msg("error reading block %" PRIu64, node);
      return -1;
    }

    // search through the index node for a free slot
    for (uint64_t i = 1; i < BLOCK_SIZE / sizeof(uint64_t); ++i) {
      if (index[i] == 0) {
        index[i] = block_number;
        if (block_write(fs->disk, node, tmp_block) != BLOCK_SIZE) {
          log_msg("error writing block %" PRIu64, node);
          return -1;
        }
        return 0;
      }
    }

    prev_node = node;
    node = index[0];
  }

  // add |block_number| as a node at the end of the list
  index[0] = block_number;
  if (block_write(fs->disk, prev_node, tmp_block) != BLOCK_SIZE) {
    log_msg("error writing block %" PRIu64, prev_node);
    return -1;
  }

  // zero the block of |block_number|
  memset(tmp_block, 0, BLOCK_SIZE);
  if (block_write(fs->disk, block_number, tmp_block) != BLOCK_SIZE) {
    log_msg("error zeroing block %" PRIu64, block_number);
    return -1;
  }

  return 0;
}
