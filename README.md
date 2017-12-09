# Operating Systems Asst3 - File System

## Building

```bash
./autogen.sh
./configure
make
```

## Testing

Create and mount a 10G disk file with `./mount.sh`.

There are some example programs in the "example" directory that test the
filesystem. They assume the filesystem was created and mounted with
`./mount.sh` and were run from the project root.


## Design

The design of the filesystem is loosely based on ext2, but way simpler.

### `fs.{h,c}`

Our on disk format starts with a superblock to make sure the disk file "looks
like" a disk file and to describe the fs parameters (which aren't tunable, but
could be made to be). We then have a fixed table of inodes that take up a few
of the following blocks, and the rest of the blocks are data.

The user who creates the filesystem gets their UID and GID stamped on the root
directory.

We choose to use 6.25% of the disk space for inodes. We squeeze about 4 inodes
into a block, so this is a large enough pool to create a large number of
nonempty files and directories.

To partition free space for inodes, we use a freelist whose head is stored in
the superblock. To partition free blocks, we use an index list approach: the
head of the list is an index of block numbers that are free in no particular
order (sadly this is bad for contiguous block allocation) with the exception
that the first block number in the list is the block number of the next index
node (which has the same structure).

### `dir.{h,c}`
