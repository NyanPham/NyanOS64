#include "libc/libc.h"

typedef struct __attribute__((packed))
{
    // bitmap header
    uint16_t type; // magic identity: 0x4d42 ("BM")
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset; // offset to image data

    // DIB  header
    uint32_t header_size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bits; // bits per pixel (24 or 32)
    uint32_t compression;
    uint32_t imagesize;
    int32_t xresolution;
    int32_t yresolution;
    uint32_t ncolours;
    uint32_t importantcolours;
} BMPHeader_t;

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print("Usage: view_bmp.elf <image_path>\n");
        exit(1);
    }

    const char *fname = argv[1];

    print("[view_bmp] Opening file: ");
    print(fname);
    print("\n");

    int fd = open(fname, O_RDONLY);
    if (fd < 0)
    {
        print("[view_bmp]: Failed to open image\n");
        exit(1);
    }

    BMPHeader_t bmp_header;
    memset(&bmp_header, 0, sizeof(BMPHeader_t));

    int bytes_read = read(fd, &bmp_header, sizeof(BMPHeader_t));

    print("[view_bmp] Bytes read:");
    print_dec(bytes_read);
    print("\n");

    print("[view_bmp] Type:");
    print_dec(bmp_header.type);
    print("\n");

    if (bmp_header.type != 0x4D42)
    {
        print("[view_bmp]: Error: Not a valid BMP file!\n");
        close(fd);
        exit(1);
    }

    WinParams_t win_params;
    win_params.x = 100;
    win_params.y = 100;
    win_params.width = bmp_header.width;
    win_params.height = bmp_header.height;
    win_params.flags = WIN_MOVABLE;
    strcpy(win_params.title, fname);

    if (win_create(&win_params) < 0)
    {
        print("[view_bmp]: Failed to create window.\n");
        close(fd);
        exit(1);
    }

    // todo: implement lseek
    int header_read_so_far = sizeof(BMPHeader_t);
    int gap = bmp_header.offset - header_read_so_far;

    if (gap > 0)
    {
        char *junk = malloc(gap);
        read(fd, junk, gap);
        free(junk);
    }

    int bytes_per_pixel = bmp_header.bits / 8;
    int row_size = (bmp_header.width * bytes_per_pixel + 3) & (~3);
    uint8_t *row_buf = malloc(row_size);
    uint32_t *img_buf = malloc(bmp_header.width * bmp_header.height * sizeof(uint32_t));

    int content_start_y = 0;

    for (int y = bmp_header.height - 1; y >= 0; y--)
    {
        read(fd, row_buf, row_size);

        for (int x = 0; x < bmp_header.width; x++)
        {
            int idx = x * bytes_per_pixel;

            uint8_t b = row_buf[idx];
            uint8_t g = row_buf[idx + 1];
            uint8_t r = row_buf[idx + 2];

            uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
            img_buf[y * bmp_header.width + x] = color;
        }
    }

    free(row_buf);
    close(fd);

    blit(0, 0, bmp_header.width, bmp_header.height, img_buf);
    free(img_buf);

    while (1)
    {
        sleep(1000);
    }

    exit(0);
}