#include "fat32.h"
#include "drivers/ata.h"
#include "string.h"
#include "drivers/serial.h"

static fat32_bpb g_bpb;
static uint32_t data_start_lba;
static uint8_t g_drive_sel;

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

void fat32_init(uint32_t partition_lba, uint8_t drive_sel)
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
    fat32_iterate_root(list_cb, NULL);
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

int fat32_find_file(const char *name, DirectoryEntry *out_entry)
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

    int res = fat32_iterate_root(find_cb, &find_control);
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

int fat32_iterate_root(fat32_entry_cb_t entry_cb, void *ctx)
{
    uint32_t curr_cluster = g_bpb.root_cluster;

    while (curr_cluster < 0x0FFFFFF8)
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
                    kprint("ERROR IN fat32_iterate_root after calling entry_cb\n");
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

uint32_t fat32_read_fat(uint32_t cluster)
{
    uint32_t fat_offset = cluster * sizeof(uint32_t);
    uint32_t fat_sector = g_bpb.reserved_sectors + (fat_offset / g_bpb.bytes_per_sector);
    uint32_t ent_offset = fat_offset % g_bpb.bytes_per_sector;

    uint16_t tmp_buf[SECTOR_SIZE / 2];
    ata_read_sectors(tmp_buf, fat_sector, 1, g_drive_sel);

    return *((uint32_t *)((uint8_t *)tmp_buf + ent_offset)) & 0x0FFFFFFF;
}
