#include "dir.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "log.h"

int sfs_dir_root(void* fs, struct sfs_fs_inode* inode) {
  assert(fs != NULL);
  assert(inode != NULL);

  if (sfs_fs_read_inode(fs, 1, inode)) {
    log_msg("error reading root directory inode");
    return -1;
  }

  return 0;
}

struct dir_iterator {
  void* fs;
  struct sfs_fs_inode* inode;
  uint64_t iblock;
  uint64_t entry;

  sfs_block_t cached_block;
};

void* sfs_dir_iterate(void* fs, struct sfs_fs_inode* inode) {
  assert(fs != NULL);
  assert(inode != NULL);
  assert((inode->mode & S_IFDIR) != 0);

  struct dir_iterator* it =
      (struct dir_iterator*)malloc(sizeof(struct dir_iterator));
  if (it == NULL) {
    log_msg("sfs_dir_iterate() malloc error");
    return NULL;
  }

  it->fs = fs;
  it->inode = inode;
  it->iblock = 0;
  it->entry = 0;

  return it;
}

void* sfs_dir_iternext(void* arg, struct sfs_dir_entry** direntry,
                       struct sfs_fs_inode* inode) {
  struct dir_iterator* it = (struct dir_iterator*)arg;
  assert(it != NULL);
  assert(it->fs != NULL);
  assert(it->inode != NULL);

  struct sfs_dir_entry* arr = (struct sfs_dir_entry*)it->cached_block;
  while (true) {
    if (it->iblock * BLOCK_SIZE >= it->inode->size) {
      goto end;
    }

    if (it->entry == 0) {
      if (sfs_fs_inode_block_read(it->fs, it->inode, it->iblock,
                                  it->cached_block)) {
        log_msg("unable to read block %" PRIu64, it->iblock);
        goto end;
      }
      it->inode->access_time = time(NULL);
      if (sfs_fs_write_inode(it->fs, it->inode)) {
        log_msg("unable write inode %" PRIu64, it->inode->inumber);
        goto end;
      }
    }

    while (it->entry < BLOCK_SIZE / sizeof(struct sfs_dir_entry)) {
      if (arr[it->entry].inumber != 0) {
        *direntry = arr + it->entry;
        // write to the output inode if provided
        if (inode != NULL) {
          if (sfs_fs_read_inode(it->fs, arr[it->entry].inumber, inode)) {
            log_msg("error reading directory entry inode");
            goto end;
          }
        }
        ++it->entry;
        return it;
      }
      ++it->entry;
    }

    ++it->iblock;
    it->entry = 0;
  }

end:
  free(it);
  return NULL;
}

// TODO: fix leakiness of directories
int sfs_dir_iter_unlink(void* arg, struct sfs_dir_entry* direntry) {
  struct dir_iterator* it = (struct dir_iterator*)arg;
  assert(it != NULL);
  assert(it->fs != NULL);
  assert(it->inode != NULL);
  assert(direntry != NULL);

  struct sfs_fs_inode inode;
  if (sfs_fs_read_inode(it->fs, direntry->inumber, &inode)) {
    log_msg("error reading inode %" PRIu64, inode.inumber);
    return -1;
  }
  --inode.links;
  inode.change_time = time(NULL);
  if (inode.links == 0) {
    if (sfs_fs_inode_deallocate(it->fs, &inode)) {
      log_msg("error deallocating inode %" PRIu64, inode.inumber);
      return -1;
    }
  } else {
    if (sfs_fs_write_inode(it->fs, &inode)) {
      log_msg("error writing inode %" PRIu64, inode.inumber);
      return -1;
    }
  }

  // `direntry` is in the `cached_block`
  direntry->inumber = 0;
  it->inode->modified_time = time(NULL);
  if (sfs_fs_write_inode(it->fs, it->inode)) {
    log_msg("error writing inode %" PRIu64, inode.inumber);
    return -1;
  }
  if (sfs_fs_inode_block_write(it->fs, it->inode, it->iblock,
                               it->cached_block)) {
    log_msg("error writing inode %" PRIu64, inode.inumber);
    return -1;
  }

  return 0;
}

int sfs_dir_iterclose(void* iterator) {
  assert(iterator != NULL);
  free(iterator);
  return 0;
}

int sfs_dir_link(void* fs, struct sfs_fs_inode* directory, const char* name,
                 struct sfs_fs_inode* inode) {
  assert(fs != NULL);
  assert(directory != NULL);
  assert(name != NULL);
  assert(inode != NULL);

  if (strlen(name) > 255) {
    log_msg("name too long");
    return -1;
  }

  sfs_block_t tmp_block;
  struct sfs_dir_entry* arr = (struct sfs_dir_entry*)tmp_block;
  for (uint64_t i = 0; i < directory->size / BLOCK_SIZE; ++i) {
    if (sfs_fs_inode_block_read(fs, directory, i, tmp_block)) {
      log_msg("error reading directory block %" PRIu64, i);
      return -1;
    }
    for (uint64_t j = 0; j < BLOCK_SIZE / sizeof(struct sfs_dir_entry); ++j) {
      if (arr[j].inumber == 0) {
        arr[j].inumber = inode->inumber;
        strncpy(arr[j].name, name, 256);
        if (sfs_fs_inode_block_write(fs, directory, i, tmp_block)) {
          log_msg("error writing directory block %" PRIu64, i);
          return -1;
        }
        ++inode->links;
        directory->modified_time = inode->change_time = time(NULL);
        if (sfs_fs_write_inode(fs, directory)) {
          log_msg("error updating mtime on directory");
          return -1;
        }
        if (sfs_fs_write_inode(fs, inode)) {
          log_msg("error updating links on inode");
          return -1;
        }
        return 0;
      }
    }
  }

  memset(tmp_block, 0, BLOCK_SIZE);
  arr[0].inumber = inode->inumber;
  strncpy(arr[0].name, name, 256);
  if (sfs_fs_inode_block_write(fs, directory, directory->size / BLOCK_SIZE,
                               tmp_block)) {
    log_msg("error adding directory block %" PRIu64,
            directory->size / BLOCK_SIZE + 1);
    return -1;
  }
  directory->size += BLOCK_SIZE;
  directory->modified_time = time(NULL);
  if (sfs_fs_write_inode(fs, directory)) {
    log_msg("error updating directory");
    return -1;
  }

  ++inode->links;
  inode->change_time = time(NULL);
  if (sfs_fs_write_inode(fs, inode)) {
    log_msg("error updating links on inode");
    return -1;
  }

  return 0;
}
