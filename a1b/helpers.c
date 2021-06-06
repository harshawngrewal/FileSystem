
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

int find_dir_entry(int inode_num, char *target_name, fs_ctx *fs){
	return 0;

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