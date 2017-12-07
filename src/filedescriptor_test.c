#include "filedescriptor.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
  void* pool = sfs_filedescriptor_pool_init();
  if (pool == NULL) {
    perror("sfs_filedescriptor_init()");
    abort();
  }

  for (int i = 0; i < 10; ++i) {
    struct sfs_fd* fd = sfs_filedescriptor_allocate(pool);
    printf("%d\n", fd->fd);
    sfs_filedescriptor_free(pool, fd);
  }

#define BIG_NUMBER 1000000
  struct sfs_fd* arr[BIG_NUMBER];
  for (int i = 0; i < BIG_NUMBER; ++i) {
    arr[i] = sfs_filedescriptor_allocate(pool);
    assert(arr[i]->fd == i);
  }

  for (int i = 0; i < BIG_NUMBER; ++i) {
    sfs_filedescriptor_free(pool, arr[i]);
  }

  sfs_filedescriptor_pool_deinit(pool);
}
