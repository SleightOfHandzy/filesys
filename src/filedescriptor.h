/**
 * describes and allocates file descriptors with slab allocator
 *
 * right now, nothing here is threadsafe because we're running fuse in single-
 * threaded/evented mode
 */

#ifndef _FILEDESCRIPTOR_H_
#define _FILEDESCRIPTOR_H_

#include <stdint.h>

struct sfs_fd {
  int fd;
  uint64_t inumber;
  uint64_t flags;
};

/**
 * initializes a filedescriptor pool
 *
 * returns opaque pointer to filedescriptor pool on success, NULL on failure
 */
void* sfs_filedescriptor_pool_init();

/**
 * frees memory used by filedescriptors
 */
void sfs_filedescriptor_pool_deinit(void* pool);

/**
 * gets a unique filedescriptor for use in sfs
 */
struct sfs_fd* sfs_filedescriptor_allocate(void* pool);

/**
 * takes as input a file descriptor number and returns the data it represents
 *
 * returns NULL if |fd| is an invalid filedescriptor
 */
struct sfs_fd* sfs_filedescriptor_get_from_fd(void* pool, int fd);

/**
 * returns a filedescriptor to the pool of available filedescriptors
 */
void sfs_filedescriptor_free(void* pool, struct sfs_fd* fd);

#endif  // _FILEDESCRIPTOR_H_
