/**
 * test program to step through some file system operations so I can digest the
 * FUSE operations with my eyeballs more good
 */

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

void hit_enter_pls() {
  fprintf(stderr, "hit enter pls");
  fflush(stderr);
  // assume stdin is line buffered
  char d;
  scanf("%c", &d);
}

#define step(x)                                      \
  printf("[at %s:%d] %s\n", __FILE__, __LINE__, #x); \
  hit_enter_pls();                                   \
  x;

int main() {
  int ret;

  printf("gonna open a file\n");
  step(int fd = open("example/mountdir/file.txt", O_CREAT | O_RDWR,
                     S_IRWXU | S_IRWXG | S_IRWXO));

  printf("opened %d\n", fd);

  printf("gonna close a file\n");
  step(ret = close(fd));
  printf("close() returned %d\n", ret);
}
