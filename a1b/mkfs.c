/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019 Karen Reid
 */

/**
 * CSC369 Assignment 1 - a1fs formatting tool.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#include "a1fs.h"
#include "map.h"
#include "helpers.c"


/** Command line options. */
typedef struct mkfs_opts {
	/** File system image file path. */
	const char *img_path;
	/** Number of inodes. */
	size_t n_inodes;

	/** Print help and exit. */
	bool help;
	/** Overwrite existing file system. */
	bool force;
	/** Zero out image contents. */
	bool zero;

} mkfs_opts;

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
	fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
	char o;
	while ((o = getopt(argc, argv, "i:hfvz")) != -1) {
		switch (o) {
			case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

			case 'h': opts->help  = true; return true;// skip other arguments
			case 'f': opts->force = true; break;
			case 'z': opts->zero  = true; break;

			case '?': return false;
			default : assert(false);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing image path\n");
		return false;
	}
	opts->img_path = argv[optind];

	if (opts->n_inodes == 0) {
		fprintf(stderr, "Missing or invalid number of inodes\n");
		return false;
	}
	return true;
}


/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
	// image marks the start of the disk image. If there is a valid disk image present
	struct a1fs_superblock *sb = (struct a1fs_superblock *)(image);

	if(sb->magic != A1FS_MAGIC) 
		return false;

	return true;
}


/**
 * helper function to initalize the block bitmap field in the super block
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param 	The superblock struct	
 * @return	true on success;
 * 					false on error, e.g. total blocks needed to format disk it more than possible
 */

bool init_block_bitmap(a1fs_superblock *sb){
	sb->block_bitmap.count = ceil_integer_division(sb->blocks_count, A1FS_BLOCK_SIZE * 8);
	if(1 + sb->inode_bitmap.count + sb->inode_table.count + sb->block_bitmap.count > sb->blocks_count)
		return false;
	return true;
}


/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
	memset(image, 0, size); // to ensure that our disk can be properly formatted

	char byte;
	// initialize the super block
	a1fs_superblock *sb = calloc(1, sizeof(a1fs_superblock));
	sb->magic = A1FS_MAGIC;
	sb->size = size;
	sb->inodes_count = opts->n_inodes;
	sb->blocks_count = sb->size / A1FS_BLOCK_SIZE; // don't have to ceil I know size if block aligned

	sb->inode_bitmap.start = 1;
	sb->inode_bitmap.count = ceil_integer_division(sb->inodes_count, A1FS_BLOCK_SIZE * 8);
	sb->block_bitmap.start = 1 + sb->inode_bitmap.count;
	sb->inode_table.count = ceil_integer_division(sb->inodes_count * sizeof(a1fs_inode), A1FS_BLOCK_SIZE);

	if(!init_block_bitmap(sb)){
		return false; // can't find format the required number of blocks for bitmap into the disk image
	}
	sb->inode_table.start = sb->block_bitmap.start + sb->block_bitmap.count;

	sb->free_inodes_count = sb->inodes_count - 1; // -1 because we are going to create one for root dir of the file system
	sb->free_blocks_count = sb->blocks_count - 1 - sb->inode_bitmap.count - sb->block_bitmap.count - sb->inode_table.count;
	sb->first_data_block = sb->inode_table.start + sb->inode_table.count;

	memcpy(image, sb, sizeof(a1fs_superblock));

	// need to flip the bits in data block bitmap to signal allocated blocks
	for(uint32_t i = 0; i < 1 + sb->inode_bitmap.count + sb->block_bitmap.count + sb->inode_table.count; i++){
		byte = ((char *)image)[sb->block_bitmap.start * A1FS_BLOCK_SIZE + i / 8];
		byte = byte | (1 << (i % 8));
		memcpy(image + sb->block_bitmap.start * A1FS_BLOCK_SIZE + (i / 8), &byte, sizeof(char));
	}

	// we must now create the root dir inode and write to the disk image
	struct a1fs_inode *root_dir_inode = calloc(1,  sizeof(a1fs_inode));
	root_dir_inode->mode = S_IFDIR | 0777;
	clock_gettime(CLOCK_REALTIME, &root_dir_inode->mtime);
	root_dir_inode->links = 2; // .. and . are both links to itself
	root_dir_inode->size = 0; // for now. As this direcory does not have dentries
	root_dir_inode->indirect = 0; // no indirect block yet
	root_dir_inode->num_extents = 0; // no extents allocated yet

	memcpy(image + sb->inode_table.start * A1FS_BLOCK_SIZE, root_dir_inode, sizeof(a1fs_inode));
	byte = ((char *)image)[sb->inode_bitmap.start * A1FS_BLOCK_SIZE];
	byte = byte | (1 << 0);
	memcpy(image + sb->inode_bitmap.start * A1FS_BLOCK_SIZE, &byte, sizeof(char));

	free(sb);
	free(root_dir_inode);
	return true;
}


int main(int argc, char *argv[])
{
	mkfs_opts opts = {0};// defaults are all 0
	if (!parse_args(argc, argv, &opts)) {
		// Invalid arguments, print help to stderr
		print_help(stderr, argv[0]);
		return 1;
	}
	if (opts.help) {
		// Help requested, print it to stdout
		print_help(stdout, argv[0]);
		return 0;
	}

	// Map image file into memory
	size_t size;
	void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
	if (image == NULL) return 1;

	// Check if overwriting existing file system
	int ret = 1;
	if (!opts.force && a1fs_is_present(image)) {
		fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
		goto end;
	}

	if (opts.zero) memset(image, 0, size);
	if (!mkfs(image, size, &opts)) {
		fprintf(stderr, "Failed to format the image\n");
		goto end;
	}

	ret = 0;
end:
	munmap(image, size);
	return ret;
}
