#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include "lz4frame.h"
#include <time.h>

#define RES 320*240

#include <sys/time.h>

#include <linux/input.h>
#include <linux/uinput.h>

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

FILE *emitFileDump;

void emit(int fd, int type, int code, int val)
{
   struct input_event ie;

   printf("Emitting %s for key %x\n", val ? "key up" : "key down");

   ie.type = type;
   ie.code = code;
   ie.value = val;
   ie.time.tv_sec = time(0);
   ie.time.tv_usec = 0;

   write(fd, &ie, sizeof(ie));
   fwrite(&ie, 1, sizeof(ie), emitFileDump);
}

int main()
{
    emitFileDump = fopen("emitDump", "wb");
    const uint16_t keys[] = { BTN_THUMBL, BTN_THUMBR, BTN_A, BTN_B, BTN_X, BTN_Y, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
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

    int fb_fil = open("/dev/fb0", O_RDWR);
    //int btn_fil = open("/dev/input/event0", O_RDWR);
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

    //Setup input
    int uinput_fd = open("/dev/input/event0", O_WRONLY);
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    for (int x = 0; x < 14; x++)
        ioctl(uinput_fd, UI_SET_KEYBIT, keys[x]);

    struct uinput_user_dev uud;
    ioctl(uinput_fd, UI_DEV_CREATE);

    while(1)
    {
        int inc_socket_desc = 0;
        printf("Going to accept!\n");
        if ((inc_socket_desc = accept(socket_desc, (struct sockaddr *)&address, (socklen_t*)&addr_size)) < 0)
        {
            printf("Failed to accept\n");
            exit(0);
        }

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
        int connected = 1;

        uint16_t *keybuffer = malloc(2 * 0xFF);

        while(connected)
        {
            if (++counter == 60)
            {
                if (ioctl(fb_fil, FBIOGET_VSCREENINFO, &vinfo) == -1) {
                    printf("Error reading variable information\n");
                    exit(0);
                }
                counter = 0;
                if (meta.bpp != vinfo.bits_per_pixel || meta.x != vinfo.xres || meta.y != vinfo.yres)
                {
                    free(lz4Buffer);
                    lz4Buffer = malloc(LZ4F_compressFrameBound(vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8, NULL));
                    free(buffer);
                    buffer = malloc(vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8);
                    meta.bpp = vinfo.bits_per_pixel;
                    meta.x = vinfo.xres;
                    meta.y = vinfo.yres;
                }
                
            }

            long int screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
            memcpy(buffer, fbp, screensize);
            meta.frame_size = (uint32_t)LZ4F_compressFrame(lz4Buffer, LZ4F_compressFrameBound(screensize, NULL), buffer, screensize, &compOpts);
            if (meta.frame_size == 0) printf("Comp size is 0\n");

            if (send(inc_socket_desc, (void *)&meta, sizeof(meta), 0) != sizeof(meta) || send(inc_socket_desc, lz4Buffer, meta.frame_size, 0) != meta.frame_size)
                connected = 0;
            
            for (int cycle = 1; cycle >= 0; cycle--)
            {
                uint8_t keys_to_process = 0;
                if (recv(inc_socket_desc, &keys_to_process, 1, 0) != 1)
                    connected = 0;
                else if (keys_to_process)
                {
                    printf("%s - %d to process\n", cycle == 0 ? "Key up" : "Key down", keys_to_process);
                    if (recv(inc_socket_desc, keybuffer, 2 * keys_to_process, 0) != (2 * keys_to_process))
                        connected = 0;
                    for (int idx = 0; idx < keys_to_process; idx++)
                    {
                        printf("Key %x\n", keybuffer[idx]);
                        bool found = false;
                        for(int keymap_idx = 0; keymap_idx < 14; keymap_idx++)
                        {
                            if (keys[keymap_idx] == keybuffer[idx])
                                found = true;
                        }
                        if (found)
                        {
                            emit(uinput_fd, EV_KEY, keybuffer[idx], cycle);
                            emit(uinput_fd, EV_SYN, SYN_REPORT, 0);
                        }
                        else printf("Not found %x\n", keybuffer[idx]);
                    }
                }
            }

            int delay = ((1000/60) - (current_timestamp() - beforeTs)) * 1000;
            if (delay > 0)
                usleep(delay);
            beforeTs += 1000 / 60;
        }
        printf("Exiting\n");
        fclose(emitFileDump);
    }
}