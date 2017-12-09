bin_PROGRAMS = sfs filedescriptor_test

sfs_SOURCES = sfs.c fuse.h log.c log.h params.h block.c block.h \
  filedescriptor.c filedescriptor.h fs.c fs.h dir.c dir.h

filedescriptor_test_SOURCES = filedescriptor.c filedescriptor.h \
  filedescriptor_test.c

AM_CPPFLAGS = -DFUSE_USE_VERSION=26 -D_XOPEN_SOURCE=500 -D_POSIX_C_SOURCE=200809L
AM_CFLAGS = @FUSE_CFLAGS@ -Wall -Werror -std=c11
LDADD = @FUSE_LIBS@
