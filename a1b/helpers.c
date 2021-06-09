
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "a1fs.h"
#include "fs_ctx.h"
#include "options.h"

uint32_t min(uint32_t num1, uint32_t num2){
		return num1 < num2 ? num1: num2;
 
}

uint32_t max(uint32_t num1, uint32_t num2){
	return num1 > num2 ? num1: num2;
}

uint32_t ceil_integer_division(uint32_t num1, uint32_t num2){
	return  num1 % num2 != 0 ? num1 / num2 + 1 : num1 / num2;
}

long find_dir_entry(uint32_t inode_num, char *target_name, fs_ctx *fs){
	// return 0;
	// We can calculate the number of entries this directory has
	a1fs_inode* inode = (a1fs_inode *)(fs->image + fs->inode_table * A1FS_BLOCK_SIZE + inode_num * sizeof(a1fs_inode));
	a1fs_extent *curr_extent; 
	a1fs_dentry *curr_dentry; // The current entry we are looking at

	// First we check the 10 direct extent blocks then the indirect block 
	// We are checking 522 which is more than needed
	for(int i = 0; i < 522; i++){
		if(i < 10)
			curr_extent = &inode->extents[i];
		else{
			if(inode->indirect <= 0) // indirect block is not allocated
				return -1; 

			curr_extent = (a1fs_extent *) (fs->image + inode->indirect * sizeof(A1FS_BLOCK_SIZE) + (i - 10) * sizeof(a1fs_extent));
		}
		// if the count is <= 0 is implies that there is no in use extent in that location

		if(curr_extent->count > 0){
			// this extent is valid
			for (a1fs_blk_t j = curr_extent->start; j < curr_extent->start + curr_extent->count; j ++){
				// Each block can fit a max of 16 dentries. Need to check if any match the target
				for(int k = 0; k < 16; k ++){
					curr_dentry = (a1fs_dentry *) (fs->image + j * sizeof(A1FS_BLOCK_SIZE) + k * sizeof(a1fs_dentry));
					
					if(curr_dentry->ino > 0 && strcmp(target_name, curr_dentry->name))
						return curr_dentry->ino; // we have found the target directory
				}

			}
		}
	}

	return -1; // could not find the dentry 
}

// we return long so that we can accomadate -error messages along with uint32 inode
long path_lookup(const char *path, fs_ctx *fs){
	if (strlen(path) >= A1FS_PATH_MAX) return -ENAMETOOLONG;
	if(path[0] != '/') {
		fprintf(stderr, "Not an absolute path\n");
		return -ENOTDIR; // there is not refernce to the root node in the path
  }

	char *path_copy = calloc(strlen(path), sizeof(char));
  strcpy(path_copy, &path[1]); // we do not include the root dir in our path as we know it exists. Otherwise can't mount the file system

	char *stringp = strsep(&path_copy, "/");
	long curr_node = 0; // root node

	while(path_copy != NULL){
		if(strlen(stringp) >= A1FS_NAME_MAX)
			return -ENAMETOOLONG; 

		curr_node = find_dir_entry(curr_node, stringp, fs);
		if (curr_node == -1){
				fprintf(stderr, "an element in the path cannot be found\n");
				return -ENOENT;
		}
		stringp = strsep(&path_copy, "/");
	}

	// now we are in on the last element of the path
	free(path_copy);
  if(strlen(stringp) >= A1FS_NAME_MAX)
			return -ENAMETOOLONG; 
	if(strcmp(stringp, "") != 0){
			curr_node = find_dir_entry(curr_node, stringp, fs);	// the path is not the root node
	}

	if (curr_node == -1){
			fprintf(stderr, "An element in the path cannot be found\n");
			return -ENOENT;
	}

	return curr_node;
}