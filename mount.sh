#!/bin/bash

diskfile="example/diskfile"
size=10g

if ! [ -f $diskfile ]; then
  uname="$(uname)"
  if [ "$uname" = "Darwin" ]; then
    mkfile -n $size $diskfile
  elif [ "$uname" = "Linux" ]; then
    fallocate -l $size $diskfile
  else
    echo "what system are you even on??"
  fi
fi

src/sfs $diskfile example/mountdir
