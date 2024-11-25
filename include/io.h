//
// Created by James Hong on 11/4/24.
//

#ifndef IO_H
#define IO_H

int os_read(int block_nr, char *user_buffer);
int os_write(int block_nr, char *user_buffer);
int lib_read(int block_nr, char *user_buffer);
int lib_write(int block_nr, char *user_buffer);

#endif //IO_H
