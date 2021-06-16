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
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

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
#include "map.h"
#include "helpers.h"

//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
	// Nothing to initialize if only printing help
	if (opts->help) return true;

	size_t size;
	void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
	if (!image) return false;

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}

// helper functions
static int a1fs_truncate(const char *path, off_t size); // so that helper function does not compain

/**
 * write the provided dir_entry to the fs under the given target_inode/parent directory
 *
 * NOTE: Can assume that parent path exists and is a directory
 *
 * @param path  					the abolute path of the parent
 * @param new_dir_dentry  the entry we have to add to the parent
 * @param fs  						file system struct
 * @return       					0 on success and -error
 */
int add_dir_entry(char *path, a1fs_dentry *new_dir_dentry, fs_ctx *fs, bool is_dir){
	long inode_num = path_lookup(path, fs); // don't have to error check due to precondition
	a1fs_inode *parent_inode = (a1fs_inode *)(fs->image + fs->inode_table.start *\
		A1FS_BLOCK_SIZE + inode_num * sizeof(a1fs_inode));
	a1fs_extent *curr_extent; 
	a1fs_dentry *curr_dentry; // The current entry we are looking at
	
	// have to loop every single block and every single dentry to find any empty location
	// if we can't find an empty location we use truncate to extend the file
	for(uint32_t i = 0; i < parent_inode->num_extents; i++){
		if(i < 10)
			curr_extent = &parent_inode->extents[i];
		else
			curr_extent = (a1fs_extent *) (fs->image + parent_inode->indirect * sizeof(A1FS_BLOCK_SIZE) + (i - 10) * sizeof(a1fs_extent));
		// if the count is <= 0 is implies that there is no in use extent in that location

		if(curr_extent->count > 0){
			// this extent is valid and is not empty
			for (a1fs_blk_t j = curr_extent->start; j < curr_extent->start + curr_extent->count; j ++){
				// we act like there are 16 dentries because we null blocks before we allocate a block to a inode extent
				for(int k = 0; k < 16; k ++){
					curr_dentry = (a1fs_dentry *) (fs->image + j * A1FS_BLOCK_SIZE + k * sizeof(a1fs_dentry));
					
					if(curr_dentry->ino == 0){
						// we found a dentry that is not in sue
						memcpy(fs->image + j * A1FS_BLOCK_SIZE + k * sizeof(a1fs_dentry), new_dir_dentry, sizeof(a1fs_dentry));
						parent_inode->size += sizeof(a1fs_dentry);
						if(is_dir){
							parent_inode->links += 1; // this should only be done if dentry is a dir 
						}

						clock_gettime(CLOCK_REALTIME, &parent_inode->mtime); // update the modification time
						memcpy(fs->image + fs->inode_table.start * A1FS_BLOCK_SIZE + inode_num * \
							sizeof(a1fs_inode), parent_inode, sizeof(a1fs_inode));

						return 0; // success
					}
				}

			}
		}
	}

	// otherwise we are going to have allocate another block, maybe another extent and maybe even indirect
	// block, so we use out truncate method as it does that for us
	int res = a1fs_truncate(path, parent_inode->size + sizeof(a1fs_dentry));
	if(res < 0){
		return res; // we could not allocate space for whatever reason(inode table full, block table full)
	}

	// we want to grab it again since we made some changes to its fields
	parent_inode = (a1fs_inode *)(fs->image + fs->inode_table.start *\
		A1FS_BLOCK_SIZE + inode_num * sizeof(a1fs_inode)); 

	// since we did truncate, there is space for this dentry
	a1fs_extent *last_extent = get_final_extent(parent_inode, fs);
	uint32_t last_block = last_extent->start + last_extent->count - 1; 
	uint32_t offset_into_last_block = (parent_inode->size - sizeof(a1fs_dentry)) % A1FS_BLOCK_SIZE;

	memcpy(fs->image + last_block * A1FS_BLOCK_SIZE + offset_into_last_block, new_dir_dentry, sizeof(a1fs_dentry));

	if(is_dir){
		parent_inode->links += 1; // this should only be done if dentry is a dir 
	}

	clock_gettime(CLOCK_REALTIME, &parent_inode->mtime); // update the modification time
	memcpy(fs->image + fs->inode_table.start * A1FS_BLOCK_SIZE + inode_num * \
		sizeof(a1fs_inode), parent_inode, sizeof(a1fs_inode));

	return 0;
}


/**
 * Helper function which intializes and creates an inode wether that be a directory or 
 * a file
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory or file which we have to create
 * @return      0 on success; -errno on error.
 */
int init_inode(const char *path, mode_t mode, fs_ctx *fs){

	a1fs_inode *inode = calloc(1, sizeof(a1fs_inode));
	if(inode == NULL)
		return -ENOMEM;
	bool is_dir =  S_ISREG(mode) ? false : true;
	inode->mode = mode;
	inode->links = S_ISREG(mode) ? 1 : 2; // default links for a file or directory
	inode->size = 0;
	clock_gettime(CLOCK_REALTIME, &inode->mtime);
	inode->indirect = 0;
	inode->num_extents = 0;

	long res = allocate_inode(fs); // will allocate the first empty inode in inode_bitmap
	if(res < 0){
		free(inode);
		return -ENOSPC; // can't allocate an inode as all inodes are allocated
	}	

	// Get the parent dir inode, modify links value and add a dir entry
	char *last_component = get_last_component(path);
	a1fs_dentry *new_dir_dentry = calloc(1, sizeof(a1fs_dentry));
	strcpy(new_dir_dentry->name, last_component);
	new_dir_dentry->ino = res;

	char parent_path[strlen(path)];
	strcpy(parent_path, path);
	set_parent_path(parent_path);

	if(add_dir_entry(parent_path, new_dir_dentry, fs, is_dir) < 0){
		free(inode);
		free(new_dir_dentry);
		return -ENOSPC; // couldn't allocate a dir_entry
	}
	
	// all operation successful, it is now safe to write to the disk
	set_bitmap(fs->sb->inode_bitmap.start, res, fs, 1);
	memcpy(fs->image + fs->sb->inode_table.start * A1FS_BLOCK_SIZE +  res * sizeof(a1fs_inode), inode, sizeof(a1fs_inode));
	memcpy(fs->image, fs->sb, sizeof(a1fs_superblock));

	free(new_dir_dentry);
	free(inode);

	return 0;
}


/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int a1fs_statfs(const char *path, struct statvfs *st)
{
	(void)path;// unused
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));
	st->f_bsize   = A1FS_BLOCK_SIZE;
	st->f_frsize  = A1FS_BLOCK_SIZE;
	//TODO: fill in the rest of required fields based on the information stored
	// in the superblock

	st->f_blocks = fs->size / A1FS_BLOCK_SIZE; // size of file system in fragment size units
	st->f_bfree = fs->sb->free_blocks_count;
	st->f_bavail = st->f_bfree; // They are the same
	st->f_files = fs->sb->inodes_count;
	st->f_ffree = fs->sb->free_inodes_count;
	st->f_favail = st->f_ffree; // They are the same

	st->f_namemax = A1FS_NAME_MAX;
	return 0; // in which case do we return errno?
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the 
 *       inode.
 *
 * NOTE2: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int a1fs_getattr(const char *path, struct stat *st)
{
	fs_ctx *fs = get_fs();
	long curr_node = path_lookup(path, fs);
	if(curr_node < 0)
		return curr_node; // path_lookup returned an error

	// Now we update the stat struct
	a1fs_inode *final_inode = (a1fs_inode *)(fs->image + fs->inode_table.start * A1FS_BLOCK_SIZE + curr_node * sizeof(a1fs_inode));
	st->st_mode = final_inode->mode;
	st->st_nlink = final_inode->links;
	st->st_size = final_inode->size; // does size include inode
	st->st_blocks = st->st_size % 512 != 0 ? st->st_size  / 512 + 1 + A1FS_BLOCK_SIZE / 512  : st->st_size  / 512 + A1FS_BLOCK_SIZE / 512; // not sure about this.
	st->st_mtim = final_inode->mtime; 

	return 0; 
}


/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset; // unused
	(void)fi; // unused

	//NOTE: This is just a placeholder that allows the file system to be mounted
	// without errors. You should remove this from your implementation.
	// if (strcmp(path, "/") == 0) {
	// 	filler(buf, "." , NULL, 0);
	// 	filler(buf, "..", NULL, 0);
	// 	return 0;
	// }

	filler(buf, "." , NULL, 0);
	filler(buf, "..", NULL, 0);

	//TODO: lookup the directory inode for given path and iterate through its
	// directory entries
	fs_ctx *fs = get_fs();
	long curr_node = path_lookup(path, fs); // can assume that path exists

	// We have a valid inode. Now we iterate over it's dentries
	a1fs_inode *final_inode = (a1fs_inode *)(fs->image + fs->inode_table.start * A1FS_BLOCK_SIZE + curr_node * sizeof(a1fs_inode));
	a1fs_extent *curr_extent; 
	a1fs_dentry *curr_dentry;
	uint32_t num_entries_in_block = 16; // default amount unless we in the last block of the last extent

	for(uint32_t i = 0; i < final_inode->num_extents; i++){
		if(i < 10)
			curr_extent = &final_inode->extents[i];
		else
			curr_extent = (a1fs_extent *) (fs->image + final_inode->indirect * sizeof(A1FS_BLOCK_SIZE) + (i - 10) * sizeof(a1fs_extent));


		// this extent is valid
		for (a1fs_blk_t j = curr_extent->start; j < curr_extent->start + curr_extent->count; j ++){
			// Each block can fit a max of 16 dentries. however if we are looking at the last block
			// it may not have 16 entries
			if(i == final_inode->num_extents - 1 && j == curr_extent->start + curr_extent->count - 1 &&  final_inode->size % A1FS_BLOCK_SIZE != 0 ){
				num_entries_in_block = (final_inode->size % A1FS_BLOCK_SIZE) / sizeof(a1fs_dentry);
			}
			for(uint32_t k = 0; k < num_entries_in_block; k ++){
				curr_dentry = (a1fs_dentry *) (fs->image + j * A1FS_BLOCK_SIZE + k * sizeof(a1fs_dentry));
				
				if(curr_dentry->ino > 0){ // valid entry
					filler(buf, curr_dentry->name , NULL, 0);
				}
			}
		}
	}

	return 0;
}


/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
	mode = mode | S_IFDIR;
	fs_ctx *fs = get_fs();
	return init_inode(path, mode, fs);
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
	fs_ctx *fs = get_fs();
	int dir_ino = path_lookup(path, fs); // can assume this succeeds due to precondition
	a1fs_inode* inode = (a1fs_inode *)(fs->image + fs->inode_table.start * A1FS_BLOCK_SIZE + dir_ino * sizeof(a1fs_inode));

	if(inode->size != 0)
		return -ENOTEMPTY;

	set_bitmap(fs->sb->inode_bitmap.start, dir_ino, fs, false); // deallocate the inode

	// now we have to remove this file from it's parent as a dentry and 
	char parent_path[strlen(path)];
	strcpy(parent_path, path);
	set_parent_path(parent_path); // set the parent_path to the path of the parent
	char *file_name = get_last_component(path); // gets the relative name of the dir we want to remove
	long inode_num = path_lookup(parent_path, fs); // don't have to error check due to precondition
	
	return remove_dir_entry(inode_num, file_name, true, fs);

	//TODO: remove the directory at given path (only if it's empty)
	(void)path;
	(void)fs;
	return -ENOSYS;
}


/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();

	return init_inode(path, mode, fs);
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();
	a1fs_truncate(path, 0); // will deallocate any blocks associated with this file
	int dir_ino = path_lookup(path, fs); // can assume this succeeds due to precondition
	set_bitmap(fs->sb->inode_bitmap.start, dir_ino, fs, false); // deallocate the inode

	// now we have to remove this file from it's parent as a dentry and 
	char parent_path[strlen(path)];
	strcpy(parent_path, path);
	set_parent_path(parent_path); // set the parent_path to the path of the parent
	char *file_name = get_last_component(path); // gets the relative name of the dir we want to remove
	long inode_num = path_lookup(parent_path, fs); // don't have to error check due to precondition
	
	remove_dir_entry(inode_num, file_name, false, fs);
	return 0;

}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec times[2])
{
	//TODO: update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page

	fs_ctx *fs = get_fs();

	uint32_t file_inode_num = path_lookup(path, fs);
	a1fs_inode *file_inode = (a1fs_inode *)(fs->image + fs->inode_table.start* A1FS_BLOCK_SIZE + file_inode_num* sizeof(a1fs_inode));

	if(times == NULL || times[1].tv_nsec == UTIME_NOW)
		clock_gettime(CLOCK_REALTIME, &file_inode->mtime);
	
	else if(times[1].tv_nsec != UTIME_OMIT)
		file_inode->mtime = times[1];
	
	// otherwise we do nothing
	return 0;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int a1fs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();
	uint32_t file_inode_num = path_lookup(path, fs);
	a1fs_inode *file_inode = (a1fs_inode *)(fs->image + fs->inode_table.start* A1FS_BLOCK_SIZE + file_inode_num* sizeof(a1fs_inode));
	a1fs_extent *final_extent; 

	//TODO: set new file size, possibly "zeroing out" the uninitialized range
	if((uint64_t)size <= file_inode->size){
		uint32_t bytes_in_last_block = file_inode->size % A1FS_BLOCK_SIZE == 0 ? A1FS_BLOCK_SIZE : file_inode->size % A1FS_BLOCK_SIZE;
		uint32_t target_num_removed_blocks = file_inode->size < (uint64_t)size + bytes_in_last_block ? 0:\
		(file_inode->size - size - bytes_in_last_block) / A1FS_BLOCK_SIZE + 1; // +1 for the last block
		fs->sb->free_blocks_count -= target_num_removed_blocks;
		
		if(target_num_removed_blocks > 0){
			while(target_num_removed_blocks > 0){
				target_num_removed_blocks -= deallocate_block(file_inode, fs);
			}
			// it can be that case that the indirect block is not longer in use
			if(file_inode->num_extents <= 10 && file_inode->indirect != 0){
				set_bitmap(fs->sb->block_bitmap.start, file_inode->indirect, fs, false);
			}
		}
	}
	
	// have to extend the file size
	else{
		uint32_t bytes_in_last_block = file_inode->size % A1FS_BLOCK_SIZE == 0 && \
			file_inode->size != 0 ? A1FS_BLOCK_SIZE : file_inode->size % A1FS_BLOCK_SIZE;
		uint32_t nonallocated_bytes_last_block = file_inode->size == 0 ? 0: A1FS_BLOCK_SIZE - bytes_in_last_block;
		uint32_t total_additional_bytes = size - file_inode->size;
		uint32_t additional_blocks = total_additional_bytes <= nonallocated_bytes_last_block ? 0:\
		 ceil_integer_division(total_additional_bytes - nonallocated_bytes_last_block, A1FS_BLOCK_SIZE);

		 uint32_t copy_additional_blocks = additional_blocks;

		if(additional_blocks > fs->sb->free_blocks_count)
			return -ENOSPC; // not enough data blocks for the new size of file

		if(file_inode->size == 0){
			long res = allocate_extent(additional_blocks, file_inode, fs);
			if(res < 0)
				return res; // error could not allocate an extent or block for extent

			additional_blocks -= res;
		}

		final_extent = get_final_extent(file_inode, fs); // the final extent
		memset(fs->image + (final_extent->start + final_extent->count - 1) * A1FS_BLOCK_SIZE + bytes_in_last_block, 0,\
			min(total_additional_bytes, nonallocated_bytes_last_block));


		if (additional_blocks != 0){
				// first we try to extend the last block as much as possible
			uint32_t max_extentsion = extend_extent(additional_blocks, file_inode, final_extent, fs);
			additional_blocks -= max_extentsion;	
			// now we allocate the new extents
			while(additional_blocks > 0){

				// edge cases needs to be tested
				if((file_inode->num_extents + 1 == 10 && additional_blocks + 1 > fs->sb->free_blocks_count) || file_inode->num_extents > 512){
						// have to reverse the changes we made by calling truncate recrusively
						file_inode->size = file_inode->size + (copy_additional_blocks - additional_blocks) * A1FS_BLOCK_SIZE + nonallocated_bytes_last_block;
						memcpy(fs->image + fs->inode_table.start * A1FS_BLOCK_SIZE + file_inode_num * sizeof(a1fs_inode), file_inode, sizeof(a1fs_inode));
						a1fs_truncate(path, file_inode->size - (copy_additional_blocks - additional_blocks) * A1FS_BLOCK_SIZE + nonallocated_bytes_last_block);
						return -ENOSPC;
				}

				additional_blocks -= allocate_extent(additional_blocks, file_inode, fs);
			}
		}
	}

	// rwe rewrite inode since we update size of file and also any changed to fields like num_extents, indirect_block
	file_inode->size = size;
	memcpy(fs->image + fs->inode_table.start * A1FS_BLOCK_SIZE + file_inode_num * sizeof(a1fs_inode), file_inode, sizeof(a1fs_inode));
	return 0;

}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: read data from the file at given offset into the buffer
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;
	return -ENOSYS;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   ENOSPC  too many extents (a1fs only needs to support 512 extents per file)
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: write data from the buffer into the file at given offset, possibly
	// "zeroing out" the uninitialized range
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;
	return -ENOSYS;
}

static struct fuse_operations a1fs_ops = {
	.destroy  = a1fs_destroy,
	.statfs   = a1fs_statfs,
	.getattr  = a1fs_getattr,
	.readdir  = a1fs_readdir,
	.mkdir    = a1fs_mkdir,
	.rmdir    = a1fs_rmdir,
	.create   = a1fs_create,
	.unlink   = a1fs_unlink,
	.utimens  = a1fs_utimens,
	.truncate = a1fs_truncate,
	.read     = a1fs_read,
	.write    = a1fs_write,
};

int main(int argc, char *argv[])
{
	a1fs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!a1fs_opt_parse(&args, &opts)) return 1;

	fs_ctx fs = {0};
	if (!a1fs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}
	return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}
