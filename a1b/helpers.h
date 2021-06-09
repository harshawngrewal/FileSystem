#include "fs_ctx.h"

uint32_t ceil_integer_division(uint32_t num1, uint32_t num2);
uint32_t min(uint32_t num1, uint32_t num2);
uint32_t max(uint32_t num1, uint32_t num2);
long find_dir_entry(uint32_t inode_num, char *target_name, fs_ctx *fs);
long path_lookup(const char *path, fs_ctx *fs);
uint32_t extend_extent(a1fs_extent *extent, fs_ctx *fs);
uint32_t allocate_extent(uint32_t max_blocks, fs_ctx *fs);
