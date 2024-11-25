#include "buffer.h"
#include "io.h"
#include <stdlib.h>
#include <stdio.h>

block_t *buffer_cache = NULL;

void *buffer_init(void) {
    // TODO : Implement buffer_init
    buffer_cache = malloc(sizeof(block_t) * BLOCKS);
    if (!buffer_cache)
        exit(-1);
    for(int i = 0; i < BLOCKS; i++) {
        buffer_cache[i].block_nr = i;
        buffer_cache[i].dirty = 0;
        buffer_cache[i].access_count = 0;
        buffer_cache[i].next = (i < BLOCKS - 1) ? &(buffer_cache[i+1]) : NULL;
    }
    return buffer_cache;
}

block_t *find_block(int block_nr) {
    // TODO : Implement find_block
    block_t *cur = buffer_cache;

    while(cur){
        if (cur->block_nr == block_nr) {
            cur->access_count++;
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

block_t *replace_block(int (*policy)(block_t *cur_victim, block_t *cur_pos)) {
    // TODO : Implement replace_block
    block_t *cur = buffer_cache;
    block_t *victim = NULL;

    while(cur){
        if (!victim || policy(victim, cur))
            victim = cur;
        cur = cur->next;
    }
    if (victim->dirty)
        os_write(victim->block_nr, victim->data);
    victim->dirty = 0;
    victim->access_count = 0;
    return victim;
}