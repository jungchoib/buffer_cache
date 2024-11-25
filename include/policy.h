#ifndef POLICY_H
# define POLICY_H
#include "buffer.h"
int lfu(block_t *cur_victim, block_t *cur_pos);
int random(block_t *cur_victim, block_t *cur_pos);
#endif