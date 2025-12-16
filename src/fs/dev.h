#ifndef DEV_H
#define DEV_H

#include "vfs.h"

uint64_t stdout_write(vfs_node_t* node, uint64_t offset, uint64_t size, uint8_t* buf);
uint64_t stdout_read(vfs_node_t* node, uint64_t offset, uint64_t size, uint8_t* buf);
uint64_t stdin_read(vfs_node_t* node, uint64_t offset, uint64_t size, uint8_t* buf);
uint64_t stdin_write(vfs_node_t* node, uint64_t offset, uint64_t size, uint8_t* buff);

void dev_init_stdio(void);
void dev_attach_stdio(file_handle_t** fd_tbl);

#endif