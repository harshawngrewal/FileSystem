
#include <stdio.h>
#include "a1fs.h"

int main(){

  printf("%ld, %d,  %ld", sizeof(a1fs_inode), A1FS_BLOCK_SIZE, A1FS_BLOCK_SIZE % sizeof(a1fs_inode) );
  return 0;
}

