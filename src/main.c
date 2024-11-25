#include <stdlib.h>
#include <stdio.h>
#include "io.h"
#include "buffer.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

char *disk_buffer;
int disk_fd;

int init()
{
	disk_buffer = aligned_alloc(BLOCK_SIZE, BLOCK_SIZE);
	if (disk_buffer == NULL)
		return -errno;

	printf("disk_buffer: %p\n", disk_buffer);

	disk_fd = open("diskfile", O_RDWR | O_CREAT, 0644);
	if (disk_fd < 0)
		return -1;
    return disk_fd;
}

int main (int argc, char *argv[])
{
    char *buffer;
    int ret;

    init();

    buffer = malloc(BLOCK_SIZE);
    buffer_init();

    // TODO: Implement test code

    ret = lib_read(0, buffer);
    printf("nread: %d\n", ret);
    printf("buffer: %s\n", buffer);

    ret = lib_read(1, buffer);
    printf("nread: %d\n", ret);
    printf("buffer: %s\n", buffer);

    ret = lib_read(30, buffer);
    printf("nread: %d\n", ret);
    printf("buffer: %s\n", buffer);

    ret = lib_read(2, buffer);
    printf("nread: %d\n", ret);

    ret = lib_read(3, buffer);
    printf("nread: %d\n", ret);

    ret = lib_write(0, buffer);
    printf("nwrite: %d\n", ret);

    return 0;
}