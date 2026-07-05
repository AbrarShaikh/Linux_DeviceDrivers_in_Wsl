#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#define DEVICE_PATH "/dev/pseudodev"

int main() {
    int fd;
    char write_buf[] = "Hello from User Space!";
    char read_buf[100];
    off_t offset;
    ssize_t ret;

    printf("Opening %s...\n", DEVICE_PATH);
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    // Test 1: Write to device
    printf("Writing data: '%s'\n", write_buf);
    ret = write(fd, write_buf, strlen(write_buf));
    if (ret < 0) {
        perror("Failed to write to device");
        close(fd);
        return 1;
    }
    printf("Wrote %zd bytes\n", ret);

    // Test 2: Seek to beginning
    printf("Seeking to start...\n");
    offset = lseek(fd, 0, SEEK_SET);
    if (offset < 0) {
        perror("Failed to seek");
        close(fd);
        return 1;
    }
    printf("Current offset after seek: %lld\n", (long long)offset);

    // Test 3: Read from device
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

    // Test 4: Seek past end but within buffer limit (4096)
    printf("Seeking to offset 100...\n");
    offset = lseek(fd, 100, SEEK_SET);
    if (offset < 0) {
        perror("Failed to seek to 100");
    } else {
        printf("Current offset: %lld\n", (long long)offset);
    }

    // Test 5: Write at offset 100
    char write_buf2[] = "Data at offset 100";
    printf("Writing at offset 100: '%s'\n", write_buf2);
    ret = write(fd, write_buf2, strlen(write_buf2));
    if (ret < 0) {
        perror("Failed to write at offset 100");
    } else {
        printf("Wrote %zd bytes at offset 100\n", ret);
    }

    // Test 6: Seek SEEK_END
    printf("Seeking to SEEK_END...\n");
    offset = lseek(fd, 0, SEEK_END);
    if (offset < 0) {
        perror("Failed to seek to SEEK_END");
    } else {
        printf("End offset: %lld (Expected: 118)\n", (long long)offset);
    }

    // Test 7: Invalid seek (negative)
    printf("Testing invalid seek (negative offset)...\n");
    offset = lseek(fd, -10, SEEK_SET);
    if (offset < 0) {
        printf("Invalid seek correctly failed with error: %s\n", strerror(errno));
    } else {
        fprintf(stderr, "Error: Invalid seek succeeded! Offset: %lld\n", (long long)offset);
    }

    // Test 8: Invalid seek (beyond buffer size 4096)
    printf("Testing invalid seek (beyond buffer size 4096)...\n");
    offset = lseek(fd, 4097, SEEK_SET);
    if (offset < 0) {
        printf("Invalid seek correctly failed with error: %s\n", strerror(errno));
    } else {
        fprintf(stderr, "Error: Invalid seek succeeded! Offset: %lld\n", (long long)offset);
    }

    close(fd);
    printf("Tests complete!\n");
    return 0;
}
