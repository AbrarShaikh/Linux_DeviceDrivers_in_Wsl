#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#define DEVICE_PATH "/dev/pseudomisc"

int main(void)
{
    int fd;
    char write_buf[] = "Hello from miscdevice user-space!";
    char read_buf[100];
    off_t offset;
    ssize_t ret;

    printf("Opening %s...\n", DEVICE_PATH);
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    printf("Writing data: '%s'\n", write_buf);
    ret = write(fd, write_buf, strlen(write_buf));
    if (ret < 0) {
        perror("Failed to write to device");
        close(fd);
        return 1;
    }
    printf("Wrote %zd bytes\n", ret);

    printf("Seeking to start...\n");
    offset = lseek(fd, 0, SEEK_SET);
    if (offset < 0) {
        perror("Failed to seek");
        close(fd);
        return 1;
    }
    printf("Current offset after seek: %lld\n", (long long)offset);

    memset(read_buf, 0, sizeof(read_buf));
    ret = read(fd, read_buf, strlen(write_buf));
    if (ret < 0) {
        perror("Failed to read from device");
        close(fd);
        return 1;
    }
    printf("Read %zd bytes: '%s'\n", ret, read_buf);

    if (strcmp(write_buf, read_buf) != 0) {
        fprintf(stderr, "Error: read data does not match written data!\n");
        close(fd);
        return 1;
    }
    printf("Success: Read data matches written data.\n");

    printf("Testing seek to offset 100...\n");
    offset = lseek(fd, 100, SEEK_SET);
    if (offset < 0) {
        perror("Failed to seek to 100");
    } else {
        printf("Current offset: %lld\n", (long long)offset);
    }

    printf("Closing device...\n");
    close(fd);
    printf("Tests complete!\n");
    return 0;
}
