#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

#define BLOCK_SIZE 4096
#define MAX_BUFFERS 3
#define HASH_TABLE_SIZE 3
#define BLOCK_ACCESS_SEQUENCE_LENGTH 20

typedef struct buffer {
    char *data;
    int block_nr;
    int dirty;
    int access_count; // For LFU
    int last_access_time; // For LRU
    struct buffer *next; // For hash table chaining
} buffer_t;

typedef struct {
    buffer_t *buffer;
} hash_table_entry_t;

char *flush_buffer;
buffer_t buffer_cache[MAX_BUFFERS];
int buffer_count = 0;
int hit_count = 0;
pthread_mutex_t buffer_lock = PTHREAD_MUTEX_INITIALIZER;
hash_table_entry_t hash_table[HASH_TABLE_SIZE];

_Atomic int flush_nr;
pthread_cond_t flush;
pthread_cond_t finish;
pthread_mutex_t flush_lock;
_Atomic int finished;

int disk_fd;

unsigned int hash(int block_nr) {
    return block_nr % HASH_TABLE_SIZE;
}

void insert_into_hash_table(buffer_t *buffer) {
    unsigned int index = hash(buffer->block_nr);
    buffer->next = hash_table[index].buffer;
    hash_table[index].buffer = buffer;
}

buffer_t *find_buffer(int block_nr) {
    unsigned int index = hash(block_nr);
    buffer_t *current = hash_table[index].buffer;

    while (current != NULL) {
        if (current->block_nr == block_nr) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// FIFO replacement algorithm
int select_victim_fifo() {
    static int next_victim = 0;
    int victim = next_victim;
    next_victim = (next_victim + 1) % MAX_BUFFERS;
    return victim;
}

// LRU replacement algorithm
int select_victim_lru() {
    int lru = 0, min_time = buffer_cache[0].last_access_time;
    for (int i = 1; i < MAX_BUFFERS; i++) {
        if (buffer_cache[i].last_access_time < min_time) {
            lru = i;
            min_time = buffer_cache[i].last_access_time;
        }
    }
    return lru;
}

// LFU replacement algorithm
int select_victim_lfu() {
    int lfu = 0, min_count = buffer_cache[0].access_count;
    for (int i = 1; i < MAX_BUFFERS; i++) {
        if (buffer_cache[i].access_count < min_count) {
            lfu = i;
            min_count = buffer_cache[i].access_count;
        }
    }
    return lfu;
}

void print_buffer_state() {
    printf("Buffer Cache (Hash Table) : [");
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        if (hash_table[i].buffer != NULL) {
            // If there's at least one buffer in this hash table entry
            buffer_t *current = hash_table[i].buffer;
            printf("%d", current->block_nr);
            current = current->next;

            // Print additional buffers if they are chained in this hash table entry
            while (current != NULL) {
                printf(", %d", current->block_nr);
                current = current->next;
            }
        }
        if (i < HASH_TABLE_SIZE - 1) {
            printf(", "); // Separator for array elements
        }
    }
    printf("]\n");
}

// Function to write a buffer to disk
void write_buffer_to_disk(buffer_t *buffer) {
    pthread_mutex_lock(&flush_lock);
    while (flush_nr == buffer->block_nr)
        pthread_cond_wait(&finish, &flush_lock);
    pthread_mutex_unlock(&flush_lock);

    memset(flush_buffer, 0, BLOCK_SIZE);
    flush_nr = buffer->block_nr;
    memcpy(flush_buffer, buffer->data, BLOCK_SIZE);

    pthread_cond_signal(&flush);
}

int read_from_disk(buffer_t *buffer) {
    int ret;

    pthread_mutex_lock(&flush_lock);
    while (flush_nr == buffer->block_nr)
        pthread_cond_wait(&finish, &flush_lock);
    pthread_mutex_unlock(&flush_lock);
    ret = read(disk_fd, buffer->data, BLOCK_SIZE);
    return ret;
}

void *flush_to_disk(void *arg) {
    int ret;

    while (finished) {
        pthread_mutex_lock(&flush_lock);
        while (flush_nr == -1)
            pthread_cond_wait(&flush, &flush_lock);
        pthread_mutex_unlock(&flush_lock);
        if (finished == 0)
            break;
        ret = lseek(disk_fd, flush_nr * BLOCK_SIZE, SEEK_SET);
        if (ret < 0) {
            flush_nr = -1;
            pthread_cond_signal(&finish);
            continue;
        }
        ret = write(disk_fd, flush_buffer, BLOCK_SIZE);
        if (ret < 0) {
            flush_nr = -1;
            pthread_cond_signal(&finish);
            continue;
        }
        flush_nr = -1;
        pthread_cond_signal(&finish);
    }
    return NULL;
}

// Function to reclaim a buffer
int reclaim_buffer() {
    // Select a victim buffer
    int victim_idx = select_victim_fifo(); // Change this for different policies
    buffer_t *victim = &buffer_cache[victim_idx];

    if (victim->dirty)
        write_buffer_to_disk(victim);

    // Remove from hash table
    unsigned int index = hash(victim->block_nr);
    buffer_t *current = hash_table[index].buffer, *prev = NULL;
    while (current != NULL) {
        if (current == victim) {
            if (prev == NULL) {
                hash_table[index].buffer = current->next;
            } else {
                prev->next = current->next;
            }
            break;
        }
        prev = current;
        current = current->next;
    }

    return victim_idx;
}

int os_read(int block_nr, char *user_buffer) {
    int ret;

    buffer_t *buffer = find_buffer(block_nr);
    if (!buffer) { // Not in cache, need to read from disk
        ret = lseek(disk_fd, block_nr * BLOCK_SIZE, SEEK_SET);
        if (ret < 0) {
            // printf("Out of bound\n");
            return ret;
        }
        if (buffer_count < MAX_BUFFERS) {
            buffer = &buffer_cache[buffer_count++];
        } else {
            int idx = reclaim_buffer();
            buffer = &buffer_cache[idx];
        }
        memset(buffer->data, 0, BLOCK_SIZE);
        buffer->block_nr = block_nr;
        buffer->access_count = 0;
        buffer->dirty = 0;
        ret = read_from_disk(buffer);
        if (ret < 0) {
            printf("Error reading from disk\n");
            return ret;
        }
        insert_into_hash_table(buffer);
        printf("Block %d: Read from Disk\n", block_nr); // Indicate read from disk
    } else {
        hit_count++;
        printf("Block %d: Readfrom Buffer Cache\n", block_nr); // Indicate read from buffer cache
    }

    // Update access metadata
    buffer->last_access_time = time(NULL);
    buffer->access_count++;
    memcpy(user_buffer, buffer->data, BLOCK_SIZE);
    return BLOCK_SIZE;
}

int os_write(int block_nr, char *user_buffer) {
    int ret;
    buffer_t *buffer;

    buffer = find_buffer(block_nr);
    if (!buffer) { // Not in cache
        ret = lseek(disk_fd, block_nr * BLOCK_SIZE, SEEK_SET);
        if (ret < 0) {
            // printf("Out of bound\n");
            return ret;
        }

        if (buffer_count < MAX_BUFFERS) {
            buffer = &buffer_cache[buffer_count++];
        } else {
            int idx = reclaim_buffer();
            buffer = &buffer_cache[idx];
        }
        buffer->block_nr = block_nr;
        buffer->access_count = 0;
        read_from_disk(buffer);
        insert_into_hash_table(buffer);
        // printf("Block %d: Write from Disk\n", block_nr); // Indicate read from disk
    } else {
        hit_count++;
        // printf("Block %d: Write from Buffer Cache\n", block_nr); // Indicate read from buffer cache
    }


    memcpy(buffer->data, user_buffer, BLOCK_SIZE);
    buffer->dirty = 1;

    // Update access metadata
    buffer->last_access_time = time(NULL);
    buffer->access_count++;


    return BLOCK_SIZE;
}

int lib_read(int block_nr, char *user_buffer) {
    int ret;
    clock_t begin = clock();
    ret = os_read(block_nr, user_buffer);
    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    // printf("Time spent: %f\n", time_spent);
    return ret;
}

int lib_write(int block_nr, char *user_buffer) {
    int ret;
    clock_t begin = clock();
    ret = os_write(block_nr, user_buffer);
    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    // printf("Time spent: %f\n", time_spent);
    return ret;
}

int init() {
    // Initialize the hash table
    memset(hash_table, 0, sizeof(hash_table_entry_t) * HASH_TABLE_SIZE);

    for (int i = 0; i < MAX_BUFFERS; i++) {
        buffer_cache[i].data = aligned_alloc(BLOCK_SIZE, BLOCK_SIZE);
        buffer_cache[i].next = NULL;
    }
    // Allocate and open disk file
    disk_fd = open("diskfile", O_RDWR);// mac은 O_DIRECT생략
    if (disk_fd < 0) {
        return -errno;
    }

    pthread_mutex_init(&flush_lock, NULL);
    pthread_cond_init(&flush, NULL);
    pthread_cond_init(&finish, NULL);

    flush_buffer = aligned_alloc(BLOCK_SIZE, BLOCK_SIZE);
    flush_nr = -1;
    finished = 1;
    return 0;
}

void generate_normal_distribution_block_access(int *sequence, int length, int mean, int stddev) {
    srand(time(NULL));
    for (int i = 0; i < length; i++) {
        float u1 = (float)rand() / RAND_MAX;
        float u2 = (float)rand() / RAND_MAX;
        float z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
        int block_nr = (int)(mean + stddev * z);
        sequence[i] = block_nr >= 0 ? block_nr : 0; // Ensure non-negative block numbers
    }
}

int main(int argc, char *argv[]) {
    char *buffer;
    int ret;
    pthread_t flush_thread = 0;

    init();

    buffer = malloc(BLOCK_SIZE);

    pthread_create(&flush_thread, NULL, flush_to_disk, NULL);

    // Generate and process block access sequence
    int block_access_sequence[BLOCK_ACCESS_SEQUENCE_LENGTH];
    generate_normal_distribution_block_access(block_access_sequence, BLOCK_ACCESS_SEQUENCE_LENGTH, 25, 5);

    printf("Read Test\n\n");

    printf("Block Access Sequence: ");
    for (int i = 0; i < BLOCK_ACCESS_SEQUENCE_LENGTH; i++)
        printf("%d ", block_access_sequence[i]);
    printf("\n");

    for (int i = 0; i < BLOCK_ACCESS_SEQUENCE_LENGTH; i++) {
        // printf("Block %d\n", block_access_sequence[i]);
        ret = lib_read(block_access_sequence[i], buffer);
        print_buffer_state();
        // printf("Block %d: Read %d bytes\n", block_access_sequence[i], ret);
        // printf("buffer: %s\n", buffer);
    }
    printf("Hit ratio : %d / %d\n\n", hit_count, BLOCK_ACCESS_SEQUENCE_LENGTH);

    printf("Write Test\n\n");
    generate_normal_distribution_block_access(block_access_sequence, BLOCK_ACCESS_SEQUENCE_LENGTH, 25, 5);
    printf("Block Access Sequence: ");
    for (int i = 0; i < BLOCK_ACCESS_SEQUENCE_LENGTH; i++)
        printf("%d ", block_access_sequence[i]);
    printf("\n");

    for (int i = 0; i < BLOCK_ACCESS_SEQUENCE_LENGTH; i++) {
        // printf("Block %d\n", block_access_sequence[i]);
        int rand = random() % 2;
        if (rand)
            ret = lib_write(block_access_sequence[i] % 25, buffer);
        else
            ret = lib_read(block_access_sequence[i] % 25, buffer);
        // print_buffer_state();
        // printf("Block %d: Read %d bytes\n", block_access_sequence[i], ret);
        // printf("buffer: %s\n", buffer);
    }
    printf("Hit ratio : %d / %d\n\n", hit_count, BLOCK_ACCESS_SEQUENCE_LENGTH);

    finished = 0;
    pthread_cancel(flush_thread);
    pthread_mutex_destroy(&flush_lock);
    pthread_cond_destroy(&flush);
    pthread_cond_destroy(&finish);
    return 0;
}
