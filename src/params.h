/*
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.

  There are a couple of symbols that need to be #defined before
  #including all the headers.
*/

#ifndef _PARAMS_H_
#define _PARAMS_H_

#include <assert.h>
#include <pthread.h>

// The FUSE API has been changed a number of times.  So, our code
// needs to define the version of the API that we assume.  As of this
// writing, the most current API version is 26
// NOTE: moved to Makefile.am
// #define FUSE_USE_VERSION 26

// need this to get pwrite().  I have to use setvbuf() instead of
// setlinebuf() later in consequence.
// NOTE: moved to Makefile.am
// #define _XOPEN_SOURCE 500

// maintain bbfs state in here
#include <limits.h>
#include <stdio.h>
struct sfs_state {
  pthread_mutex_t mu;
  FILE* logfile;
  int disk;
  const char* diskfile;
  void* fd_pool;
  void* fs;
};

#define SFS_DATA ((struct sfs_state*)fuse_get_context()->private_data)
#define DECL_SFS_DATA(name) \
  struct sfs_state* name = (struct sfs_state*)fuse_get_context()->private_data
#define SFS_LOCK_OR_FAIL(name, ret)               \
  do {                                            \
    int mu_ret = pthread_mutex_lock(&(name)->mu); \
    assert(mu_ret == 0);                          \
    if (mu_ret) return ret;                       \
  } while (0);
#define SFS_UNLOCK_OR_FAIL(name, ret)               \
  do {                                              \
    int mu_ret = pthread_mutex_unlock(&(name)->mu); \
    assert(mu_ret == 0);                            \
    if (mu_ret) return ret;                         \
  } while (0);

#define FUSE_CALLER_UID (fuse_get_context()->uid)
#define FUSE_CALLER_GID (fuse_get_context()->gid)
#define FUSE_CALLER_UMASK (fuse_get_context()->umask)

#endif
