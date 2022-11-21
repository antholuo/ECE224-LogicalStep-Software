/*
 * "Hello World" example.
 *
 * This example prints 'Hello from Nios II' to the STDOUT stream. It runs on
 * the Nios II 'standard', 'full_featured', 'fast', and 'low_cost' example
 * designs. It runs with or without the MicroC/OS-II RTOS and requires a STDOUT
 * device in your system's hardware.
 * The memory footprint of this hosted application is ~69 kbytes by default
 * using the standard reference design.
 *
 * For a reduced footprint version of this template, and an explanation of how
 * to reduce the memory footprint for a given application, see the
 * "small_hello_world" template.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <system.h>
#include <sys/alt_alarm.h>
#include <sys/alt_irq.h>
#include <io.h>

#include "fatfs.h"
#include "diskio.h"

#include "ff.h"
#include "monitor.h"
#include "uart.h"

#include "alt_types.h"

#include <altera_avalon_pio_regs.h>
#include <altera_up_avalon_audio.h>
#include <altera_up_avalon_audio_and_video_config.h>

/* data defines */
#define BUF_SIZE 64
#define DATA_OFFSET 44
#define ALL_LOW 128
#define SW0_HIGH 129
#define SW1_HIGH 130
#define SW01_HIGH 131

#define DISK_NUM 0
#define DRIVE_NUM 0
#define FOPEN_MODE 1

#define ESC 27
#define CLEAR_LCD_STR "[2J"

#define STOPPED 0
#define PAUSED 1
#define PBACK_NORM 2
#define PBACK_HALF 3
#define PBACK_DBL 4
#define PBACK_MONO 5

#define PSTR(_a) _a

typedef struct wav_file
{
    char fname[20];
    unsigned long length;
    int entry_num;
} WAV_FILE;

// a bunch of defs
uint8_t playing, stopped, paused, next, prev;
int inputs = 0;
int track_num;
int speed = 2; // 2 = norm, 3 = half, 4 = double, 5 = mono

static alt_alarm alarm;
static unsigned long Systick = 0;
static volatile unsigned short Timer; /* 1000Hz increment timer */

static void button_ISR(void *context, alt_u32 id)
{
    // isr code here
    // disable button interrupts
    IOWR(BUTTON_PIO_BASE, 2, 0x0);
    int buttons = IORD(BUTTON_PIO_BASE, 0);
    if (buttons == 0b1110)
    { // next
        inputs = 1;
        next = 1;
        // stopped = 1;
    }
    if (buttons == 0b1101)
    { // play/pause
        inputs = 2;
        if (paused)
        {
            display(track_num, speed);
            paused = 0;
            playing = 1;
        }
        else
        {
            display(track_num, PAUSED);
            paused = 1;
            playing = 0;
        }
    }
    if (buttons == 0b1011)
    { // stop
        display(track_num, STOPPED);
        printf("stopped\n");
        inputs = 3;
        stopped = 1;
    }
    if (buttons == 0b0111)
    { // prev
        inputs = 4;
        prev = 1;
        // stopped = 1;
    }
    // start the timer
    IOWR(TIMER_0_BASE, 1, 0b0111);

    // clear interrupt reg
    IOWR(BUTTON_PIO_BASE, 3, 0x0);
}

static void timer_ISR(void *context, alt_u32 id)
{
    // timer isr code here

    // clear button int reg
    IOWR(BUTTON_PIO_BASE, 3, 0x0);

    // re-enable button irq
    IOWR(BUTTON_PIO_BASE, 2, 0xf);
    // clear timer TO
    IOWR(TIMER_0_BASE, 0, 0x0);
    IOWR(TIMER_0_BASE, 1, 0b1011);
}

static alt_u32 TimerFunction(void *context)
{
    static unsigned short wTimer10ms = 0;

    (void)context;

    Systick++;
    wTimer10ms++;
    Timer++; /* Performance counter for this module */

    if (wTimer10ms == 10)
    {
        wTimer10ms = 0;
        ffs_DiskIOTimerproc(); /* Drive timer procedure of low level disk I/O module */
    }

    return (1);
} /* TimerFunction */

static void IoInit(void)
{
    uart0_init(115200);

    /* Init diskio interface */
    ffs_DiskIOInit();

    // SetHighSpeed();

    /* Init timer system */
    alt_alarm_start(&alarm, 1, &TimerFunction, NULL);

} /* IoInit */
uint32_t acc_size; /* Work register for fs command */
uint16_t acc_files, acc_dirs;
FILINFO Finfo;
#if _USE_LFN
char Lfname[512];
#endif

char Line[256]; /* Console input buffer */
char fnames[20][20];
unsigned long lengths[20];
int entry_nums[20];
int num_wav_files = 0;

FATFS Fatfs[_VOLUMES];                          /* File system object for each logical drive */
FIL File1, File2;                               /* File objects */
DIR Dir;                                        /* Directory object */
uint8_t Buff[8192] __attribute__((aligned(4))); /* Working buffer */

static FRESULT scan_files(char *path)
{
    DIR dirs;
    FRESULT res;
    uint8_t i;
    char *fn;

    if ((res = f_opendir(&dirs, path)) == FR_OK)
    {
        i = (uint8_t)strlen(path);
        while (((res = f_readdir(&dirs, &Finfo)) == FR_OK) && Finfo.fname[0])
        {
            if (_FS_RPATH && Finfo.fname[0] == '.')
                continue;
#if _USE_LFN
            fn = *Finfo.lfname ? Finfo.lfname : Finfo.fname;
#else
            fn = Finfo.fname;
#endif
            if (Finfo.fattrib & AM_DIR)
            {
                acc_dirs++;
                *(path + i) = '/';
                strcpy(path + i + 1, fn);
                res = scan_files(path);
                *(path + i) = '\0';
                if (res != FR_OK)
                    break;
            }
            else
            {
                acc_files++;
                acc_size += Finfo.fsize;
            }
        }
    }

    return res;
}

static void put_rc(FRESULT rc)
{
    const char *str = "OK\0"
                      "DISK_ERR\0"
                      "INT_ERR\0"
                      "NOT_READY\0"
                      "NO_FILE\0"
                      "NO_PATH\0"
                      "INVALID_NAME\0"
                      "DENIED\0"
                      "EXIST\0"
                      "INVALID_OBJECT\0"
                      "WRITE_PROTECTED\0"
                      "INVALID_DRIVE\0"
                      "NOT_ENABLED\0"
                      "NO_FILE_SYSTEM\0"
                      "MKFS_ABORTED\0"
                      "TIMEOUT\0"
                      "LOCKED\0"
                      "NOT_ENOUGH_CORE\0"
                      "TOO_MANY_OPEN_FILES\0";
    FRESULT i;

    for (i = 0; i != rc && *str; i++)
    {
        while (*str++)
            ;
    }
    xprintf("rc=%u FR_%s\n", (uint32_t)rc, str);
}

int isWav(char *filename)
{
    char *dot = strrchr(filename, '.');
    return (dot && !strcmp(dot, ".WAV"));
}

int initialize()
{
    disk_initialize((uint8_t)DISK_NUM);
    f_mount((uint8_t)DRIVE_NUM, &Fatfs[DRIVE_NUM]);
    return 0;
}

int load_wav()
{
    uint8_t res;
    long p1;
    uint32_t s1, s2 = sizeof(Buff);

    res = f_opendir(&Dir, "");
    if (res)
    {
        return -1;
    }
    p1 = s1 = s2 = 0;

    int i = 0;

    for (;;)
    {
        res = f_readdir(&Dir, &Finfo);
        if ((res != FR_OK) || !Finfo.fname[0])
            return -1;
        if (Finfo.fattrib & AM_DIR)
        {
            s2++;
        }
        else
        {
            s1++;
            p1 += Finfo.fsize;
        }
        i++;

        if (isWav(&Finfo.fname[0]))
        {
            strcpy(&fnames[num_wav_files][0], &(Finfo.fname[0]));
            lengths[num_wav_files] = Finfo.fsize;
            entry_nums[num_wav_files] = i;
            num_wav_files++;
        }
    }
}

int display(int index, int mode)
{
    FILE *lcd;

    lcd = fopen("/dev/lcd_display", "w");
    char *first_line = &fnames[index][0];

    if (lcd != NULL)
    {
        fprintf(lcd, "%c%s", ESC, CLEAR_LCD_STR);
        fprintf(lcd, "%d: %s\n", entry_nums[index], first_line);
        if (mode == STOPPED)
        {
            fprintf(lcd, "STOPPED\n");
        }
        else if (mode == PAUSED)
        {
            fprintf(lcd, "PAUSED\n");
        }
        else if (mode == PBACK_NORM)
        {
            fprintf(lcd, "PBACK-NORM SPD\n");
        }
        else if (mode == PBACK_HALF)
        {
            fprintf(lcd, "PBACK-HALF SPD\n");
        }
        else if (mode == PBACK_DBL)
        {
            fprintf(lcd, "PBACK-DBL SPD\n");
        }
        else if (mode == PBACK_MONO)
        {
            fprintf(lcd, "PBACK-MONO\n");
        }
    }

    fclose(lcd);

    return 0;
}

int play(int index, char *fname, unsigned long p1, alt_up_audio_dev *audio_dev)
{
    int fifospace;
    char *ptr, *ptr2;
    long p2, p3;
    uint8_t res = 0;
    uint32_t s1, s2, cnt, blen = sizeof(Buff);
    uint32_t ofs = 0;
    FATFS *fs;

    unsigned int l_buf;
    unsigned int r_buf;

    // open file
    f_open(&File1, fname, (uint8_t)FOPEN_MODE);

    ofs = File1.fptr;
    unsigned int bytes_read = 0;
    res = f_read(&File1, Buff, DATA_OFFSET, &s2);
    bytes_read += s2;

    uint8_t playback_buf[BUF_SIZE] __attribute__((aligned(4)));

    int switches = IORD(SWITCH_PIO_BASE, 0x0);
    switches &= 3;
    switch (switches)
    {
    case 0:
        display(index, PBACK_NORM);
        speed = PBACK_NORM;
        break;
    case 1:
        display(index, PBACK_HALF);
        speed = PBACK_HALF;
        break;
    case 2:
        display(index, PBACK_DBL);
        speed = PBACK_DBL;
        break;
    case 3:
        display(index, PBACK_MONO);
        speed = PBACK_MONO;
        break;
    }

    while (bytes_read < p1)
    {
        if (p1 - bytes_read > BUF_SIZE)
        {
            res = f_read(&File1, playback_buf, BUF_SIZE, &s2);
            if (res != FR_OK)
            {
                put_rc(res);
                break;
            }
        }
        else
        {
            res = f_read(&File1, playback_buf, p1 - bytes_read, &s2);
            if (res != FR_OK)
            {
                put_rc(res);
                break;
            }
        }

        bytes_read += s2;
        int pos = 0;

        switch (switches)
        {
        case 0:
            // display(index, PBACK_NORM);
            // Regular speed
            while (pos < s2)
            {
                // do not play while paused
                while (paused)
                {
                    if (stopped || next || prev)
                    {
                        stopped = 0;
                        playing = 0;
                        paused = 1;
                        return 0;
                    }
                };

                // if stopped (interrupted), return
                if (stopped)
                {
                    stopped = 0;
                    playing = 0;
                    paused = 1;
                    return 0;
                }
                if (next || prev)
                {
                    playing = 1;
                    return 0;
                }
                int fifospace = alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT);
                if (fifospace > 0)
                {
                    l_buf = playback_buf[pos] + (playback_buf[pos + 1] << 8);
                    r_buf = playback_buf[pos + 2] + (playback_buf[pos + 3] << 8);

                    alt_up_audio_write_fifo(audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT);
                    alt_up_audio_write_fifo(audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
                    pos += 4;
                }
            }
            break;
        case 1:
            // Half speed
            // display(index, PBACK_HALF);
            while (pos < s2)
            {
                // do not play while paused
                while (paused)
                {
                    if (stopped || next || prev)
                    {
                        stopped = 0;
                        playing = 0;
                        paused = 1;
                        return 0;
                    }
                };
                if (stopped)
                {
                    stopped = 0;
                    playing = 0;
                    paused = 1;
                    return 0;
                }
                if (next || prev)
                {
                    playing = 1;
                    return 0;
                }

                int fifospace = alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT);
                if (fifospace > 0)
                {
                    l_buf = playback_buf[pos] + (playback_buf[pos + 1] << 8);
                    r_buf = playback_buf[pos + 2] + (playback_buf[pos + 3] << 8);

                    alt_up_audio_write_fifo(audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT);
                    alt_up_audio_write_fifo(audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);

                    alt_up_audio_write_fifo(audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT);
                    alt_up_audio_write_fifo(audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);

                    pos += 4;
                }
            }
            break;
        case 2:
            // Double speed
            // display(index, PBACK_DBL);
            while (pos < s2)
            {
                // do not play while paused
                while (paused)
                {
                    if (stopped || prev || next)
                    {
                        stopped = 0;
                        playing = 0;
                        paused = 1;
                        return 0;
                    }
                };
                if (stopped)
                {
                    stopped = 0;
                    playing = 0;
                    paused = 1;
                    return 0;
                }
                if (next || prev)
                {
                    playing = 1;
                    return 0;
                }

                int fifospace = alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT);
                if (fifospace > 0)
                {
                    l_buf = playback_buf[pos] + (playback_buf[pos + 1] << 8);
                    r_buf = playback_buf[pos + 2] + (playback_buf[pos + 3] << 8);

                    alt_up_audio_write_fifo(audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT);
                    alt_up_audio_write_fifo(audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
                    pos += 8;
                }
            }
            break;
        case 3:
            // Mono - Left OR Right only played on both channels
            // display(index, PBACK_MONO);
            while (pos < s2)
            {
                // do not play while paused
                while (paused)
                {
                    if (stopped || prev || next)
                    {
                        stopped = 0;
                        playing = 0;
                        paused = 1;
                        return 0;
                    }
                };
                if (stopped)
                {
                    stopped = 0;
                    playing = 0;
                    paused = 1;
                    return 0;
                }
                else if (next || prev)
                {
                    return 0;
                }

                int fifospace = alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT);
                if (fifospace > 0)
                {
                    l_buf = playback_buf[pos] + (playback_buf[pos + 1] << 8);

                    alt_up_audio_write_fifo(audio_dev, &(l_buf), 1, ALT_UP_AUDIO_RIGHT);
                    alt_up_audio_write_fifo(audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
                    pos += 4;
                }
            }
            break;

            playing = 0;
            return 0;
        }
    }
}

int main()
{
    int fifospace;
    char *ptr, *ptr2;
    long p1, p2, p3;
    uint8_t res, b1, drv = 0;
    uint16_t w1;
    uint32_t s1, s2, cnt, blen = sizeof(Buff);
    static const uint8_t ft[] = {0, 12, 16, 32};
    uint32_t ofs = 0, sect = 0, blk[2];
    FATFS *fs;

    // register button irq
    alt_irq_register(BUTTON_PIO_IRQ, (void *)0, button_ISR);

    // clear button reg
    IOWR(BUTTON_PIO_BASE, 3, 0x0);

    // enable button irq
    IOWR(BUTTON_PIO_BASE, 2, 0xf);

    // write timer periods
    IOWR(TIMER_0_BASE, 2, 0xffff);
    IOWR(TIMER_0_BASE, 3, 0x00ff);
    IOWR(TIMER_0_BASE, 4, 0x0000);
    IOWR(TIMER_0_BASE, 5, 0x0000);

    // set timer cont 0, IRQ en
    IOWR(TIMER_0_BASE, 1, 0x3);

    // register timer IRQ
    alt_irq_register(TIMER_0_IRQ, (void *)0, timer_ISR);

    alt_up_audio_dev *audio_dev;
    unsigned int l_buf;
    unsigned int r_buf;
    audio_dev = alt_up_audio_open_dev("/dev/Audio");
    if (audio_dev == NULL)
        alt_printf("Error: could not open audio device \n");

    IOWR(SEVEN_SEG_PIO_BASE, 1, 0x0000);

    IoInit();

    initialize();
    load_wav();

    xprintf("total of %d wav files found.\n", num_wav_files);

    int i;
    paused = 1;
    track_num = 0;
    display(track_num, STOPPED);
    while (1)
    {
        while (paused)
        {
            if (next)
            {
                next = 0;
                track_num += 1;
                if (track_num >= num_wav_files)
                {
                    track_num -= num_wav_files;
                }
                display(track_num, STOPPED);
            }
            if (prev)
            {
                prev = 0;
                track_num -= 1;
                if (track_num < 0)
                {
                    track_num += num_wav_files;
                }
                display(track_num, STOPPED);
            }
        };
        play(track_num, &fnames[track_num][0], lengths[track_num], audio_dev);
        while (playing)
        {
            stopped = 0;
            paused = 0;
            if (next)
            {
                next = 0;
                track_num += 1;
                if (track_num >= num_wav_files)
                {
                    track_num -= num_wav_files;
                }
                play(track_num, &fnames[track_num][0], lengths[track_num], audio_dev);
            }
            if (prev)
            {
                prev = 0;
                track_num -= 1;
                if (track_num < 0)
                {
                    track_num += num_wav_files;
                }
                play(track_num, &fnames[track_num][0], lengths[track_num], audio_dev);
            }
        }
        paused = 1;
        display(track_num, STOPPED);
    }
    for (track_num = 0; track_num < num_wav_files; track_num++)
    {
        xprintf("file %d: %s, length: %lu\n", track_num, &fnames[track_num][0], lengths[track_num]);
        play(track_num, &fnames[track_num][0], lengths[track_num], audio_dev);
        while (paused)
        {
        };
    }

    return 0;
}
