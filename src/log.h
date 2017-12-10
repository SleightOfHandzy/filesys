/*
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.
*/

#ifndef _LOG_H_
#define _LOG_H_

#include <inttypes.h>
#include <stdio.h>

#include "fs.h"
#include "fuse.h"
#include "params.h"

//  macro to log fields in structs.
#define log_struct_impl(st, field, format, cast)                \
  do {                                                          \
    fprintf(SFS_DATA->logfile, "    " #field " = " format "\n", \
            cast(st)->field);                                   \
    fflush(SFS_DATA->logfile);                                  \
  } while (0);
#define log_struct_with_cast(st, field, format, cast) \
  log_struct_impl(st, field, format, (cast))
#define log_struct(st, field, format) log_struct_impl(st, field, format, )

FILE *log_open(void);

#define log_msg_impl(fmt, ...)                                       \
  do {                                                               \
    fprintf(SFS_DATA->logfile, "%s:%u [%s] " fmt "\n", __VA_ARGS__); \
    fflush(SFS_DATA->logfile);                                       \
  } while (0);
#define log_msg_helper(fmt, ...) \
  log_msg_impl(fmt "%s", __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_msg(...) log_msg_helper(__VA_ARGS__, "")

// snag contextual information for logs
#define LOG_CONTEXT_PARAMS const char *file, unsigned line, const char *function
#define LOG_CONTEXT_ARGS file, line, function
#define DO_LOG_FN(fn, ...) fn(__FILE__, __LINE__, __func__, __VA_ARGS__)

void log_conn_impl(LOG_CONTEXT_PARAMS, struct fuse_conn_info *conn);
#define log_conn(...) DO_LOG_FN(log_conn_impl, __VA_ARGS__)

void log_fi_impl(LOG_CONTEXT_PARAMS, struct fuse_file_info *fi);
#define log_fi(...) DO_LOG_FN(log_fi_impl, __VA_ARGS__)

void log_fuse_context_impl(LOG_CONTEXT_PARAMS, struct fuse_context *context);
#define log_fuse_context(...) DO_LOG_FN(log_fuse_context_impl, __VA_ARGS__)

void log_stat_impl(LOG_CONTEXT_PARAMS, struct stat *si);
#define log_stat(...) DO_LOG_FN(log_stat_impl, __VA_ARGS__)

void log_inode_impl(LOG_CONTEXT_PARAMS, struct sfs_fs_inode *inode);
#define log_inode(...) DO_LOG_FN(log_inode_impl, __VA_ARGS__)

#endif
