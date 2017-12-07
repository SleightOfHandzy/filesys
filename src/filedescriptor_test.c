#include "filedescriptor.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
  if (sfs_filedescriptor_init() < 0) {
    perror("sfs_filedescriptor_init()");
    abort();
  }

  for (int i = 0; i < 10; ++i) {
    struct sfs_fd* fd = sfs_filedescriptor_allocate();
    printf("%d\n", fd->fd);
    sfs_filedescriptor_free(fd);
  }

#define BIG_NUMBER 1000000
  struct sfs_fd* arr[BIG_NUMBER];
  for (int i = 0; i < BIG_NUMBER; ++i) {
    arr[i] = sfs_filedescriptor_allocate();
    assert(arr[i]->fd == i);
  }

  for (int i = 0; i < BIG_NUMBER; ++i) {
    sfs_filedescriptor_free(arr[i]);
  }

  sfs_filedescriptor_deinit();
}
