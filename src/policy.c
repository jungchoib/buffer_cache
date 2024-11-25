#include "buffer.h"

//TODO : Everything
int lfu(block_t *cur_victim, block_t *cur_pos) {
    return (cur_victim->access_count < cur_pos->access_count);
}

int random(block_t *cur_victim, block_t *cur_pos) {
    return 1; // random함수로 변경
}