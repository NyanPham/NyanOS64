#ifndef TAR_FS_H
#define TAR_FS_H

#include "vfs.h"
#include "tar.h"
#include <stdint.h>

vfs_node_t* tar_fs_init(void* tar_addr);
uint64_t tar_vfs_read(vfs_node_t* node, uint64_t offset, uint64_t size, uint8_t* buffer);
vfs_node_t* tar_vfs_finddir(vfs_node_t* node, const char* name);

#endif