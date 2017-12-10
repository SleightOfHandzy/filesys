/*
  Copyright (C) 2015 CS416/CS516

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.
*/

#include "block.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"

/** Read a block from an open file
 *
 * Read should return (1) exactly @BLOCK_SIZE when succeeded, or (2) 0 when the
 * requested block has never been touched before, or (3) a negtive value when
 * failed. In cases of error or return value equals to 0, the content of the
 * @buf is set to 0.
 */
int block_read(int fd, uint64_t block_num, void* block) {
  int ret = 0;
  // log_msg("block_read() %" PRIu64, block_num);
  ret = pread(fd, block, BLOCK_SIZE, block_num * BLOCK_SIZE);
  if (ret <= 0) {
    memset(block, 0, BLOCK_SIZE);
    if (ret < 0) perror("block_read failed");
  }

  return ret;
}

/** Write a block to an open file
 *
 * Write should return exactly @BLOCK_SIZE except on error.
 */
int block_write(int fd, uint64_t block_num, const void* block) {
  int ret = 0;
  // log_msg("block_write() %" PRIu64, block_num);
  ret = pwrite(fd, block, BLOCK_SIZE, block_num * BLOCK_SIZE);
  if (ret < 0) perror("block_write failed");

  return ret;
}
