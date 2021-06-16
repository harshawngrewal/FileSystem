#include "fs_ctx.h"

uint32_t ceil_integer_division(uint32_t num1, uint32_t num2);
uint32_t min(uint32_t num1, uint32_t num2);
uint32_t max(uint32_t num1, uint32_t num2);

long find_dir_entry(uint32_t inode_num, char *target_name, fs_ctx *fs);
int remove_dir_entry(uint32_t inode_num, char *target_name, bool is_dir, fs_ctx *fs);
long path_lookup(const char *path, fs_ctx *fs);

a1fs_extent * get_final_extent(a1fs_inode * file_inode, fs_ctx *fs);
uint32_t extend_extent(uint32_t max_blocks, a1fs_inode *inode, a1fs_extent *extent, fs_ctx *fs);
long allocate_extent(uint32_t max_blocks, a1fs_inode *inode, fs_ctx *fs);
int deallocate_block(a1fs_inode *inode, fs_ctx *fs);

char* get_last_component(const char *abs_path);
void set_parent_path(char *path);
long allocate_inode(fs_ctx *fs);
long allocate_block(fs_ctx *fs);
void set_bitmap(uint32_t bitmap_block, uint32_t offset, fs_ctx *fs , bool set);
