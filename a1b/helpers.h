#include "fs_ctx.h"

uint32_t find_dir_entry( uint32_t inode_num, char *target_name, fs_ctx *fs);
uint32_t path_lookup(const char *path, fs_ctx *fs);