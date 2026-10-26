#include "fat32.h"
#include "drivers/ata.h"
#include "string.h"
#include "fs/vfs.h"
#include "mem/kmalloc.h"
#include "mem/vmm.h"
#include "drivers/serial.h"
#include "utils/math.h"

#define EOC 0x0FFFFFF8
#define FAT_CACHE_SIZE 64

typedef struct
{
    uint32_t lba;
    uint32_t last_used;
    uint8_t valid;
    uint8_t data[SECTOR_SIZE];
} FatCacheEntry;

static FatCacheEntry g_fat_cache[FAT_CACHE_SIZE];
static uint32_t g_fat_cache_tick = 0;

static fat32_bpb g_bpb;
static uint32_t g_data_start_lba;
static uint32_t g_partition_lba;
static uint8_t g_drive_sel;
static uint64_t g_bytes_per_cluster;

static void fat_cache_invalidate(void)
{
    for (int i = 0; i < FAT_CACHE_SIZE; i++)
    {
        g_fat_cache[i].valid = 0;
    }
}

static void fat_cache_read_sector(uint32_t lba, uint8_t *out_buf)
{
    g_fat_cache_tick++;
    for (int i = 0; i < FAT_CACHE_SIZE; i++)
    {
        if (g_fat_cache[i].valid && g_fat_cache[i].lba == lba)
        {
            g_fat_cache[i].last_used = g_fat_cache_tick;
            memcpy(out_buf, g_fat_cache[i].data, SECTOR_SIZE);
            return;
        }
    }

    int victim = 0;
    for (int i = 0; i < FAT_CACHE_SIZE; i++)
    {
        if (!g_fat_cache[i].valid)
        {
            victim = i;
            break;
        }
        if (g_fat_cache[i].last_used < g_fat_cache[victim].last_used)
        {
            victim = i;
        }
    }

    ata_read_sectors((uint16_t *)g_fat_cache[victim].data, lba, 1, g_drive_sel);
    g_fat_cache[victim].lba = lba;
    g_fat_cache[victim].last_used = g_fat_cache_tick;
    g_fat_cache[victim].valid = 1;
    memcpy(out_buf, g_fat_cache[victim].data, SECTOR_SIZE);
}

static void fat_cache_write_sector(uint32_t lba, const uint8_t *in_buf)
{
    g_fat_cache_tick++;
    int slot = -1;
    for (int i = 0; i < FAT_CACHE_SIZE; i++)
    {
        if (g_fat_cache[i].valid && g_fat_cache[i].lba == lba)
        {
            slot = i;
            break;
        }
    }
    if (slot >= 0)
    {
        memcpy(g_fat_cache[slot].data, in_buf, SECTOR_SIZE);
        g_fat_cache[slot].last_used = g_fat_cache_tick;
    }
    ata_write_sectors((uint16_t *)in_buf, lba, 1, g_drive_sel);
}

static inline uint32_t fat32_cluster_to_lba(uint32_t cluster)
{
    if (cluster < 2)
    {
        return g_data_start_lba;
    }
    return g_data_start_lba + (cluster - 2) * (uint32_t)g_bpb.sectors_per_cluster;
}

extern vfs_fs_ops_t fat32_ops;

static vfs_node_t *fat32_finddir(vfs_node_t *node, const char *name)
{
    fat32_node_data *node_data = (fat32_node_data *)node->device_data;
    uint32_t cluster = node_data->first_cluster;

    DirectoryEntry dir_entry;
    fat32_location_t loc;

    if (fat32_find_file(cluster, name, &dir_entry, &loc) < 0)
    {
        return NULL;
    }

    vfs_node_t *new_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (new_node == NULL)
    {
        kprint("FAT32_FINDDIR failed: OOM\n");
        return NULL;
    }

    fat32_node_data *new_node_data = (fat32_node_data *)kmalloc(sizeof(fat32_node_data));
    if (new_node_data == NULL)
    {
        kprint("FAT32_FINDDIR failed: OOM\n");
        kfree(new_node);
        return NULL;
    }

    strcpy(new_node->name, name);
    new_node->length = dir_entry.file_size;
    new_node->flags = ((dir_entry.attributes & 0x10)
                           ? VFS_DIRECTORY
                           : VFS_FILE) |
                      VFS_NODE_AUTOFREE;

    new_node_data->first_cluster = (dir_entry.first_cluster_high << 0x10) | dir_entry.first_cluster_low;
    new_node_data->sector_lba = loc.sector_lba;
    new_node_data->offset = loc.offset;
    new_node->device_data = new_node_data;
    new_node->ops = &fat32_ops;

    return new_node;
}

static uint64_t fat32_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (offset >= node->length)
    {
        return 0;
    }

    if (offset + size > node->length)
    {
        size = node->length - offset;
    }

    fat32_node_data *node_data = (fat32_node_data *)(node->device_data);
    uint32_t curr_cluster = node_data->first_cluster;
    if (curr_cluster < 2)
    {
        return 0;
    }

    for (uint64_t i = 0; i < offset / g_bytes_per_cluster; i++)
    {
        curr_cluster = fat32_read_fat(curr_cluster);
        if (curr_cluster >= EOC)
        {
            return 0;
        }
    }

    uint32_t read_offset = offset % g_bytes_per_cluster;

    uint8_t *tmp_buf = (uint8_t *)kmalloc(g_bytes_per_cluster);
    if (tmp_buf == NULL)
    {
        kprint("FAT32_READ failed: OOM\n");
        return 0;
    }

    uint64_t rem_size = size;
    while (rem_size > 0 && curr_cluster >= 2 && curr_cluster < EOC)
    {
        uint64_t copy_size = uint64_min(rem_size, g_bytes_per_cluster - read_offset);
        uint32_t lba = fat32_cluster_to_lba(curr_cluster);
        ata_read_sectors((uint16_t *)tmp_buf, lba, g_bpb.sectors_per_cluster, g_drive_sel);
        memcpy(buffer, tmp_buf + read_offset, copy_size);
        buffer += copy_size;
        rem_size -= copy_size;
        read_offset = 0;
        if (rem_size > 0)
        {
            curr_cluster = fat32_read_fat(curr_cluster);
        }
    }

    kfree(tmp_buf);
    return size - rem_size;
}

static void fat32_update_size(vfs_node_t *node, uint64_t new_size)
{
    fat32_node_data *node_data = (fat32_node_data *)(node->device_data);

    uint8_t tmp_buf[SECTOR_SIZE];
    memset(tmp_buf, 0, SECTOR_SIZE);

    ata_read_sectors((uint16_t *)tmp_buf, node_data->sector_lba, 1, g_drive_sel);
    DirectoryEntry *dir_entry = (DirectoryEntry *)(tmp_buf + node_data->offset);
    dir_entry->file_size = (uint32_t)new_size;
    ata_write_sectors((uint16_t *)tmp_buf, node_data->sector_lba, 1, g_drive_sel);
    node->length = new_size;
}

static void fat32_truncate(vfs_node_t *node)
{
    fat32_node_data *node_data = (fat32_node_data *)(node->device_data);
    if (node_data == NULL)
    {
        return;
    }

    uint32_t curr_cluster = node_data->first_cluster;
    while (curr_cluster >= 2 && curr_cluster < EOC)
    {
        uint32_t next_cluster = fat32_read_fat(curr_cluster);
        fat32_write_fat_entry(curr_cluster, 0);
        curr_cluster = next_cluster;
    }

    node_data->first_cluster = 0;
    node->length = 0;

    uint8_t tmp_buf[SECTOR_SIZE];
    ata_read_sectors((uint16_t *)tmp_buf, node_data->sector_lba, 1, g_drive_sel);
    DirectoryEntry *dirent = (DirectoryEntry *)(tmp_buf + node_data->offset);
    dirent->first_cluster_high = 0;
    dirent->first_cluster_low = 0;
    dirent->file_size = 0;
    ata_write_sectors((uint16_t *)tmp_buf, node_data->sector_lba, 1, g_drive_sel);
}

static uint64_t fat32_write(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (size == 0)
    {
        return 0;
    }

    fat32_node_data *node_data = (fat32_node_data *)(node->device_data);

    if (offset + size > node->length || node_data->first_cluster == 0)
    {
        uint8_t tmp_buf[SECTOR_SIZE];

        if (node_data->first_cluster == 0)
        {
            int64_t free_cluster_id = fat32_find_free_cluster();
            if (free_cluster_id < 0)
            {
                kprint("FAT32_WRITE failed: No free cluster id found!\n");
                return 0;
            }

            ata_read_sectors((uint16_t *)tmp_buf, node_data->sector_lba, 1, g_drive_sel);
            DirectoryEntry *dirent = (DirectoryEntry *)(tmp_buf + node_data->offset);
            dirent->first_cluster_high = (free_cluster_id >> 16) & 0xFFFF;
            dirent->first_cluster_low = free_cluster_id & 0xFFFF;
            ata_write_sectors((uint16_t *)tmp_buf, node_data->sector_lba, 1, g_drive_sel);

            fat32_write_fat_entry(free_cluster_id, EOC);
            node_data->first_cluster = free_cluster_id;
        }

        uint32_t curr_cluster = node_data->first_cluster;
        uint32_t prev_cluster = 0;
        uint32_t cluster_count = 0;
        while (curr_cluster >= 2 && curr_cluster < EOC)
        {
            prev_cluster = curr_cluster;
            curr_cluster = fat32_read_fat(curr_cluster);
            cluster_count++;
        }

        if (prev_cluster < 2)
        {
            kprint("FAT32_WRITE failed: failed trace last cluster of a file\n");
            return 0;
        }

        uint64_t occupied_cluster_bytes = (uint64_t)cluster_count * g_bytes_per_cluster;
        if ((offset + size) > occupied_cluster_bytes)
        {
            uint64_t size_left = (offset + size) - occupied_cluster_bytes;
            uint64_t cluster_count_left = (size_left + g_bytes_per_cluster - 1) / g_bytes_per_cluster;

            for (uint64_t i = 0; i < cluster_count_left; i++)
            {
                int64_t free_cluster_id = fat32_find_free_cluster();
                if (free_cluster_id < 0)
                {
                    kprint("FAT32_WRITE failed: No free cluster id found!\n");
                    return 0;
                }

                fat32_write_fat_entry(prev_cluster, free_cluster_id);
                fat32_write_fat_entry(free_cluster_id, EOC);
                prev_cluster = free_cluster_id;
            }
        }
    }

    uint32_t curr_cluster = node_data->first_cluster;
    for (uint64_t i = 0; i < offset / g_bytes_per_cluster; i++)
    {
        curr_cluster = fat32_read_fat(curr_cluster);
    }

    uint32_t read_offset = offset % g_bytes_per_cluster;

    uint8_t *tmp_buf = (uint8_t *)kmalloc(g_bytes_per_cluster);
    if (tmp_buf == NULL)
    {
        kprint("FAT32_WRITE failed: OOM\n");
        return 0;
    }
    memset(tmp_buf, 0, g_bytes_per_cluster);

    uint64_t rem_size = size;
    while (rem_size > 0 && curr_cluster >= 2 && curr_cluster < EOC)
    {
        uint64_t copy_size = uint64_min(rem_size, g_bytes_per_cluster - read_offset);
        uint32_t lba = fat32_cluster_to_lba(curr_cluster);
        ata_read_sectors((uint16_t *)tmp_buf, lba, g_bpb.sectors_per_cluster, g_drive_sel);
        memcpy(tmp_buf + read_offset, buffer, copy_size);
        ata_write_sectors((uint16_t *)tmp_buf, lba, g_bpb.sectors_per_cluster, g_drive_sel);
        buffer += copy_size;
        rem_size -= copy_size;
        read_offset = 0;
        if (rem_size > 0)
        {
            curr_cluster = fat32_read_fat(curr_cluster);
        }
    }

    kfree(tmp_buf);

    if (offset + size > node->length)
    {
        fat32_update_size(node, offset + size);
    }

    return size - rem_size;
}

vfs_fs_ops_t fat32_ops = {
    .read = fat32_read,
    .write = fat32_write,
    .finddir = fat32_finddir,
    .readdir = fat32_readdir,
    .open = NULL,
    .close = NULL,
    .create = fat32_create,
    .unlink = fat32_unlink,
    .truncate = fat32_truncate,
};

uint32_t fat32_read_fat(uint32_t cluster)
{
    uint32_t fat_offset = cluster * sizeof(uint32_t);
    uint32_t fat_sector = g_partition_lba + g_bpb.reserved_sectors + (fat_offset / g_bpb.bytes_per_sector);
    uint32_t ent_offset = fat_offset % g_bpb.bytes_per_sector;

    uint8_t tmp_buf[SECTOR_SIZE];
    fat_cache_read_sector(fat_sector, tmp_buf);

    return *((uint32_t *)(tmp_buf + ent_offset)) & 0x0FFFFFFF;
}

vfs_node_t *fat32_init_fs(uint32_t partition_lba, uint8_t drive_sel)
{
    g_drive_sel = drive_sel;
    g_partition_lba = partition_lba;
    fat_cache_invalidate();

    uint16_t tmp_buf[SECTOR_SIZE / sizeof(uint16_t)];
    ata_read_sectors(tmp_buf, partition_lba, 1, g_drive_sel);
    memcpy(&g_bpb, tmp_buf, sizeof(fat32_bpb));

    uint32_t fats_region = (uint32_t)g_bpb.fats_num * g_bpb.sectors_per_fat32;
    g_data_start_lba = partition_lba + (uint32_t)g_bpb.reserved_sectors + fats_region;
    g_bytes_per_cluster = g_bpb.sectors_per_cluster * g_bpb.bytes_per_sector;

    vfs_node_t *root = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (root == NULL)
    {
        return NULL;
    }
    fat32_node_data *node_data = (fat32_node_data *)kmalloc(sizeof(fat32_node_data));
    if (node_data == NULL)
    {
        kfree(root);
        return NULL;
    }

    strcpy(root->name, "fat32_root");
    root->flags = VFS_DIRECTORY;
    root->length = 0;
    node_data->first_cluster = g_bpb.root_cluster;
    root->device_data = node_data;
    root->ops = &fat32_ops;

    return root;
}

int list_cb(DirectoryEntry *dir_entry, fat32_location_t *loc, void *ctx)
{
    (void)loc;
    (void)ctx;
    char name[13] = {0};
    int j = 0;
    while (j < 8 && dir_entry->name[j] != ' ')
    {
        name[j] = dir_entry->name[j];
        j++;
    }
    if (dir_entry->ext[0] != ' ')
    {
        name[j++] = '.';

        int k = 0;
        while (k < 3 && dir_entry->ext[k] != ' ')
        {
            name[j++] = dir_entry->ext[k++];
        }
    }

    kprint(name);
    kprint("\n");
    return 0;
}

void fat32_list_root(void)
{
    fat32_iterate(g_bpb.root_cluster, list_cb, NULL);
}

int fat32_parse_name(const char *fname, char *out_name, char *out_ext)
{
    size_t name_len = strlen(fname);
    if (name_len > 12)
    {
        kprint("FAT32_FIND_FILE: invalid name.txt length!");
        return -1;
    }

    memset(out_name, ' ', 8);
    memset(out_ext, ' ', 3);

    uint8_t is_ext = 0;
    size_t i = 0;
    size_t j = 0;
    for (i = 0; i < name_len; i++)
    {
        char c = fname[i];

        if (c == '.')
        {
            if (is_ext)
            {
                kprint("FAT32_FIND_FILE: invalid name.ext");
                return -1;
            }
            is_ext = 1;
            continue;
        }
        if (c >= 'a' && c <= 'z')
        {
            c &= ~0x20;
        }

        if (!is_ext && i >= 8)
        {
            kprint("FAT32_FIND_FILE: name is longer than 8 characters!");
            return -1;
        }

        if (is_ext)
        {
            if (j >= 3)
            {
                kprint("FAT32_FIND_FILE: ext is longer than 3 characters!");
                return -1;
            }
            out_ext[j] = c;
            j++;
        }
        else
        {
            out_name[i] = c;
        }
    }

    return 0;
}

int find_cb(DirectoryEntry *dir_entry, fat32_location_t *loc, void *ctx)
{
    find_control_t *find_control = (find_control_t *)ctx;
    if ((strncmp(dir_entry->name, find_control->name, 8) == 0) && (strncmp(dir_entry->ext, find_control->ext, 3) == 0))
    {
        memcpy(find_control->out_entry, dir_entry, sizeof(DirectoryEntry));
        if (find_control->out_loc != NULL)
        {
            memcpy(find_control->out_loc, loc, sizeof(fat32_location_t));
        }
        return 1;
    }
    return 0;
}

int fat32_find_file(uint32_t cluster, const char *name, DirectoryEntry *out_entry, fat32_location_t *out_loc)
{
    char target_name[8] = {0};
    char target_ext[3] = {0};
    if (fat32_parse_name(name, target_name, target_ext) < 0)
    {
        kprint("FAT32_FIND_FILE failed: failed to parse name!");
        return -1;
    }

    find_control_t find_control = {
        .name = target_name,
        .ext = target_ext,
        .out_entry = out_entry,
        .out_loc = out_loc,
    };

    int res = fat32_iterate(cluster, find_cb, &find_control);
    if (res > 0)
    {
        return 0;
    }

    if (res < 0)
    {
        kprint("Error occurs to find a file\n");
    }

    return -1;
}

int fat32_iterate(uint32_t cluster, fat32_entry_cb_t entry_cb, void *ctx)
{
    uint32_t curr_cluster = cluster;

    while (curr_cluster < EOC)
    {
        uint32_t root_lba = fat32_cluster_to_lba(curr_cluster);
        uint16_t tmp_buf[g_bpb.bytes_per_sector / 2];

        for (int i = 0; i < g_bpb.sectors_per_cluster; i++)
        {
            uint32_t curr_lba = root_lba + i;
            ata_read_sectors(tmp_buf, curr_lba, 1, g_drive_sel);

            DirectoryEntry *dir_entries = (DirectoryEntry *)tmp_buf;
            for (size_t j = 0; j < g_bpb.bytes_per_sector / sizeof(DirectoryEntry); j++)
            {
                DirectoryEntry *dir_entry = &dir_entries[j];
                if (dir_entry->name[0] == '\0')
                {
                    return 0;
                }

                if ((uint8_t)dir_entry->name[0] == 0xE5)
                {
                    continue;
                }

                fat32_location_t loc = {
                    .sector_lba = curr_lba,
                    .offset = j * sizeof(DirectoryEntry),
                };
                int res = entry_cb(dir_entry, &loc, ctx);
                if (res < 0)
                {
                    kprint("ERROR IN fat32_iterate after calling entry_cb\n");
                    return -1;
                }
                else if (res > 0)
                {
                    return res;
                }
            }
        }

        curr_cluster = fat32_read_fat(curr_cluster);
    }

    return 0;
}

uint8_t *fat32_read_file(DirectoryEntry *entry)
{
    uint8_t *buf = (uint8_t *)kmalloc(entry->file_size + 1);
    if (buf == NULL)
    {
        return NULL;
    }

    uint32_t curr_cluster = (entry->first_cluster_high << 0x10) | entry->first_cluster_low;
    uint8_t *buf_cur = buf;

    while (curr_cluster < EOC)
    {
        uint32_t lba = fat32_cluster_to_lba(curr_cluster);
        ata_read_sectors((uint16_t *)buf_cur, lba, g_bpb.sectors_per_cluster, g_drive_sel);
        buf_cur += g_bpb.bytes_per_sector * g_bpb.sectors_per_cluster;
        curr_cluster = fat32_read_fat(curr_cluster);
    }

    buf[entry->file_size] = '\0';
    return buf;
}

typedef struct
{
    uint32_t tgt_idx;
    uint32_t curr_idx;
    dirent_t *out;
} readdir_ctx;

int readdir_cb(DirectoryEntry *entry, fat32_location_t *loc, void *p)
{
    (void)loc;
    readdir_ctx *ctx = (readdir_ctx *)p;

    if (entry->attributes == 0x0F || entry->attributes == 0x08)
    {
        return 0;
    }

    if (ctx->curr_idx == ctx->tgt_idx)
    {
        int j = 0;
        int k = 0;
        while (k < 8 && entry->name[k] != ' ')
        {
            ctx->out->name[j++] = entry->name[k++];
        }

        if (entry->ext[0] != ' ')
        {
            ctx->out->name[j++] = '.';
            k = 0;
            while (k < 3 && entry->ext[k] != ' ')
            {
                ctx->out->name[j++] = entry->ext[k++];
            }
        }
        ctx->out->name[j] = '\0';
        ctx->out->type = (entry->attributes & 0x10)
                             ? VFS_DIRECTORY
                             : VFS_FILE;

        ctx->out->size = entry->file_size;
        return 1;
    }
    ctx->curr_idx++;
    return 0;
}

int fat32_readdir(vfs_node_t *node, uint32_t idx, dirent_t *out)
{
    fat32_node_data *data = (fat32_node_data *)node->device_data;

    readdir_ctx ctx;
    ctx.tgt_idx = idx;
    ctx.curr_idx = 0;
    ctx.out = out;

    if (fat32_iterate(data->first_cluster, readdir_cb, &ctx) == 1)
    {
        return 0;
    }

    return -1;
}

vfs_node_t *fat32_create(vfs_node_t *parent, const char *fname, uint32_t flags)
{
    char name[8];
    char ext[3];
    fat32_location_t loc;

    fat32_node_data *parent_data = (fat32_node_data *)parent->device_data;
    if (parent_data == NULL)
    {
        kprint("FAT32_CREATE failed: Parent Data is NULL!\n");
        return NULL;
    }

    DirectoryEntry exist_entry;
    if (fat32_find_file(parent_data->first_cluster, fname, &exist_entry, NULL) == 0)
    {
        kprint("FAT32_CREATE failed: File already exists!\n");
        return NULL;
    }

    if (fat32_parse_name(fname, name, ext) < 0)
    {
        return NULL;
    }

    int64_t free_cluster_id = fat32_find_free_cluster();
    if (free_cluster_id < 0)
    {
        kprint("FAT32_CREATE failed: No free cluster id found!\n");
        return NULL;
    }

    if (fat32_find_free_directory_entry(parent_data->first_cluster, &loc) < 0)
    {
        return NULL;
    }

    uint8_t *tmp_buf = (uint8_t *)kmalloc(SECTOR_SIZE);
    if (tmp_buf == NULL)
    {
        kprint("FAT32_CREATE: OOM\n");
        return NULL;
    }
    memset(tmp_buf, 0, SECTOR_SIZE);

    DirectoryEntry new_entry;
    memset(&new_entry, 0, sizeof(DirectoryEntry));
    strncpy(new_entry.name, name, 8);
    strncpy(new_entry.ext, ext, 3);
    new_entry.attributes = 0x20;
    new_entry.file_size = 0;
    new_entry.first_cluster_high = (free_cluster_id >> 16) & 0xFFFF;
    new_entry.first_cluster_low = free_cluster_id & 0xFFFF;

    if (flags & VFS_DIRECTORY)
    {
        new_entry.attributes = 0x10;

        DirectoryEntry dot_entry;
        memset(&dot_entry, 0, sizeof(DirectoryEntry));

        strncpy(dot_entry.name, ".       ", 8);
        strncpy(dot_entry.ext, "   ", 3);
        dot_entry.attributes = 0x10;
        dot_entry.first_cluster_high = (free_cluster_id >> 16) & 0xFFFF;
        dot_entry.first_cluster_low = free_cluster_id & 0xFFFF;

        DirectoryEntry two_dot_entry;
        memset(&two_dot_entry, 0, sizeof(DirectoryEntry));

        strncpy(two_dot_entry.name, "..      ", 8);
        strncpy(two_dot_entry.ext, "   ", 3);
        two_dot_entry.attributes = 0x10;
        two_dot_entry.first_cluster_high = (parent_data->first_cluster >> 16) & 0xFFFF;
        two_dot_entry.first_cluster_low = parent_data->first_cluster & 0xFFFF;

        memcpy(tmp_buf, &dot_entry, sizeof(DirectoryEntry));
        memcpy(tmp_buf + sizeof(DirectoryEntry), &two_dot_entry, sizeof(DirectoryEntry));

        uint32_t lba = fat32_cluster_to_lba(free_cluster_id);
        ata_write_sectors((uint16_t *)tmp_buf, lba, 1, g_drive_sel);
    }

    ata_read_sectors((uint16_t *)tmp_buf, loc.sector_lba, 1, g_drive_sel);
    memcpy(&tmp_buf[loc.offset], &new_entry, sizeof(DirectoryEntry));
    ata_write_sectors((uint16_t *)tmp_buf, loc.sector_lba, 1, g_drive_sel);

    fat32_write_fat_entry(free_cluster_id, EOC);

    kfree(tmp_buf);

    vfs_node_t *new_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (new_node == NULL)
    {
        return NULL;
    }

    memset(new_node, 0, sizeof(vfs_node_t));
    strncpy(new_node->name, fname, 127);
    new_node->flags = flags | VFS_NODE_AUTOFREE;
    new_node->length = 0;
    new_node->ops = &fat32_ops;

    fat32_node_data *new_data = (fat32_node_data *)kmalloc(sizeof(fat32_node_data));
    if (new_data == NULL)
    {
        kfree(new_node);
        return NULL;
    }
    new_data->first_cluster = free_cluster_id;
    new_data->sector_lba = loc.sector_lba;
    new_data->offset = loc.offset;
    new_node->device_data = new_data;

    return new_node;
}

void fat32_write_fat_entry(uint32_t cluster, uint32_t value)
{
    uint32_t fat_start_lba = g_partition_lba + g_bpb.reserved_sectors;

    uint32_t fat_sector_offset = (cluster * 4) / SECTOR_SIZE;
    uint32_t entry_offset = (cluster * 4) % SECTOR_SIZE;
    uint32_t target_lba = fat_start_lba + fat_sector_offset;

    uint8_t tmp_buf[SECTOR_SIZE] = {0};
    fat_cache_read_sector(target_lba, tmp_buf);

    *(uint32_t *)&tmp_buf[entry_offset] = value;

    fat_cache_write_sector(target_lba, tmp_buf);
}

int64_t fat32_find_free_cluster(void)
{
    uint8_t tmp_buf[SECTOR_SIZE] = {0};
    for (uint32_t i = 0; i < g_bpb.sectors_per_fat32; i++)
    {
        uint32_t lba = g_partition_lba + g_bpb.reserved_sectors + i;
        fat_cache_read_sector(lba, tmp_buf);

        for (uint32_t j = 0; j < SECTOR_SIZE / sizeof(uint32_t); j++)
        {
            uint32_t cluster_id = i * 128 + j;
            if (i == 0 && cluster_id < 2)
            {
                continue;
            }

            if ((((uint32_t *)tmp_buf)[j] & 0x0FFFFFFF) == 0)
            {
                return cluster_id;
            }
        }
    }
    return -1;
}

int fat32_find_free_directory_entry(uint32_t dir_cluster, fat32_location_t *out_loc)
{
    uint32_t curr_cluster = dir_cluster;

    while (curr_cluster < EOC)
    {
        uint32_t root_lba = fat32_cluster_to_lba(curr_cluster);
        uint16_t tmp_buf[g_bpb.bytes_per_sector / 2];

        for (int i = 0; i < g_bpb.sectors_per_cluster; i++)
        {
            uint32_t curr_lba = root_lba + i;
            ata_read_sectors(tmp_buf, curr_lba, 1, g_drive_sel);

            DirectoryEntry *dir_entries = (DirectoryEntry *)tmp_buf;
            for (size_t j = 0; j < g_bpb.bytes_per_sector / sizeof(DirectoryEntry); j++)
            {
                DirectoryEntry *dir_entry = &dir_entries[j];
                uint8_t c = (uint8_t)dir_entry->name[0];
                if (c == 0x00 || c == 0xE5)
                {
                    out_loc->sector_lba = curr_lba;
                    out_loc->offset = j * sizeof(DirectoryEntry);
                    return 0;
                }
            }
        }

        curr_cluster = fat32_read_fat(curr_cluster);
    }

    return -1;
}

void fat32_unlink(vfs_node_t *node)
{
    fat32_node_data *node_data = (fat32_node_data *)node->device_data;
    uint8_t *tmp_buf = (uint8_t *)kmalloc(SECTOR_SIZE);
    ata_read_sectors((uint16_t *)tmp_buf, node_data->sector_lba, 1, g_drive_sel);
    ((DirectoryEntry *)(tmp_buf + node_data->offset))->name[0] = 0xE5;
    ata_write_sectors((uint16_t *)tmp_buf, node_data->sector_lba, 1, g_drive_sel);
    kfree(tmp_buf);

    uint32_t curr_cluster = node_data->first_cluster;

    while (curr_cluster >= 2 && curr_cluster < EOC)
    {
        uint32_t next_cluster = fat32_read_fat(curr_cluster);
        fat32_write_fat_entry(curr_cluster, 0);
        curr_cluster = next_cluster;
    }
}