#!/bin/bash

# create image, formate it and mount it
fusermount -u /tmp/grewa309 # just in case mounted image without running script
make
if [ ! -d "/tmp/grewa309" ]; then
  mkdir /tmp/grewa309
fi

truncate -s 40960 disk 
./mkfs.a1fs -f -i 10 disk
./a1fs disk /tmp/grewa309

# perform a few operations
stat -f /tmp/grewa309 # see file sys specs before any changes
mkdir /tmp/grewa309/hello # create a dir
mkdir /tmp/grewa309/hello/bye # create a subdir
touch /tmp/grewa309/testing.txt # create a file 
echo "welcome" > /tmp/grewa309/welcome.txt # creates file and writes to it
truncate -s 10 /tmp/grewa309/welcome.txt # increase file size
cat /tmp/grewa309/welcome.txt # reads the file contents(all of it)

stat -f /tmp/grewa309
ls -la /tmp/grewa309

rmdir /tmp/grewa309/hello/bye # removes a directory(must be empty)
rm /tmp/grewa309/testing.txt #removes a empty file

# see changes
stat -f /tmp/grewa309
ls -la /tmp/grewa309

#unmount
fusermount -u /tmp/grewa309

#remount to show nothing is lost
./a1fs disk /tmp/grewa309
stat -f /tmp/grewa309
ls -la /tmp/grewa309
cat /tmp/grewa309/welcome.txt # reads the file contents(all of it)

fusermount -u /tmp/grewa309

