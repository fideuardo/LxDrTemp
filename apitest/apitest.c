#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include "../include/uapi/simtemp_uapi.h" // Incluye las definiciones de tu driver

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s <device> <command>\n", prog_name);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  start    - Start the sampler\n");
    fprintf(stderr, "  stop     - Stop the sampler\n");
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *device_path = argv[1];
    const char *command_str = argv[2];
    unsigned int cmd;

    if (strcmp(command_str, "start") == 0) {
        cmd = SIMTEMP_IOC_START;
    } else if (strcmp(command_str, "stop") == 0) {
        cmd = SIMTEMP_IOC_STOP;
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command_str);
        print_usage(argv[0]);
        return 1;
    }

    int fd = open(device_path, O_RDWR);
    if (fd < 0) {
        perror("Error opening device");
        return 1;
    }

    if (ioctl(fd, cmd, NULL) < 0) {
        perror("ioctl failed");
        close(fd);
        return 1;
    }

    printf("ioctl command '%s' sent successfully to %s.\n", command_str, device_path);
    close(fd);
    return 0;
}
