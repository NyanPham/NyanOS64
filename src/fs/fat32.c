#include "fat32.h"
#include "drivers/ata.h"
#include "string.h"
#include "fs/vfs.h"
#include "mem/kmalloc.h"
#include "mem/vmm.h"
#include "drivers/serial.h"
#include "utils/math.h"

#define EOC 0x0FFFFFF8

static fat32_bpb g_bpb;
static uint32_t data_start_lba;
static uint8_t g_drive_sel;
static uint64_t g_bytes_per_cluster;

// According to FAT32, Cluster 2 is the first cluster
static inline uint32_t fat32_cluster_to_lba(uint32_t cluster)
{
    if (cluster < 2)
    {
        // TODO: Panic
        return data_start_lba;
    }

    return data_start_lba + (cluster - 2) * (uint32_t)g_bpb.sectors_per_cluster;
}

/* START: VFS */
typedef struct
{
    uint32_t first_cluster;
} fat32_node_data;

extern vfs_fs_ops_t fat32_ops;

static vfs_node_t *fat32_finddir(vfs_node_t *node, const char *name)
{
    fat32_node_data *node_data = (fat32_node_data *)node->device_data;
    uint32_t cluster = node_data->first_cluster;

    DirectoryEntry dir_entry;
    if (fat32_find_file(cluster, name, &dir_entry) < 0)
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
    new_node->flags = (dir_entry.attributes & 0x10)
                          ? VFS_DIRECTORY
                          : VFS_FILE;

    new_node_data->first_cluster = (dir_entry.first_cluster_high << 0x10) | dir_entry.first_cluster_low;
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

    if (offset + size >= node->length)
    {
        size = node->length - offset;
    }

    fat32_node_data *node_data = (fat32_node_data *)(node->device_data);
    uint32_t curr_cluster = node_data->first_cluster;

    for (int i = 0; i < offset / g_bytes_per_cluster; i++)
    {
        curr_cluster = fat32_read_fat(curr_cluster);
    }

    uint32_t read_offset = offset % g_bytes_per_cluster;

    uint8_t *tmp_buf = (uint8_t *)vmm_alloc(g_bytes_per_cluster);
    if (tmp_buf == NULL)
    {
        kprint("FAT32_READ failed: OOM\n");
        return 0;
    }

    uint64_t rem_size = size;
    while (rem_size > 0)
    {
        uint64_t copy_size = uint64_min(rem_size, g_bytes_per_cluster - read_offset);
        uint32_t lba = fat32_cluster_to_lba(curr_cluster);
        ata_read_sectors(tmp_buf, lba, g_bpb.sectors_per_cluster, g_drive_sel);
        memcpy(buffer, tmp_buf + read_offset, copy_size);
        buffer += copy_size;
        rem_size -= copy_size;
        read_offset = 0;
        curr_cluster = fat32_read_fat(curr_cluster);
    }

    vmm_free(tmp_buf);
    return size;
}

vfs_fs_ops_t fat32_ops = {
    .read = fat32_read,
    .write = NULL, // todo
    .finddir = fat32_finddir,
    .open = NULL,
    .close = NULL,
    .create = NULL, // todo
};

/* END: VFS */

uint32_t fat32_read_fat(uint32_t cluster)
{
    uint32_t fat_offset = cluster * sizeof(uint32_t);
    uint32_t fat_sector = g_bpb.reserved_sectors + (fat_offset / g_bpb.bytes_per_sector);
    uint32_t ent_offset = fat_offset % g_bpb.bytes_per_sector;

    uint16_t tmp_buf[SECTOR_SIZE / 2];
    ata_read_sectors(tmp_buf, fat_sector, 1, g_drive_sel);

    return *((uint32_t *)((uint8_t *)tmp_buf + ent_offset)) & 0x0FFFFFFF;
}

vfs_node_t *fat32_init_fs(uint32_t partition_lba, uint8_t drive_sel)
{
    // our purpose is to find the data region lba.
    // reserved region is where bpb is stored
    // we have fat region (FAT tables) = fats_num * sectors_per_fat32
    // data region (clusters, including root directory), is right after FAT region.

    g_drive_sel = drive_sel;
    uint16_t tmp_buf[SECTOR_SIZE / sizeof(uint16_t)];
    ata_read_sectors(tmp_buf, partition_lba, 1, g_drive_sel);
    memcpy(&g_bpb, tmp_buf, sizeof(fat32_bpb));

    uint32_t fats_region = (uint32_t)g_bpb.fats_num * g_bpb.sectors_per_fat32;
    data_start_lba = partition_lba + (uint32_t)g_bpb.reserved_sectors + fats_region;
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

int list_cb(DirectoryEntry *dir_entry, void *ctx)
{
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
    return 0;
}

void fat32_list_root()
{
    fat32_iterate(g_bpb.root_cluster, list_cb, NULL);
}

/**
 * Normalize a file name to separated name and ext.
 * For example: HELLO.TXT -> name = HELLO, ext = TXT
 */
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
    int i = 0; // index for name
    int j = 0; // index for ext
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
            c &= ~0x20; // capitalize
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

int find_cb(DirectoryEntry *dir_entry, void *ctx)
{
    find_control_t *find_control = (find_control_t *)ctx;
    if ((strncmp(dir_entry->name, find_control->name, 8) == 0) && (strncmp(dir_entry->ext, find_control->ext, 3) == 0))
    {
        memcpy(find_control->out_entry, dir_entry, sizeof(DirectoryEntry));
        return 1;
    }
    return 0;
}

/**
 * @brief Finds a file in FAT32 format drive
 * returns 0 if file found
 * returns -1 otherwise
 */
int fat32_find_file(uint32_t cluster, const char *name, DirectoryEntry *out_entry)
{
    // First, let's normalize the name into the dir_entry->name and dir_entry->ext
    char target_name[8] = {0};
    char target_ext[3] = {0};
    if (fat32_parse_name(name, target_name, target_ext) < 0)
    {
        kprint("FAT32_FIND_FILE failed: failed to parse name!");
        return -1;
    }

    // Then, scan the Root Directory, compare the target with the entry if they have
    // the same name and ext.
    // If found, copy the entry data to out_entry, and return 0;
    // Otherwise, not found, return -1.
    find_control_t find_control = {
        .name = target_name,
        .ext = target_ext,
        .out_entry = out_entry,
    };

    int res = fat32_iterate(cluster, find_cb, &find_control);
    if (res > 0)
    {
        kprint("Found a file\n");
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

            DirectoryEntry *dir_entries = tmp_buf;
            for (int j = 0; j < g_bpb.bytes_per_sector / sizeof(DirectoryEntry); j++)
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

                int res = entry_cb(dir_entry, ctx);
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
    uint8_t *buf = (uint8_t *)vmm_alloc(entry->file_size + 1);
    if (buf == NULL)
    {
        return NULL;
    }

    uint32_t curr_cluster = (entry->first_cluster_high << 0x10) | entry->first_cluster_low;
    uint8_t *buf_cur = buf;

    while (curr_cluster < EOC)
    {
        uint32_t lba = fat32_cluster_to_lba(curr_cluster);
        ata_read_sectors(buf_cur, lba, g_bpb.sectors_per_cluster, g_drive_sel);
        buf_cur += g_bpb.bytes_per_sector * g_bpb.sectors_per_cluster;
        curr_cluster = fat32_read_fat(curr_cluster);
    }

    buf[entry->file_size] = '\0';

    return buf;
}