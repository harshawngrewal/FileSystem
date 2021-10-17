This is a simple extent based file system using the libfuse library

### Prerequiste
1. GCC compiler
2. Linux OS or VM

### First Time Setup

To be able to use FUSE, it must be running in your kernel, so this means that it is not feasible to run the code for this tutorial on your Mac OS, Windows, WSL on Windows. These operating systems have FUSE installed, but they have not turned on the options that allow you to implement your own file system. 

1. Clone this repo using the instructions here [Cloning a repository - GitHub Docs](https://docs.github.com/en/repositories/creating-and-managing-repositories/cloning-a-repository)
2. On your local machine, go into **/FileSystem/a1b** 

### Running the Project

1. There is a bash script call runit.sh, you can run it using the command **./runit.sh**. This script creates a disk image and mounts the file system to tmp/grewa309 and so now the file systems commands within that directory will utilize the file system we have defined 
2. You may have to add executing permissions to the bash script

### Link to Demo

