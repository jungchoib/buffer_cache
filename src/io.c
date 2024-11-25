#include "io.h"
#include "buffer.h"
#include <unistd.h>
#include <string.h>
#include "policy.h"

extern int disk_fd;
extern char *disk_buffer;

int os_read(int block_nr, char *user_buffer)
{
    int ret; // 읽은 바이트 수

    // TODO : implement BUFFERED_READ

    ret = lseek(disk_fd, block_nr * BLOCK_SIZE, SEEK_SET);
    if (ret < 0)
        return ret;

    ret = read(disk_fd, disk_buffer, BLOCK_SIZE);
    if (ret < 0)
        return ret;

    memcpy(user_buffer, disk_buffer, BLOCK_SIZE);

    return BLOCK_SIZE;
}

int os_write(int block_nr, char *user_buffer)
{
    int ret;

    // TODO : implement BUFFERED_WRITE

    ret = lseek(disk_fd, block_nr * BLOCK_SIZE, SEEK_SET);
    if (ret < 0)
        return ret;

    ret = write(disk_fd, user_buffer, BLOCK_SIZE);
    if (ret < 0)
        return ret;

    return BLOCK_SIZE;
}

int lib_read(int block_nr, char *user_buffer)
{
    int ret = 0;
    block_t *blk = find_block(block_nr);
    // 캐시에 있으면 바로 읽
    if (blk) {
        memcpy(user_buffer, blk->data, BLOCK_SIZE);
        return BLOCK_SIZE;
    }// 없으면 victim에다 저장 후 읽기
    blk = replace_block(lfu);
    if (!blk)
        return -1;
    ret = os_read(block_nr, user_buffer);
    if (ret < 0)
        return ret;
    // victim update
    blk->block_nr = block_nr;
    blk->dirty = 0;
    blk->access_count = 1;
    memcpy(user_buffer, blk->data, BLOCK_SIZE);
    return BLOCK_SIZE;
}

int lib_write(int block_nr, char *user_buffer)
{
    int ret;
    block_t *blk = find_block(block_nr);
    // 캐시에 있으면 바로 쓰
    if (blk) {
        memcpy(blk->data, user_buffer, BLOCK_SIZE);
        blk->dirty = 1;
        blk->access_count++;
        return BLOCK_SIZE;
    }// 없으면 victim에다 저장 후 쓰기
    blk = replace_block(lfu);
    if (!blk)
        return -1;
    // victim update
    memcpy(blk->data, user_buffer, BLOCK_SIZE);
    blk->block_nr = block_nr;
    blk->dirty = 1;
    blk->access_count = 1;
    return os_write(block_nr, blk->data);;
}