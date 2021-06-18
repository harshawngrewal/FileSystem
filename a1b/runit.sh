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

printf "\nHere is that state of fs before any changes\n"
stat -f /tmp/grewa309 # see file sys specs before any changes
ls -la /tmp/grewa309

printf "\nCreating a directory: mkdir /tmp/grewa309/hello\n"
mkdir /tmp/grewa309/hello # create a dir
printf "Creating a sub-directory: mdkir  /tmp/grewa309/hello/bye\n"
mkdir /tmp/grewa309/hello/bye 
printf "Creating a empty file: touch /tmp/grewa309/testing.txt\n"
touch /tmp/grewa309/testing.txt # create a file 
printf "Creating a file and write to it: echo welcome > /tmp/grewa309/testing.txt\n"
echo "welcome" > /tmp/grewa309/welcome.txt # creates file and writes to it
printf "Increasing size of file: truncate -s 10 /tmp/grewa309/welcome.txt\n"
truncate -s 10 /tmp/grewa309/welcome.txt # increase file size
printf "\nRead entire file: cat /tmp/grewa309/welcome.txt\n"
cat /tmp/grewa309/welcome.txt # reads the file contents(all of it)

printf "\nHere is the state of fs after previous operations\n"
stat -f /tmp/grewa309
ls -la /tmp/grewa309

printf "\nRemoving empty sub dir: rmdir /tmp/grewa309/hello/bye\n"
rmdir /tmp/grewa309/hello/bye # removes a directory(must be empty)
printf "Removing empty file: rm /tmp/grewa309/testing.txt\n"
rm /tmp/grewa309/testing.txt #removes a empty file

printf "\nHere is the state of fs after previous operations\n"
stat -f /tmp/grewa309
ls -la /tmp/grewa309

printf "\nUnmount the file system\n"
fusermount -u /tmp/grewa309

#remount to show nothing is lost
printf "\nRemount the file system to check that the disk is preserved\n"
./a1fs disk /tmp/grewa309
stat -f /tmp/grewa309
ls -la /tmp/grewa309
printf "\nRead entire file again: cat /tmp/grewa309/welcome.txt\n"
cat /tmp/grewa309/welcome.txt # reads the file contents(all of it)

fusermount -u /tmp/grewa309

