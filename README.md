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
