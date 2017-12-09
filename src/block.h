/*
  Copyright (C) 2015 CS416/CS516

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.
*/

#ifndef _BLOCK_H_
#define _BLOCK_H_

#include <stdint.h>

#define BLOCK_SIZE 512

typedef char sfs_block_t[BLOCK_SIZE];

int block_read(int fd, uint64_t block_num, void* block);
int block_write(int fd, uint64_t block_num, const void* block);

#endif
