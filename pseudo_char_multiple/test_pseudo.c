#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#define MAX_DEVICES 4

int main() {
    int fds[MAX_DEVICES];
    char dev_paths[MAX_DEVICES][30];
    char write_bufs[MAX_DEVICES][100] = {
        "Device 0: First pseudo character device buffer contents.",
        "Device 1: Second pseudo character device, completely independent.",
        "Device 2: Third pseudo character device in sequence.",  // Fixed device label
        "Device 3: Fourth and final pseudo character device buffer."
    };
    char read_buf[100];
    ssize_t ret;
    off_t offset;
    int i;

    printf("==================================================\n");
    printf("   Starting Multi-Device Pseudo Driver Test\n");
    printf("==================================================\n\n");

    /* 1. Open all devices */
    for (i = 0; i < MAX_DEVICES; i++) {
        snprintf(dev_paths[i], sizeof(dev_paths[i]), "/dev/pseudodev%d", i);
        printf("Opening %s...\n", dev_paths[i]);
        fds[i] = open(dev_paths[i], O_RDWR);
        if (fds[i] < 0) {
            fprintf(stderr, "Failed to open %s: %s (Is the module loaded?)\n", dev_paths[i], strerror(errno));
            /* Close any previously opened descriptors */
            while (--i >= 0) close(fds[i]);
            return 1;
        }
    }
    printf("All 4 devices opened successfully.\n\n");

    /* 2. Write unique data to each device */
    for (i = 0; i < MAX_DEVICES; i++) {
        printf("Writing to %s: '%s'\n", dev_paths[i], write_bufs[i]);
        ret = write(fds[i], write_bufs[i], strlen(write_bufs[i]));
        if (ret < 0) {
            fprintf(stderr, "Failed to write to %s: %s\n", dev_paths[i], strerror(errno));
            goto cleanup;
        }
        if (ret != strlen(write_bufs[i])) {
            fprintf(stderr, "Partial write to %s: wrote %zd instead of %zu bytes\n", dev_paths[i], ret, strlen(write_bufs[i]));
            goto cleanup;
        }
        printf("Wrote %zd bytes to %s\n", ret, dev_paths[i]);
    }
    printf("All writes completed successfully.\n\n");

    /* 3. Read back from each device and verify independence */
    for (i = 0; i < MAX_DEVICES; i++) {
        /* Seek to beginning first */
        printf("Seeking %s to start (offset 0)...\n", dev_paths[i]);
        offset = lseek(fds[i], 0, SEEK_SET);
        if (offset < 0) {
            fprintf(stderr, "Failed to seek on %s: %s\n", dev_paths[i], strerror(errno));
            goto cleanup;
        }

        memset(read_buf, 0, sizeof(read_buf));
        size_t len_to_read = strlen(write_bufs[i]);
        printf("Reading %zu bytes from %s...\n", len_to_read, dev_paths[i]);

        size_t total_read = 0;
        while (total_read < len_to_read) {
            ret = read(fds[i], read_buf + total_read, len_to_read - total_read);
            if (ret < 0) {
                fprintf(stderr, "Failed to read from %s: %s\n", dev_paths[i], strerror(errno));
                goto cleanup;
            }
            if (ret == 0) {
                break; /* EOF reached early */
            }
            total_read += ret;
        }
        printf("Read %zu bytes from %s: '%s'\n", total_read, dev_paths[i], read_buf);

        if (total_read != len_to_read || strcmp(write_bufs[i], read_buf) != 0) {
            fprintf(stderr, "ERROR: Data read from %s does not match written data!\n", dev_paths[i]);
            fprintf(stderr, "Expected: '%s'\n", write_bufs[i]);
            fprintf(stderr, "Got:      '%s'\n", read_buf);
            goto cleanup;
        }
        printf("SUCCESS: Verified contents for %s.\n\n", dev_paths[i]);
    }

    /* 4. Seek test on one device to verify it doesn't affect the file pos of another */
    printf("--- Specific File Position & Seek Independence Tests ---\n");
    printf("Seeking /dev/pseudodev0 to offset 10...\n");
    offset = lseek(fds[0], 10, SEEK_SET);
    if (offset != 10) {
        fprintf(stderr, "Failed to seek /dev/pseudodev0 to 10\n");
        goto cleanup;
    }

    /* Check file pos of dev 1 */
    printf("Checking offset of /dev/pseudodev1 (should still be at end of read, i.e., %zu)...\n", strlen(write_bufs[1]));
    offset = lseek(fds[1], 0, SEEK_CUR);
    if (offset != strlen(write_bufs[1])) {
        fprintf(stderr, "ERROR: /dev/pseudodev1 offset affected! Got %lld, expected %zu\n", (long long)offset, strlen(write_bufs[1]));
        goto cleanup;
    }
    printf("SUCCESS: /dev/pseudodev1 offset is independent.\n\n");

    /* 5. SEEK_END test */
    printf("Checking SEEK_END on /dev/pseudodev0...\n");
    offset = lseek(fds[0], 0, SEEK_END);
    if (offset < 0) {
        fprintf(stderr, "Failed to seek to SEEK_END on /dev/pseudodev0\n");
        goto cleanup;
    }
    printf("/dev/pseudodev0 logical end position is: %lld (Expected: %zu)\n", (long long)offset, strlen(write_bufs[0]));
    if (offset != strlen(write_bufs[0])) {
        fprintf(stderr, "ERROR: SEEK_END returned unexpected position!\n");
        goto cleanup;
    }
    printf("SUCCESS: SEEK_END returned correct size.\n\n");

    /* 6. Invalid seek tests */
    printf("Testing invalid seek (negative offset) on /dev/pseudodev0...\n");
    offset = lseek(fds[0], -5, SEEK_SET);
    if (offset < 0) {
        printf("Invalid seek correctly failed with: %s\n", strerror(errno));
    } else {
        fprintf(stderr, "ERROR: Invalid seek (negative) succeeded! Offset: %lld\n", (long long)offset);
        goto cleanup;
    }

    printf("Testing invalid seek (beyond buffer limit 4096) on /dev/pseudodev0...\n");
    offset = lseek(fds[0], 4097, SEEK_SET);
    if (offset < 0) {
        printf("Invalid seek correctly failed with: %s\n", strerror(errno));
    } else {
        fprintf(stderr, "ERROR: Invalid seek (beyond buffer) succeeded! Offset: %lld\n", (long long)offset);
        goto cleanup;
    }

    printf("\n==================================================\n");
    printf("   All Multi-Device Tests Passed Successfully!\n");
    printf("==================================================\n");

    for (i = 0; i < MAX_DEVICES; i++) close(fds[i]);
    return 0;

cleanup:
    for (i = 0; i < MAX_DEVICES; i++) close(fds[i]);
    return 1;
}
