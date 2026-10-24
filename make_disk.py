import os
import sys
import struct
import tarfile

SECTOR_SIZE = 512

def build_rootfs_tar():
    print("Creating rootfs.tar...")
    if os.path.exists("rootfs.tar"):
        os.remove("rootfs.tar")

    with tarfile.open("rootfs.tar", "w", format=tarfile.USTAR_FORMAT) as tar:
        for root, dirs, files in os.walk("rootfs"):
            rel_root = os.path.relpath(root, "rootfs").replace("\\", "/")
            if rel_root != ".":
                ti = tarfile.TarInfo(rel_root + "/")
                ti.type = tarfile.DIRTYPE
                ti.mode = 0o755
                tar.addfile(ti)
            for f in sorted(files):
                full_path = os.path.join(root, f)
                arc_name = f if rel_root == "." else f"{rel_root}/{f}"
                tar.add(full_path, arcname=arc_name, recursive=False)

def make_83_name(filename):
    name = filename.upper()
    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    base = base.replace("-", "_")
    return base[:8].ljust(8, " ").encode("ascii") + ext[:3].ljust(3, " ").encode("ascii")

def lfn_checksum(short_name_11):
    chk = 0
    for b in short_name_11:
        chk = (((chk & 1) << 7) + (chk >> 1) + b) & 0xFF
    return chk

def make_lfn_entries(filename, short_name_11):
    chk = lfn_checksum(short_name_11)
    utf16_chars = [ord(c) for c in filename]
    if len(utf16_chars) % 13 != 0:
        utf16_chars.append(0x0000)
        while len(utf16_chars) % 13 != 0:
            utf16_chars.append(0xFFFF)

    num_chunks = len(utf16_chars) // 13
    entries = []
    for i in range(num_chunks, 0, -1):
        chunk = utf16_chars[(i - 1) * 13 : i * 13]
        seq = i | (0x40 if i == num_chunks else 0)
        entry = bytearray(32)
        entry[0] = seq
        struct.pack_into("<5H", entry, 1, *chunk[0:5])
        entry[11] = 0x0F  # LFN attribute
        entry[12] = 0x00
        entry[13] = chk
        struct.pack_into("<6H", entry, 14, *chunk[5:11])
        struct.pack_into("<H", entry, 26, 0)
        struct.pack_into("<2H", entry, 28, *chunk[11:13])
        entries.append(entry)
    return entries

class Fat32Builder:
    def __init__(self, total_sectors, hidden_sectors=0, label=b"NYANOS     ", use_lfn=False, sectors_per_cluster=1):
        self.total_sectors = total_sectors
        self.hidden_sectors = hidden_sectors
        self.use_lfn = use_lfn
        self.sectors_per_cluster = sectors_per_cluster
        self.reserved_sectors = 32
        self.num_fats = 2
        clusters = total_sectors // self.sectors_per_cluster
        self.sectors_per_fat = ((clusters * 4) + SECTOR_SIZE - 1) // SECTOR_SIZE
        self.data_start_sec = self.reserved_sectors + (self.num_fats * self.sectors_per_fat)
        self.image = bytearray(total_sectors * SECTOR_SIZE)
        self.fat = [0] * clusters
        self.fat[0] = 0x0FFFFFF8
        self.fat[1] = 0x0FFFFFFF
        self.next_free_cluster = 2
        self.label = label
        self._write_bpb()
        self.root_cluster = self.alloc_cluster()

    def _write_bpb(self):
        bpb = bytearray(SECTOR_SIZE)
        bpb[0:3] = b"\xEB\x58\x90"
        bpb[3:11] = b"MSWIN4.1"
        struct.pack_into("<H", bpb, 11, SECTOR_SIZE)
        struct.pack_into("<B", bpb, 13, self.sectors_per_cluster)
        struct.pack_into("<H", bpb, 14, self.reserved_sectors)
        struct.pack_into("<B", bpb, 16, self.num_fats)
        struct.pack_into("<H", bpb, 17, 0)
        struct.pack_into("<H", bpb, 19, 0)
        struct.pack_into("<B", bpb, 21, 0xF8)
        struct.pack_into("<H", bpb, 22, 0)
        struct.pack_into("<H", bpb, 24, 63)
        struct.pack_into("<H", bpb, 26, 255)
        struct.pack_into("<I", bpb, 28, self.hidden_sectors)
        struct.pack_into("<I", bpb, 32, self.total_sectors)
        struct.pack_into("<I", bpb, 36, self.sectors_per_fat)
        struct.pack_into("<H", bpb, 40, 0)
        struct.pack_into("<H", bpb, 42, 0)
        struct.pack_into("<I", bpb, 44, 2)  # Root cluster = 2
        struct.pack_into("<H", bpb, 48, 1)  # FSInfo
        struct.pack_into("<H", bpb, 50, 6)  # Backup boot sector
        bpb[66] = 0x29
        struct.pack_into("<I", bpb, 67, 0x12345678)
        bpb[71:82] = self.label
        bpb[82:90] = b"FAT32   "
        bpb[510:512] = b"\x55\xAA"
        self.image[0:SECTOR_SIZE] = bpb
        self.image[6*SECTOR_SIZE:7*SECTOR_SIZE] = bpb

        fsinfo = bytearray(SECTOR_SIZE)
        struct.pack_into("<I", fsinfo, 0, 0x41615252)
        struct.pack_into("<I", fsinfo, 484, 0x61417272)
        struct.pack_into("<I", fsinfo, 488, 0xFFFFFFFF)
        struct.pack_into("<I", fsinfo, 492, 0xFFFFFFFF)
        fsinfo[510:512] = b"\x55\xAA"
        self.image[SECTOR_SIZE:2*SECTOR_SIZE] = fsinfo

    def cluster_offset(self, cluster):
        sec = self.data_start_sec + (cluster - 2) * self.sectors_per_cluster
        return sec * SECTOR_SIZE

    def alloc_cluster(self):
        c = self.next_free_cluster
        self.next_free_cluster += 1
        self.fat[c] = 0x0FFFFFFF
        return c

    def _append_raw_dir_entry(self, dir_cluster, raw_entry):
        cluster_bytes = self.sectors_per_cluster * SECTOR_SIZE
        curr = dir_cluster
        while True:
            off = self.cluster_offset(curr)
            for i in range(0, cluster_bytes, 32):
                if self.image[off + i] in (0x00, 0xE5):
                    self.image[off + i : off + i + 32] = raw_entry
                    return
            nxt = self.fat[curr]
            if nxt >= 0x0FFFFFF8:
                nxt = self.alloc_cluster()
                self.fat[curr] = nxt
            curr = nxt

    def add_dir_entry(self, dir_cluster, filename, name11, attr, first_cluster, size):
        if self.use_lfn and filename not in (".", ".."):
            for lfn_ent in make_lfn_entries(filename, name11):
                self._append_raw_dir_entry(dir_cluster, lfn_ent)

        entry = bytearray(32)
        entry[0:11] = name11
        entry[11] = attr
        struct.pack_into("<H", entry, 20, (first_cluster >> 16) & 0xFFFF)
        struct.pack_into("<H", entry, 26, first_cluster & 0xFFFF)
        struct.pack_into("<I", entry, 28, size)
        self._append_raw_dir_entry(dir_cluster, entry)

    def mkdir(self, parent_cluster, dirname):
        c = self.alloc_cluster()
        self.add_dir_entry(parent_cluster, dirname, make_83_name(dirname), 0x10, c, 0)
        self.add_dir_entry(c, ".", b".          ", 0x10, c, 0)
        parent_ref = 0 if parent_cluster == self.root_cluster else parent_cluster
        self.add_dir_entry(c, "..", b"..         ", 0x10, parent_ref, 0)
        return c

    def add_file(self, dir_cluster, filename, data):
        cluster_bytes = self.sectors_per_cluster * SECTOR_SIZE
        if len(data) == 0:
            first_c = self.alloc_cluster()
        else:
            num_clusters = (len(data) + cluster_bytes - 1) // cluster_bytes
            chain = [self.alloc_cluster() for _ in range(num_clusters)]
            for idx in range(len(chain) - 1):
                self.fat[chain[idx]] = chain[idx + 1]
            self.fat[chain[-1]] = 0x0FFFFFFF
            first_c = chain[0]
            for idx, c in enumerate(chain):
                chunk = data[idx * cluster_bytes : (idx + 1) * cluster_bytes]
                off = self.cluster_offset(c)
                self.image[off : off + len(chunk)] = chunk
        self.add_dir_entry(dir_cluster, filename, make_83_name(filename), 0x20, first_c, len(data))

    def finalize(self):
        fat_bytes = bytearray(self.sectors_per_fat * SECTOR_SIZE)
        for i, val in enumerate(self.fat):
            struct.pack_into("<I", fat_bytes, i * 4, val)
        for f in range(self.num_fats):
            start = (self.reserved_sectors + f * self.sectors_per_fat) * SECTOR_SIZE
            self.image[start : start + len(fat_bytes)] = fat_bytes
        return self.image

def build_hdd_img(reset=False):
    if not reset and os.path.exists("hdd.img"):
        return
    print("Creating 64MB FAT32 hdd.img...")
    total_sectors = (64 * 1024 * 1024) // SECTOR_SIZE
    fs = Fat32Builder(total_sectors, hidden_sectors=0, label=b"DATA_DISK  ", use_lfn=False, sectors_per_cluster=1)
    fs.add_file(fs.root_cluster, "TEST.TXT", b"Hello from NyanOS FAT32 disk!")
    if os.path.exists("snake.elf"):
        with open("snake.elf", "rb") as f:
            fs.add_file(fs.root_cluster, "SNAKE.ELF", f.read())
    with open("hdd.img", "wb") as f:
        f.write(fs.finalize())

def build_boot_image():
    print("Creating bootable image.hdd...")
    total_sectors = (64 * 1024 * 1024) // SECTOR_SIZE
    part_start = 2048
    part_sectors = total_sectors - part_start

    # sectors_per_cluster=1 ensures >65,525 clusters so Limine detects FAT32 instead of FAT16
    fs = Fat32Builder(part_sectors, hidden_sectors=part_start, label=b"NYANBOOT   ", use_lfn=True, sectors_per_cluster=1)
    efi_dir = fs.mkdir(fs.root_cluster, "EFI")
    efi_boot_dir = fs.mkdir(efi_dir, "BOOT")
    boot_dir = fs.mkdir(fs.root_cluster, "boot")
    limine_dir = fs.mkdir(boot_dir, "limine")

    files_to_copy = [
        (boot_dir, "nyanOS", "bin/nyanOS", True),
        (boot_dir, "limine.conf", "limine.conf", True),
        (limine_dir, "limine.conf", "limine.conf", True),
        (fs.root_cluster, "limine.conf", "limine.conf", True),
        (boot_dir, "SHELL.ELF", "shell.elf", True),
        (boot_dir, "ROOTFS.TAR", "rootfs.tar", True),
        (limine_dir, "limine-bios.sys", "limine/limine-bios.sys", True),
        (boot_dir, "limine-bios.sys", "limine/limine-bios.sys", True),
        (fs.root_cluster, "limine-bios.sys", "limine/limine-bios.sys", True),
        (efi_boot_dir, "BOOTX64.EFI", "limine/BOOTX64.EFI", False),
        (efi_boot_dir, "BOOTIA32.EFI", "limine/BOOTIA32.EFI", False),
    ]
    for target_dir, fat_name, host_path, required in files_to_copy:
        if os.path.exists(host_path):
            with open(host_path, "rb") as f:
                fs.add_file(target_dir, fat_name, f.read())
        elif required:
            raise FileNotFoundError(f"Required boot file missing: {host_path}")

    disk = bytearray(total_sectors * SECTOR_SIZE)
    mbr_part = struct.pack("<BBBBBBBBII", 0x80, 0, 2, 0, 0x0C, 254, 255, 255, part_start, part_sectors)
    disk[0x1BE : 0x1BE + 16] = mbr_part
    disk[510:512] = b"\x55\xAA"
    disk[part_start * SECTOR_SIZE :] = fs.finalize()

    with open("image.hdd", "wb") as f:
        f.write(disk)

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "all"
    if cmd == "reset-disk":
        build_hdd_img(reset=True)
    elif cmd == "tar":
        build_rootfs_tar()
    elif cmd == "image":
        build_rootfs_tar()
        build_hdd_img(reset=False)
        build_boot_image()