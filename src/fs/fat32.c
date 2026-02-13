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
static uint32_t g_data_start_lba;
static uint32_t g_partition_lba;
static uint8_t g_drive_sel;
static uint64_t g_bytes_per_cluster;

/**
 * @brief Converts a Cluster Number to a Physical LBA (Logical Block Address).
 *
 * @details MECHANISM:
 * The Data Region starts at `g_data_start_lba`. However, in FAT32, the first
 * valid cluster for data is Cluster #2. Clusters 0 and 1 are reserved.
 * Therefore, to find the physical location of Cluster N, we must subtract 2
 * before multiplying by the cluster size.
 *
 * Formula: LBA = Start + (Cluster - 2) * SectorsPerCluster
 *
 * @param cluster The cluster number to convert.
 * @return uint32_t The LBA sector address where this cluster begins.
 */
static inline uint32_t fat32_cluster_to_lba(uint32_t cluster)
{
    if (cluster < 2)
    {
        // TODO: Panic
        return g_data_start_lba;
    }

    return g_data_start_lba + (cluster - 2) * (uint32_t)g_bpb.sectors_per_cluster;
}

/* START: VFS */

extern vfs_fs_ops_t fat32_ops;

/**
 * @brief VFS Find Directory: Looks for a child file within a node.
 *
 * @details MECHANISM:
 * Uses `fat32_find_file` to scan the cluster associated with the `node`.
 * If found, it creates a new `vfs_node_t`, populates it with the file's
 * metadata (size, start cluster), and returns it.
 */
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
    new_node->flags = (dir_entry.attributes & 0x10)
                          ? VFS_DIRECTORY
                          : VFS_FILE;

    new_node_data->first_cluster = (dir_entry.first_cluster_high << 0x10) | dir_entry.first_cluster_low;
    new_node_data->sector_lba = loc.sector_lba;
    new_node_data->offset = loc.offset;
    new_node->device_data = new_node_data;
    new_node->ops = &fat32_ops;

    return new_node;
}

/**
 * @brief VFS Read: Reads data from a file into a buffer.
 *
 * @details MECHANISM (Cluster Chasing):
 * 1. Start at the file's `first_cluster`.
 * 2. Traverse the FAT chain to skip clusters until reaching `offset`.
 * 3. Read data cluster-by-cluster, copying it into `buffer`.
 * 4. Use `fat32_read_fat` to jump to the next cluster in the chain.
 *
 * @param node The file node to read from.
 * @param offset The byte offset to start reading.
 * @param size How many bytes to read.
 * @param buffer The destination buffer.
 */
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

/**
 * @brief Writes data to a file, extending the file size if necessary.
 *
 * @details MECHANISM:
 * This function handles two main tasks: File Extension and Data Writing.
 *
 * 1. FILE EXTENSION (Allocation):
 * - Checks if the write goes beyond the current file size (`node->length`).
 * - If the file is empty (`first_cluster == 0`), it allocates the first cluster
 * and updates the Directory Entry on the disk.
 * - If the file exists but needs more space, it traverses the FAT chain to the
 * end, calculates how many new clusters are needed, and appends them to the chain.
 * - Updates the file size in the Directory Entry via `fat32_update_size`.
 *
 * 2. DATA WRITING (Read-Modify-Write):
 * - Traverses the FAT chain to find the start cluster corresponding to `offset`.
 * - Since FAT32 works with clusters (e.g., 4096 bytes), we cannot write just a few bytes
 * directly without overwriting the rest of the sector/cluster with garbage.
 * - We perform a Read-Modify-Write cycle:
 * a. READ the entire cluster from disk into a buffer.
 * b. MODIFY only the requested bytes in the buffer.
 * c. WRITE the entire buffer back to disk.
 *
 * @param node   The file node to write to.
 * @param offset The offset in bytes where writing begins.
 * @param size   The number of bytes to write.
 * @param buffer The source buffer containing data.
 * @return uint64_t The number of bytes successfully written.
 */
static uint64_t fat32_write(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    fat32_node_data *node_data = (fat32_node_data *)(node->device_data);

    if (offset + size >= node->length)
    {
        uint8_t tmp_buf[SECTOR_SIZE];

        if (node_data->first_cluster == 0) // A brand new file, so no cluster attached
        {
            int64_t free_cluster_id = fat32_find_free_cluster();
            if (free_cluster_id < 0)
            {
                kprint("FAT32_CREATE failed: No free cluster id found!\n");
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
        else // file exists, but content overflows
        {
            uint32_t curr_cluster = node_data->first_cluster;
            uint32_t prev_cluster = 0;
            uint32_t cluster_count = 0;
            while (curr_cluster != EOC)
            {
                prev_cluster = curr_cluster;
                curr_cluster = fat32_read_fat(curr_cluster);
                cluster_count++;
            }
            if (prev_cluster == 0 || prev_cluster == EOC)
            {
                kprint("FAT32_WRITE failed: failed trace last cluster of a file\n");
                return 0;
            }

            uint32_t occupied_cluster_bytes = cluster_count * g_bytes_per_cluster;
            if ((offset + size) >= occupied_cluster_bytes)
            {
                uint32_t size_left = (offset + size) - occupied_cluster_bytes;
                uint32_t cluster_count_left = (size_left + g_bytes_per_cluster - 1) / g_bytes_per_cluster;

                for (uint32_t i = 0; i < cluster_count_left; i++)
                {
                    int64_t free_cluster_id = fat32_find_free_cluster();
                    if (free_cluster_id < 0)
                    {
                        kprint("FAT32_CREATE failed: No free cluster id found!\n");
                        return 0;
                    }

                    fat32_write_fat_entry(prev_cluster, free_cluster_id);
                    prev_cluster = free_cluster_id;
                }

                fat32_write_fat_entry(prev_cluster, EOC);
            }
        }

        fat32_update_size(node, offset + size);
    }

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
    memset(tmp_buf, 0, g_bytes_per_cluster);

    uint64_t rem_size = size;
    while (rem_size > 0)
    {
        uint64_t copy_size = uint64_min(rem_size, g_bytes_per_cluster - read_offset);
        uint32_t lba = fat32_cluster_to_lba(curr_cluster);
        ata_read_sectors(tmp_buf, lba, g_bpb.sectors_per_cluster, g_drive_sel);
        memcpy(tmp_buf + read_offset, buffer, copy_size);
        ata_write_sectors(tmp_buf, lba, g_bpb.sectors_per_cluster, g_drive_sel);
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
    .write = fat32_write,
    .finddir = fat32_finddir,
    .readdir = fat32_readdir,
    .open = NULL,
    .close = NULL,
    .create = fat32_create,
    .unlink = fat32_unlink,
};

/* END: VFS */

/**
 * @brief Reads the next cluster in the chain from the FAT Table.
 *
 * @details MECHANISM:
 * The FAT Table is an array of 32-bit integers stored sequentially on disk.
 * To find the entry for a specific cluster:
 * 1. Calculate the byte offset: cluster_index * 4 bytes.
 * 2. Locate the specific Sector (Page) in the FAT Region containing this offset.
 * 3. Read that Sector into a buffer.
 * 4. Extract the 4-byte value at the calculated offset within that sector.
 *
 * @param cluster The current cluster number (index).
 * @return uint32_t The next cluster number, or EOC (0x0FFFFFFF) if end of chain.
 */
uint32_t fat32_read_fat(uint32_t cluster)
{
    uint32_t fat_offset = cluster * sizeof(uint32_t);
    uint32_t fat_sector = g_bpb.reserved_sectors + (fat_offset / g_bpb.bytes_per_sector);
    uint32_t ent_offset = fat_offset % g_bpb.bytes_per_sector;

    uint16_t tmp_buf[SECTOR_SIZE / 2];
    ata_read_sectors(tmp_buf, fat_sector, 1, g_drive_sel);

    return *((uint32_t *)((uint8_t *)tmp_buf + ent_offset)) & 0x0FFFFFFF;
}

/**
 * @brief Initializes the FAT32 filesystem driver.
 *
 * @details MECHANISM:
 * Reads the Boot Sector (BPB) to understand the disk geometry.
 * Calculates the start LBA of the Data Region by skipping:
 * 1. The Reserved Region (Boot code).
 * 2. The FAT Region (The allocation tables).
 *
 * @param partition_lba The LBA where the partition starts.
 * @param drive_sel The ATA drive selector (Master/Slave).
 * @return vfs_node_t* The Root Directory node.
 */
vfs_node_t *fat32_init_fs(uint32_t partition_lba, uint8_t drive_sel)
{
    // our purpose is to find the data region lba.
    // reserved region is where bpb is stored
    // we have fat region (FAT tables) = fats_num * sectors_per_fat32
    // data region (clusters, including root directory), is right after FAT region.

    g_drive_sel = drive_sel;
    g_partition_lba = partition_lba;
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

void fat32_list_root()
{
    fat32_iterate(g_bpb.root_cluster, list_cb, NULL);
}

/**
 * @brief Parses an 8.3 filename (e.g., "FILE.TXT") into separate Name and Ext parts.
 *
 * @details MECHANISM:
 * FAT32 Directory Entries store names in a fixed format: 8 bytes for Name,
 * 3 bytes for Extension, padded with spaces. There is no dot stored on disk.
 * This function converts "file.txt" -> Name: "FILE    ", Ext: "TXT".
 * It handles capitalization and validation.
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

/**
 * @brief Finds a file in FAT32 format drive
 * returns 0 if file found
 * returns -1 otherwise
 */
int fat32_find_file(uint32_t cluster, const char *name, DirectoryEntry *out_entry, fat32_location_t *out_loc)
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

/**
 * @brief Iterates over all Directory Entries in a specific directory.
 *
 * @details MECHANISM:
 * A directory in FAT32 is just a file containing a list of `DirectoryEntry` structs.
 * This function:
 * 1. Reads the directory's cluster chain one cluster at a time.
 * 2. Inside each cluster, reads sectors one by one.
 * 3. Casts the buffer to `DirectoryEntry*` and loops through them.
 * 4. Checks special markers:
 * - 0x00: End of Directory (Stop scanning).
 * - 0xE5: Deleted Entry (Skip).
 * 5. Calls the `entry_cb` callback for every valid entry found.
 *
 * @param cluster The starting cluster of the directory.
 * @param entry_cb The callback function to invoke for each entry.
 * @param ctx User-provided context to pass to the callback.
 * @return int 0 on success, or error code.
 */
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

    // ignore the Long File Names and Volumne ID
    if (entry->attributes == 0x0F || entry->attributes == 0x08)
    {
        return 0;
    }

    // Found the file at 'idx'
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

/**
 * @brief VFS Read Directory: Lists files in a directory by index.
 *
 * @details MECHANISM:
 * Uses `fat32_iterate` to walk the directory. It counts valid files
 * until it reaches the requested `idx`. When the target index is reached,
 * it populates the `dirent_t` structure.
 */
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

/**
 * @brief Creates a new empty file in a specified directory.
 *
 * @details PROCESS:
 * 1. Preparation: Parses the filename and allocates a new Free Cluster in the FAT table.
 * 2. Location: Finds a free Directory Entry slot in the Parent Directory.
 * 3. Registration (Data Region):
 * - Creates a 'DirectoryEntry' struct with the file's metadata (Name, Size=0, Cluster ID).
 * - Reads the target sector of the Parent Directory.
 * - Writes the new entry into the calculated offset.
 * - Flushes the sector back to disk.
 * 4. Finalization (FAT Region):
 * - Marks the newly allocated cluster as EOC (End of Chain) in the FAT Table.
 *
 * @param parent Pointer to the parent directory node (vfs_node_t).
 * @param fname  The name of the new file (e.g., "test.txt").
 * @param flags  Creation flags (currently unused).
 * @return int 0 on success, -1 on failure (Disk full, Directory full, or Error).
 */
int fat32_create(vfs_node_t *parent, const char *fname, int flags)
{
    char name[8];
    char ext[3];
    fat32_location_t loc;

    fat32_node_data *parent_data = (fat32_node_data *)parent->device_data;
    if (parent_data == NULL)
    {
        kprint("FAT32_CREATE failed: Parent Data is NULL!\n");
        return -1;
    }

    DirectoryEntry exist_entry;
    if (fat32_find_file(parent_data->first_cluster, fname, &exist_entry, NULL) == 0)
    {
        kprint("FAT32_CREATE failed: File already exists!\n");
        return -1;
    }

    fat32_parse_name(fname, name, ext);
    int64_t free_cluster_id = fat32_find_free_cluster(); // find the slot for the child
    if (free_cluster_id < 0)
    {
        kprint("FAT32_CREATE failed: No free cluster id found!\n");
        return -1;
    }

    if (fat32_find_free_directory_entry(parent_data->first_cluster, &loc) < 0) // find a free slot in the parent to point to the child's slot
    {
        return -1;
    }

    uint8_t *tmp_buf = (uint8_t *)kmalloc(SECTOR_SIZE);
    if (tmp_buf == NULL)
    {
        kprint("FAT32_CREATE: OOM\n");
        return -1;
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
    return 0;
}

/**
 * @brief Updates a specific entry in the FAT Table (Write).
 *
 * @details MECHANISM (Read-Modify-Write):
 * Hard disks only write in full Sectors (512 bytes). We cannot write just 4 bytes.
 * 1. Calculate which Sector contains the entry for this cluster.
 * 2. READ the entire sector into memory.
 * 3. MODIFY the specific 32-bit integer at the correct offset.
 * 4. WRITE the entire sector back to disk.
 *
 * @param cluster The cluster number (index) to update.
 * @param value The new value to write (e.g., Next Cluster ID or EOC).
 */
void fat32_write_fat_entry(uint32_t cluster, uint32_t value)
{
    uint32_t fat_start_lba = g_partition_lba + g_bpb.reserved_sectors;

    uint32_t fat_sector_offset = (cluster * 4) / SECTOR_SIZE;
    uint32_t entry_offset = (cluster * 4) % SECTOR_SIZE;
    uint32_t target_lba = fat_start_lba + fat_sector_offset;

    uint8_t tmp_buf[SECTOR_SIZE] = {0};
    ata_read_sectors((uint16_t *)tmp_buf, target_lba, 1, g_drive_sel);

    *(uint32_t *)&tmp_buf[entry_offset] = value;

    ata_write_sectors((uint16_t *)tmp_buf, target_lba, 1, g_drive_sel);
}

/**
 * @brief Scans the FAT table to find the first available (free) cluster.
 *
 * @details MECHANISM:
 * Iterates through every sector of the FAT region. Within each sector,
 * it checks every 32-bit entry.
 * - A value of 0x00000000 indicates a free cluster.
 * - Skips cluster 0 and 1 as they are reserved in FAT32.
 *
 * @return int64_t The cluster number if found, or -1 if the disk is full.
 */
int64_t fat32_find_free_cluster()
{
    uint8_t tmp_buf[SECTOR_SIZE] = {0};
    for (uint32_t i = 0; i < g_bpb.sectors_per_fat32; i++)
    {
        uint32_t lba = g_partition_lba + g_bpb.reserved_sectors + i;
        ata_read_sectors((uint16_t *)tmp_buf, lba, 1, g_drive_sel);

        for (uint32_t j = 0; j < SECTOR_SIZE / sizeof(uint32_t); j++)
        {
            uint32_t cluster_id = i * 128 + j;
            if (i == 0 && cluster_id < 2)
            {
                continue;
            }

            if (((uint32_t *)tmp_buf)[j] == 0)
            {
                return cluster_id;
            }
        }
    }
    return -1;
}

/**
 * @brief Finds a free directory entry slot in a specific directory.
 *
 * @details MECHANISM:
 * Scans the directory's cluster chain to find a place to store a new file entry.
 * It looks for two types of available slots:
 * 1. Unused Slot (0x00): Marks the end of the directory. No entries exist after this.
 * 2. Deleted Slot (0xE5): A slot that was previously used but the file was deleted.
 * * When a free slot is found, it populates the 'out_loc' structure with the
 * physical LBA and byte offset, allowing the caller to write data directly to disk.
 *
 * @param dir_cluster The starting cluster of the directory to scan (e.g., Parent Directory).
 * @param out_loc Pointer to a structure to store the found location (Sector LBA + Offset).
 * @return int 0 if a free slot is found, -1 if the directory is full or an error occurs.
 */
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

            DirectoryEntry *dir_entries = tmp_buf;
            for (int j = 0; j < g_bpb.bytes_per_sector / sizeof(DirectoryEntry); j++)
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
    while (curr_cluster != EOC)
    {
        uint32_t next_cluster = fat32_read_fat(curr_cluster);
        fat32_write_fat_entry(curr_cluster, 0);
        curr_cluster = next_cluster;
    }
}