/**
 * describes and allocates file descriptors with slab allocator
 *
 * right now, nothing here is threadsafe because we're running fuse in single-
 * threaded/evented mode
 */

#ifndef _FILEDESCRIPTOR_H_
#define _FILEDESCRIPTOR_H_

struct sfs_fd {
  int fd;
};

/**
 * initializes first slab
 *
 * returns 0 on success, -1 on failure
 */
int sfs_filedescriptor_init();

/**
 * frees used memory by filedescriptors
 */
void sfs_filedescriptor_deinit();

/**
 * gets a unique filedescriptor for use in sfs
 */
struct sfs_fd* sfs_filedescriptor_allocate();

/**
 * takes as input a file descriptor number and returns the data it represents
 *
 * returns NULL if |fd| is an invalid filedescriptor
 */
struct sfs_fd* sfs_filedescriptor_get_from_fd(int fd);

/**
 * returns a filedescriptor to the pool of available filedescriptors
 */
void sfs_filedescriptor_free(struct sfs_fd* fd);

#endif  // _FILEDESCRIPTOR_H_
