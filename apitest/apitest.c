#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include "../include/uapi/simtemp_uapi.h" // Incluye las definiciones de tu driver

static void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s <device> <command> [args]\n", prog_name);
    fprintf(stderr, "\nCommands:\n");
    fprintf(stderr, "  start                    - Start the sampler\n");
    fprintf(stderr, "  stop                     - Stop the sampler\n");
    fprintf(stderr, "  read <N>                 - Read N samples (blocking)\n");
    fprintf(stderr, "  get_mode                 - Get current operation mode\n");
    fprintf(stderr, "  set_mode <0|1>           - Set mode (0=ONESHOT, 1=CONTINUOUS)\n");
    fprintf(stderr, "  get_period               - Get sampling period in ms\n");
    fprintf(stderr, "  set_period <ms>          - Set sampling period in ms\n");
    fprintf(stderr, "  get_threshold            - Get threshold in milli-Celsius\n");
    fprintf(stderr, "  set_threshold <mC>       - Set threshold in milli-Celsius (0-150000)\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *device_path = argv[1];
    const char *command_str = argv[2];

    int fd = open(device_path, O_RDWR);
    if (fd < 0) {
        perror("Error opening device");
        return 1;
    }
    
    int ret = 0;

    if (strcmp(command_str, "start") == 0) {
        ret = ioctl(fd, SIMTEMP_IOC_START);
        if (ret == 0) printf("Sampler started.\n");
    } else if (strcmp(command_str, "stop") == 0) {
        ret = ioctl(fd, SIMTEMP_IOC_STOP);
        if (ret == 0) printf("Sampler stopped.\n");
    } else if (strcmp(command_str, "read") == 0) {
        if (argc != 4) { print_usage(argv[0]); ret = -1; goto out; }
        int num_samples = atoi(argv[3]);
        if (num_samples <= 0) { print_usage(argv[0]); ret = -1; goto out; }
        
        struct simtemp_sample_v1 *samples = malloc(num_samples * sizeof(*samples));
        if (!samples) { perror("malloc"); ret = -1; goto out; }

        printf("Reading up to %d samples...\n", num_samples);
        ssize_t bytes_read = read(fd, samples, num_samples * sizeof(*samples));
        if (bytes_read < 0) {
            perror("read failed");
            free(samples);
            ret = -1;
            goto out;
        }
        
        int samples_read = bytes_read / sizeof(struct simtemp_sample_v1);
        printf("Read %d samples:\n", samples_read);
        for (int i = 0; i < samples_read; i++) {
            printf("  - T: %.3f C, TS: %llu, Flags: 0x%X\n",
                   (double)samples[i].temp_mC / 1000.0,
                   samples[i].timestamp_ns,
                   samples[i].flags);
        }
        free(samples);

    } else if (strcmp(command_str, "get_mode") == 0) {
        __u32 mode;
        ret = ioctl(fd, SIMTEMP_IOC_GET_MODE, &mode);
        if (ret == 0) printf("Current mode: %s\n", mode == 0 ? "one-shot" : "continuous");
    } else if (strcmp(command_str, "set_mode") == 0) {
        if (argc != 4) { print_usage(argv[0]); ret = -1; goto out; }
        __u32 mode = atoi(argv[3]);
        ret = ioctl(fd, SIMTEMP_IOC_SET_MODE, &mode);
        if (ret == 0) printf("Mode set successfully.\n");
    } else if (strcmp(command_str, "get_period") == 0) {
        __u32 period;
        ret = ioctl(fd, SIMTEMP_IOC_GET_PERIOD, &period);
        if (ret == 0) printf("Current period: %u ms\n", period);
    } else if (strcmp(command_str, "set_period") == 0) {
        if (argc != 4) { print_usage(argv[0]); ret = -1; goto out; }
        __u32 period = atoi(argv[3]);
        ret = ioctl(fd, SIMTEMP_IOC_SET_PERIOD, &period);
        if (ret == 0) printf("Period set successfully.\n");
    } else if (strcmp(command_str, "get_threshold") == 0) {
        __s32 threshold;
        ret = ioctl(fd, SIMTEMP_IOC_GET_THRESHOLD, &threshold);
        if (ret == 0) printf("Current threshold: %d mC\n", threshold);
    } else if (strcmp(command_str, "set_threshold") == 0) {
        if (argc != 4) { print_usage(argv[0]); ret = -1; goto out; }
        __s32 threshold = atoi(argv[3]);
        ret = ioctl(fd, SIMTEMP_IOC_SET_THRESHOLD, &threshold);
        if (ret == 0) printf("Threshold set successfully.\n");
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command_str);
        print_usage(argv[0]);
        ret = -1;
    }

    if (ret < 0) {
        perror("Operation failed");
    }

out:
    close(fd);
    return ret < 0 ? 1 : 0;
}
