#include "fs_ctx.h"

int find_dir_entry(int inode_num, char *target_name, fs_ctx *fs);
int path_lookup(const char *path, fs_ctx *fs);