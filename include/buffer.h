#ifndef BUFFER_H
# define BUFFER_H
# define BLOCK_SIZE 512
# define BLOCKS 1000

#ifndef NULL
# define NULL (void *)0
#endif

typedef struct _block block_t;

struct _block {
    char data[BLOCK_SIZE];
    int block_nr;
    int dirty;
    union {
        int access_count;
    };
    struct _block *next;
};

void *buffer_init(void);
block_t *find_block(int block_nr);
block_t *replace_block(int (*policy)(block_t *cur_victim, block_t *cur_pos));

#endif
