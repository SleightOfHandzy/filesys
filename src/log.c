/*
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.

  Since the point of this filesystem is to learn FUSE and its
  datastructures, I want to see *everything* that happens related to
  its data structures.  This file contains macros and functions to
  accomplish this.
*/

#include "params.h"

#include <fuse.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "log.h"

FILE *log_open() {
  FILE *logfile;

  // very first thing, open up the logfile and mark that we got in
  // here.  If we can't open the logfile, we're dead.
  logfile = fopen("sfs.log", "w");
  if (logfile == NULL) {
    perror("logfile");
    exit(EXIT_FAILURE);
  }

  // set logfile to line buffering
  setvbuf(logfile, NULL, _IOLBF, 0);

  return logfile;
}

// fuse context
void log_fuse_context_impl(LOG_CONTEXT_PARAMS, struct fuse_context *context) {
  log_msg_impl("struct fuse_context:", LOG_CONTEXT_ARGS);

  /** Pointer to the fuse object */
  //	struct fuse *fuse;
  log_struct(context, fuse, "%p");

  /** User ID of the calling process */
  //	uid_t uid;
  log_struct(context, uid, "%d");

  /** Group ID of the calling process */
  //	gid_t gid;
  log_struct(context, gid, "%d");

  /** Thread ID of the calling process */
  //	pid_t pid;
  log_struct(context, pid, "%d");

  /** Private filesystem data */
  //	void *private_data;
  log_struct(context, private_data, "%p");
  log_struct(((struct sfs_state *)context->private_data), logfile, "%p");

  /** Umask of the calling process (introduced in version 2.8) */
  //	mode_t umask;
  log_struct(context, umask, "%05o");
}

// struct fuse_conn_info contains information about the socket
// connection being used.  I don't actually use any of this
// information in sfs
void log_conn_impl(LOG_CONTEXT_PARAMS, struct fuse_conn_info *conn) {
  log_msg_impl("struct fuse_conn_info:", LOG_CONTEXT_ARGS);

  /** Major version of the protocol (read-only) */
  // unsigned proto_major;
  log_struct(conn, proto_major, "%u");

  /** Minor version of the protocol (read-only) */
  // unsigned proto_minor;
  log_struct(conn, proto_minor, "%u");

  /** Is asynchronous read supported (read-write) */
  // unsigned async_read;
  log_struct(conn, async_read, "%u");

  /** Maximum size of the write buffer */
  // unsigned max_write;
  log_struct(conn, max_write, "%u");

  /** Maximum readahead */
  // unsigned max_readahead;
  log_struct(conn, max_readahead, "%u");

  /** Capability flags, that the kernel supports */
  // unsigned capable;
  log_struct(conn, capable, "%u");

  /** Capability flags, that the filesystem wants to enable */
  // unsigned want;
  log_struct(conn, want, "%u");

  /** For future use. */
  // unsigned reserved[23];
}

// struct fuse_file_info keeps information about files (surprise!).
// This dumps all the information in a struct fuse_file_info.  The struct
// definition, and comments, come from /usr/include/fuse/fuse_common.h
// Duplicated here for convenience.
void log_fi_impl(LOG_CONTEXT_PARAMS, struct fuse_file_info *fi) {
  log_msg_impl("struct fuse_file_info:", LOG_CONTEXT_ARGS);

  /** Open flags.  Available in open() and release() */
  //	int flags;
  log_struct(fi, flags, "%d");

  /** Old file handle, don't use */
  //	unsigned long fh_old;
  log_struct(fi, fh_old, "%lu");

  /** In case of a write operation indicates if this was caused by a
      writepage */
  //	int writepage;
  log_struct(fi, writepage, "%d");

  /** Can be filled in by open, to use direct I/O on this file.
      Introduced in version 2.4 */
  //	unsigned int keep_cache : 1;
  log_struct(fi, direct_io, "%u");

  /** Can be filled in by open, to indicate, that cached file data
      need not be invalidated.  Introduced in version 2.4 */
  //	unsigned int flush : 1;
  log_struct(fi, keep_cache, "%u");

  /** Padding.  Do not use*/
  //	unsigned int padding : 29;

  /** File handle.  May be filled in by filesystem in open().
      Available in all other file operations */
  //	uint64_t fh;
  log_struct(fi, fh, "%" PRId64);

  /** Lock owner id.  Available in locking operations and flush */
  //  uint64_t lock_owner;
  log_struct(fi, lock_owner, "%" PRIu64);
};

// This dumps the info from a struct stat.  The struct is defined in
// <bits/stat.h>; this is indirectly included from <fcntl.h>
void log_stat_impl(LOG_CONTEXT_PARAMS, struct stat *si) {
  log_msg_impl("struct stat:", LOG_CONTEXT_ARGS);

  //  dev_t     st_dev;     /* ID of device containing file */
  log_struct_with_cast(si, st_dev, "%" PRId64, int64_t);

  //  ino_t     st_ino;     /* inode number */
  log_struct_with_cast(si, st_ino, "%" PRId64, int64_t);

  //  mode_t    st_mode;    /* protection */
  log_struct(si, st_mode, "0%o");

  //  nlink_t   st_nlink;   /* number of hard links */
  log_struct(si, st_nlink, "%d");

  //  uid_t     st_uid;     /* user ID of owner */
  log_struct(si, st_uid, "%d");

  //  gid_t     st_gid;     /* group ID of owner */
  log_struct(si, st_gid, "%d");

  //  dev_t     st_rdev;    /* device ID (if special file) */
  log_struct_with_cast(si, st_rdev, "%" PRId64, int64_t);

  //  off_t     st_size;    /* total size, in bytes */
  log_struct(si, st_size, "%lld");

  //  blksize_t st_blksize; /* blocksize for filesystem I/O */
  log_struct_with_cast(si, st_blksize, "%" PRId64, int64_t);

  //  blkcnt_t  st_blocks;  /* number of blocks allocated */
  log_struct(si, st_blocks, "%lld");

  //  time_t    st_atime;   /* time of last access */
  log_struct(si, st_atime, "0x%08lx");

  //  time_t    st_mtime;   /* time of last modification */
  log_struct(si, st_mtime, "0x%08lx");

  //  time_t    st_ctime;   /* time of last status change */
  log_struct(si, st_ctime, "0x%08lx");
}

void log_inode_impl(LOG_CONTEXT_PARAMS, struct sfs_fs_inode *inode) {
  log_msg_impl("si:", LOG_CONTEXT_ARGS);

  log_struct(inode, inumber, "%" PRIu64);
  log_struct_with_cast(inode, mode, "%" PRIo64, int64_t);

  log_struct(inode, uid, "%" PRIu32);
  log_struct(inode, gid, "%" PRIu32);
  log_struct(inode, links, "%" PRIu32);

  log_struct(inode, access_time, "%" PRIu64);
  log_struct(inode, modified_time, "%" PRIu64);
  log_struct(inode, change_time, "%" PRIu64);

  log_struct(inode, size, "%" PRIu64);

#define b(n) log_struct(inode, block_pointers[n], "%" PRIu64)
  b(0);
  b(1);
  b(2);
  b(3);
  b(4);
  b(5);
  b(6);
  b(7);
  b(8);
  b(9);
  b(10);
  b(11);
  b(12);
  b(13);
#undef b
}
