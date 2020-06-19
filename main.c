#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include "lz4frame.h"

#define RES 320*240

#include <sys/time.h>

long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
    // printf("milliseconds: %lld\n", milliseconds);
    return milliseconds;
}

typedef struct 
{
    uint8_t bpp;
    uint32_t frame_size;
    uint16_t x;
    uint16_t y;
} __attribute__((packed)) metadata_t;

int main()
{
    int socket_desc = 0;
    int opt = 1;
    if (!(socket_desc = socket(AF_INET, SOCK_STREAM, 0)))
    {
        printf("Failed to open socket!\n");
        exit(0);
    }

    int flag = 1;
    if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1)
    {
        printf("Failed to set socket opts!\n");
        exit(0);
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(420);

    int addr_size = sizeof(address);

    if (bind(socket_desc, (struct sockaddr *)&address, addr_size) < 0)
    {
        printf("Failed to bind!\n");
        exit(0);
    }

    if (listen(socket_desc, 3) < 0)
    {
        printf("Failed to listen\n");
        exit(0);
    }

    
    int fb_size = RES * 4;
    uint8_t *vb_buffer = malloc(fb_size);

    if (vb_buffer == NULL)
    {
        printf("Failed to allocate FB\n");
        exit(0);
    }

    while(1)
    {
        int inc_socket_desc = 0;
        printf("Going to accept!\n");
        if ((inc_socket_desc = accept(socket_desc, (struct sockaddr *)&address, (socklen_t*)&addr_size)) < 0)
        {
            printf("Failed to accept\n");
            exit(0);
        }

        int fb_fil = open("/dev/fb0", O_RDWR);
        int btn_fil = open("/dev/input/event0", O_RDWR)
        int js_fil = open("/dev/input/event3", O_RDWR);

        if (fb_fil == -1)
        {
            printf("Failed to open fb dev\n");
            exit(0);
        }

        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;

        if (ioctl(fb_fil, FBIOGET_FSCREENINFO, &finfo) == -1) {
            printf("Error reading fixed information\n");
            exit(0);
        }

        // Get variable screen information
        if (ioctl(fb_fil, FBIOGET_VSCREENINFO, &vinfo) == -1) {
            printf("Error reading variable information\n");
            exit(0);
        }

        printf("%dx%d, %dbpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

        // Figure out the size of the screen in bytes
        long int screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

        uint8_t *fbp = (uint8_t *)mmap(0, screensize, PROT_READ, MAP_SHARED, fb_fil, 0);


        LZ4F_preferences_t compOpts;
        memset(&compOpts, 0, sizeof(compOpts));
        compOpts.compressionLevel = -1;

        int counter = 0;
        metadata_t meta;
        meta.bpp = vinfo.bits_per_pixel;
        meta.x = vinfo.xres;
        meta.y = vinfo.yres;
        send(inc_socket_desc, (void *)&meta, sizeof(meta), 0);
        long long beforeTs = current_timestamp(); //TS in MS
        uint8_t *buffer = malloc(vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8);
        char *lz4Buffer = malloc(vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8);
        memset(lz4Buffer, 0, vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8);
        while(1)
        {
            if (++counter == 60)
            {
                if (ioctl(fb_fil, FBIOGET_VSCREENINFO, &vinfo) == -1) {
                    printf("Error reading variable information\n");
                    exit(0);
                }
                counter = 0;
                meta.bpp = vinfo.bits_per_pixel;
                meta.x = vinfo.xres;
                meta.y = vinfo.yres;
                free(lz4Buffer);
                lz4Buffer = malloc(LZ4F_compressFrameBound(vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8, NULL));
                free(buffer);
                buffer = malloc(vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8);

            }

            long int screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
            
            memcpy(buffer, fbp, screensize);
            
            meta.frame_size = (uint32_t)LZ4F_compressFrame(lz4Buffer, LZ4F_compressFrameBound(screensize, NULL), buffer, screensize, &compOpts);
            if (meta.frame_size == 0) printf("Comp size is 0\n");
            send(inc_socket_desc, (void *)&meta, sizeof(meta), 0);   
            send(inc_socket_desc, lz4Buffer, meta.frame_size, 0);
            int delay = ((1000/60) - (current_timestamp() - beforeTs)) * 1000;

            if (delay > 0)
                usleep(delay);

            beforeTs += 1000 / 60;
        }
    }
}