#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../include/uapi/simtemp_uapi.h" // Incluye las definiciones de tu driver

#define SIMTEMP_MODE_ONESHOT 0u
#define SIMTEMP_MODE_CONTINUOUS 1u

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
    fprintf(stderr, "  --test                   - Run self-test (sets low threshold and checks alert)\n");
}

static int run_self_test(int fd) {
    const __u32 test_period_ms = 100;
    const __s32 test_threshold_mc = 1000; /* 1°C, guaranteed below nominal temp */
    const int max_attempts = 4;
    const int poll_timeout_ms = (int)(test_period_ms * 2 + 200);
    __u32 original_mode = SIMTEMP_MODE_CONTINUOUS;
    __u32 original_period = 0;
    __s32 original_threshold = 0;
    __u32 mode = SIMTEMP_MODE_CONTINUOUS;
    __u32 period = test_period_ms;
    __s32 threshold = test_threshold_mc;
    int ret = 0;
    bool alert_detected = false;

    /* Capture current configuration so we can restore it afterwards. */
    if (ioctl(fd, SIMTEMP_IOC_GET_MODE, &original_mode) < 0) {
        perror("get_mode failed");
        return -1;
    }
    if (ioctl(fd, SIMTEMP_IOC_GET_PERIOD, &original_period) < 0) {
        perror("get_period failed");
        return -1;
    }
    if (ioctl(fd, SIMTEMP_IOC_GET_THRESHOLD, &original_threshold) < 0) {
        perror("get_threshold failed");
        return -1;
    }

    /* Ensure sampler is stopped before reconfiguration. */
    ioctl(fd, SIMTEMP_IOC_STOP);

    if (ioctl(fd, SIMTEMP_IOC_SET_MODE, &mode) < 0) {
        perror("set_mode failed");
        goto restore;
    }
    if (ioctl(fd, SIMTEMP_IOC_SET_PERIOD, &period) < 0) {
        perror("set_period failed");
        goto restore;
    }
    if (ioctl(fd, SIMTEMP_IOC_SET_THRESHOLD, &threshold) < 0) {
        perror("set_threshold failed");
        goto restore;
    }

    if (ioctl(fd, SIMTEMP_IOC_START) < 0) {
        perror("start failed");
        goto restore;
    }

    for (int attempt = 0; attempt < max_attempts && !alert_detected; ++attempt) {
        struct pollfd pfd = {
            .fd = fd,
            .events = POLLIN | POLLPRI,
            .revents = 0,
        };
        ret = poll(&pfd, 1, poll_timeout_ms);
        if (ret < 0) {
            perror("poll failed");
            break;
        }
        if (ret == 0) {
            fprintf(stderr, "[self-test] Timeout waiting for sample (attempt %d)\n", attempt + 1);
            continue;
        }

        if (pfd.revents & (POLLIN | POLLPRI)) {
            struct simtemp_sample_v1 sample;
            ssize_t bytes = read(fd, &sample, sizeof(sample));
            if (bytes < 0) {
                perror("read failed");
                break;
            }
            if (bytes != sizeof(sample)) {
                fprintf(stderr, "[self-test] Short read: expected %zu, got %zd\n",
                        sizeof(sample), bytes);
                continue;
            }

            printf("[self-test] Sample #%d: temp=%.3f°C flags=0x%08X\n",
                   attempt + 1, sample.temp_mC / 1000.0, sample.flags);
            if (sample.flags & SIMTEMP_FLAG_THR_EDGE) {
                alert_detected = true;
            }
        }
    }

    ioctl(fd, SIMTEMP_IOC_STOP);

    if (!alert_detected) {
        fprintf(stderr, "[self-test] ALERT flag not observed within %d attempts.\n", max_attempts);
        ret = -1;
        goto restore;
    }

    printf("[self-test] PASS: threshold alert detected within %d attempts.\n", max_attempts);
    ret = 0;

restore:
    /* Restore previous configuration. Ignore errors during cleanup. */
    ioctl(fd, SIMTEMP_IOC_STOP);
    ioctl(fd, SIMTEMP_IOC_SET_PERIOD, &original_period);
    ioctl(fd, SIMTEMP_IOC_SET_MODE, &original_mode);
    ioctl(fd, SIMTEMP_IOC_SET_THRESHOLD, &original_threshold);
    return ret;
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
    } else if (strcmp(command_str, "--test") == 0 || strcmp(command_str, "test") == 0) {
        ret = run_self_test(fd);
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
