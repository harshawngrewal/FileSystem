
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

/**
 * Given the parent node, scan the dentries for the the entry which 
 * @param path				the absolute path of the file or directory
 * @param target_name the name of the target file or directory
 * @param fs					the file system struct
 * 
 * NOTE: we can assume that now two dentries have the same name
 * @return      		the inode number of the target sub dir or -1 on error
 */
long find_dir_entry(uint32_t inode_num, char *target_name, fs_ctx *fs){
	// return 0;
	// We can calculate the number of entries this directory has
	a1fs_inode* inode = (a1fs_inode *)(fs->image + fs->inode_table.start * A1FS_BLOCK_SIZE + inode_num * sizeof(a1fs_inode));
	a1fs_extent *curr_extent; 
	a1fs_dentry *curr_dentry; // The current entry we are looking at

	// First we check the 10 direct extent blocks then the indirect block 
	// We are checking 522 which is more than needed
	for(uint32_t i = 0; i < inode->num_extents; i++){
		if(i < 10)
			curr_extent = &inode->extents[i];
		else
			curr_extent = (a1fs_extent *) (fs->image + inode->indirect * sizeof(A1FS_BLOCK_SIZE) + (i - 10) * sizeof(a1fs_extent));
		// if the count is <= 0 is implies that there is no in use extent in that location

		if(curr_extent->count > 0){
			// this extent is valid and is not empty
			for (a1fs_blk_t j = curr_extent->start; j < curr_extent->start + curr_extent->count; j ++){
				// Each block can fit a max of 16 dentries. Need to check if any match the target
				for(int k = 0; k < 16; k ++){
					curr_dentry = (a1fs_dentry *) (fs->image + j * A1FS_BLOCK_SIZE + k * sizeof(a1fs_dentry));
					
					if(curr_dentry->ino > 0 && strcmp(target_name, curr_dentry->name) == 0)
						return curr_dentry->ino; // we have found the target directory
				}

			}
		}
	}

	return -1; // could not find the dentry 
}


/**
 *
 * @param path	the absolute path of the file or directory
 * @param fs					the file system struct
 * 
 * @return      		the inode number of the file or directory specified in path 
 * 									or -error is something goes wrong(eg. path component is too long)
 */
long path_lookup(const char *path, fs_ctx *fs){
	if (strlen(path) >= A1FS_PATH_MAX) return -ENAMETOOLONG;
	if(path[0] != '/') {
		fprintf(stderr, "Not an absolute path\n");
		return -ENOTDIR; // there is not refernce to the root node in the path
  }

	char *path_copy = calloc(strlen(path), sizeof(char));
  strcpy(path_copy, &path[1]); // we do not include the root dir in our path as we know it exists
	char *stringp = strsep(&path_copy, "/");
	long curr_node = 0; // root node

	while(path_copy != NULL){
		if(strlen(stringp) >= A1FS_NAME_MAX)
			return -ENAMETOOLONG; 

		curr_node = find_dir_entry(curr_node, stringp, fs);
		if (curr_node == -1){
				fprintf(stderr, "An element in the path cannot be found\n");
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


/**
 * given the file inode, return the last extent
 * @param file_inode	the inode of a file or dir
 * @param fs					the file system struct
 * 
 * NOTE: 							Assume that the file/dir has at least one extent
 * @return      			the final extent of the file
 */
a1fs_extent * get_final_extent(a1fs_inode * file_inode, fs_ctx *fs){
	a1fs_extent *final_extent;
	if (file_inode->num_extents <= 10){
		final_extent = &file_inode->extents[file_inode->num_extents - 1];
	}

	else
		final_extent = (a1fs_extent *) (fs->image + file_inode->indirect * sizeof(A1FS_BLOCK_SIZE) + \
			(file_inode->num_extents - 10 - 1) * sizeof(a1fs_extent));
	
	return final_extent;
}

/**
 * flip the bit to either 0 or 1 for the given bitmap at the given location
 *
 * @param bitmap_block	the data block number of the bitmap
 * @param offset				the number of bits into the bitmap	
 * @param fs						the file system struct
 * @param set						false to flip to 0 and true to flip bit to 1		
 * 
 */

void set_bitmap(uint32_t bitmap_block, uint32_t offset, fs_ctx *fs , bool set){
	int x = 0;
	if(set)
		x = 1;

	if(x == 0){
		if(bitmap_block == fs->sb->block_bitmap.start)
			fs->sb->free_blocks_count += 1;
		else
			fs->sb->free_inodes_count += 1;
	}

	else{
		if(bitmap_block == fs->sb->block_bitmap.start)
			fs->sb->free_blocks_count -= 1;
		else
			fs->sb->free_inodes_count -= 1;
	}
	
	// modify a byte and the re write
	char byte = ((char *)fs->image)[bitmap_block * A1FS_BLOCK_SIZE + offset / 8];
	if(set)
		byte = byte | (1 << offset % 8); // flip bit to 1
	else
		byte = byte & ~(1 << offset % 8); // flip bit to 0


	memcpy(fs->image + bitmap_block * A1FS_BLOCK_SIZE + offset / 8, &byte, sizeof(char));
	memcpy(fs->image, fs->sb, sizeof(a1fs_superblock));

	if(set && fs->sb->block_bitmap.start == bitmap_block){
		memset(fs->image + offset * A1FS_BLOCK_SIZE, 0, A1FS_BLOCK_SIZE);// nulls the entire block
	}
	
} 


/**
 * allocate the first useable inode possible in the inode bitmap
 * @param fs  file system struct
 * @return       the inode allocated on success;
 *               -1 on error
 */

long allocate_inode(fs_ctx *fs){
	// loop over the inode bitmap
	uint32_t curr_block = 0;
	char curr_byte;
	while(curr_block < fs->sb->inodes_count){
		curr_byte = ((char *)fs->image)[fs->sb->inode_bitmap.start * A1FS_BLOCK_SIZE + (curr_block) / 8];
		for(int i = curr_block % 8; i < 8 && curr_block < fs->sb->inodes_count; i++){ 
			// loop from last_block + 1 to 7 as that represents the 8 bits in byte
			if((curr_byte & (1 << i)) == 0){
				return curr_block;
			}
			curr_block += 1;
		}
	}

	return -1;
}

long allocate_block(fs_ctx *fs){
	// loop over the inode bitmap
	uint32_t curr_block = 0;
	char curr_byte;
	while(curr_block < fs->sb->blocks_count){
		curr_byte = ((char *)fs->image)[fs->sb->block_bitmap.start * A1FS_BLOCK_SIZE + (curr_block) / 8];
		for(int i = curr_block % 8; i < 8 && curr_block < fs->sb->blocks_count; i++){ 
			// loop from last_block + 1 to 7 as that represents the 8 bits in byte
			if((curr_byte & (1 << i)) == 0){
				return curr_block;
			}
			curr_block += 1;
		}
	}

	return -1;

}

/**
 * Extend the extent by as many contigious blocks as possible
 *
 * @param max_blocks	the maximum number of blocks we want to extend by
 * @param inode				the file's inode which we want to extend
 * @param extent			the extent struct which we want to extend			
 * @param fs					the file system struct
 * 
 * @return      		the number of blocks that extent was extended by
 */
uint32_t extend_extent(uint32_t max_blocks, a1fs_inode *inode, a1fs_extent *extent, fs_ctx *fs){
	uint32_t last_block = extent->start + extent->count - 1;
	char byte;
	int cont = 0; 

	// find the byte in the bitmap where we start looking for contig blocks
	uint32_t curr_block = last_block + 1;
	uint32_t count = 0;

	while(curr_block < fs->sb->blocks_count && cont == 0){
		byte = ((char *)fs->image)[fs->sb->block_bitmap.start * A1FS_BLOCK_SIZE + (curr_block) / 8];
		for(int i = curr_block % 8; i < 8 && curr_block < fs->sb->blocks_count; i++){ 
			// loop from curr_block to 7 as that represents the 8 bits in byte
			if((byte & (1 << i)) == 0){
				set_bitmap(fs->sb->block_bitmap.start, curr_block, fs, true);
				count += 1;
			}
			else{
				cont = 1; // we have found the maximum blocks by which we can extend our inode
				break;
			}
			if(count >= max_blocks){
				cont = 1; // so that we can break out of the outer while loop
				break; // we don't care about if we can find a longer extent
			}

			curr_block += 1;
		}
	}

	// Update the extent a re-write it back to the disk
	extent->count += count;
	if (inode->num_extents <= 10)
		inode->extents[inode->num_extents - 1] = *extent; // don't need to write to disk
	else
		memcpy(fs->image + inode->indirect * sizeof(A1FS_BLOCK_SIZE) + (inode->num_extents - 10 - 1)\
			 * sizeof(a1fs_extent), extent, sizeof(a1fs_extent));

	return count;
}

/**
 * Allocate one extent with maximum max_blocks number of blocks and at least 1 block
 *
 * @param max_blocks	the maximum number of blocks we want the extent to have
 * @param inode				the file's inode which we want to extend
 * @param extent			the extent struct which we want to extend			
 * @param fs					the file system struct
 * 
 * NOTE: Assumed that there is at least one free block
 * @return      		the number of blocks that newly allocated extent has
 * 									-error if extent can't be allocated
 */
long allocate_extent(uint32_t max_blocks, a1fs_inode *inode, fs_ctx *fs){
	/* find starting block for the longest contigious number of blocks(but <= max_blocks)
	and call extend_block */ 
	char curr_byte; // the current byte in the bitmap we are looking at
	a1fs_extent longest_extent;
	a1fs_extent curr_extent;

	longest_extent.start = 0;
	longest_extent.count = 0;
	curr_extent.start = 0;
	curr_extent.count = 0;

	int cont = 0; //continue variable
	uint32_t curr_block = 0;
	while(curr_block < fs->sb->blocks_count && cont == 0){
		curr_byte = ((char *)fs->image)[fs->sb->block_bitmap.start * A1FS_BLOCK_SIZE + curr_block / 8];
		for(int i = 0; i < 8 && curr_block < fs->sb->blocks_count; i++){

			if((curr_byte & (1 << i)) == 0) 
				curr_extent.count += 1;

			else{
				if(curr_extent.count > longest_extent.count){
					longest_extent = curr_extent;
				}
				curr_extent.count = 0;
				curr_extent.start = curr_block + 1;
			}

			if(curr_extent.count >= max_blocks){
				longest_extent = curr_extent;
				cont = 1; // so that we can break out of the outer while loop
				break; // we don't care about if we can find a longer extent
			}

			curr_block += 1;
		}
	}

	

	// we should now loop over extent blocks and allocate them
	curr_block = longest_extent.start;
	while(curr_block < longest_extent.start + longest_extent.count){
		curr_byte = ((char *)fs->image)[fs->sb->block_bitmap.start * A1FS_BLOCK_SIZE + (curr_block) / 8];
		for(int i = curr_block % 8; i < 8 && curr_block < longest_extent.start + longest_extent.count; i++){ 
			// don't even need if statement because we verified it above
			if((curr_byte & (1 << i)) == 0){
				set_bitmap(fs->sb->block_bitmap.start, curr_block, fs, true);
				curr_block += 1;
			}
		}
	}
	
	// can assume there is a free block to for a indirect block if needed
	inode->num_extents += 1; // we have created a new extent

	if(inode->num_extents == 10){
		long res = allocate_block(fs); // can assume this is will return a valid block due to check we made in truncate
		inode->indirect = res;
		set_bitmap(fs->sb->block_bitmap.start, res, fs, true);
	}


	if (inode->num_extents <= 10)
		inode->extents[inode->num_extents - 1] = longest_extent; // don't need to write to disk
	else
		memcpy(fs->image + inode->indirect * sizeof(A1FS_BLOCK_SIZE) + (inode->num_extents - 10 - 1)\
			 * sizeof(a1fs_extent), &longest_extent, sizeof(a1fs_extent));


	return longest_extent.count;
}


/**
 * deallocate the last block of the file and update the inode or indirect block
 * with the modified extent. Also modify the super block 
 * @param inode  the inode of which whose last block we want to deallocate
 * @param fs		 the file system struct
 * @return       the number of blocks deallocated
 */
int deallocate_block( a1fs_inode *inode, fs_ctx *fs){
	a1fs_extent *final_extent = get_final_extent(inode, fs);
	uint32_t final_block = final_extent->start + final_extent->count - 1; // the last block

	// neeed to update block bitmap to show that final_block is now free to use
	set_bitmap(fs->sb->block_bitmap.start, final_block, fs, false);
	final_extent->count -= 1;

	// have to rewrite this extent back to the disk
	if (inode->num_extents <= 10)
		inode->extents[inode->num_extents - 1] = *final_extent; // we will write inode to disk in truncate
	else
		memcpy(fs->image + inode->indirect * sizeof(A1FS_BLOCK_SIZE) + (inode->num_extents - 10 - 1)\
			 * sizeof(a1fs_extent), final_extent, sizeof(a1fs_extent));

	if(final_extent->count == 0){
		inode->num_extents -= 1; // this could mean that the indirect block is not in use which we take care in truncate
	}

	return 1; // just deallocated 1 block
}

/**
 * Get the dir or file name from the absolute path 
 *
 * @param abs_path	the path for the directory or inode
 * 
 */
char* get_last_component(const char *abs_path){
	char *ptr = strrchr(abs_path, '/');
	return &ptr[1]; // don't have to error check since we are assured that the path is valid
}

/**
 * Gets the parent path given an absolute path
 *
 * NOTE: Can assume that parent path exists and is a directory
 *
 * @param path  absolute path of a file or directory
 */
void set_parent_path(char *path){
	//remove the last component of the path gives us the parent path
	char *ptr = strrchr(path, '/');
	char *ptr2 = strchr(path, '/');

	if(ptr == ptr2)
		// this means that the parent is / which is the root node(we don't want to null the root /)
		ptr[1] = '\0';

	else
		ptr[0] = '\0'; // removes the last component of the path to give the path of the parent node

}
