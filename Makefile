TARGET 	= buffer

CC 			= gcc
SRCS 	= $(shell find ./src 	-type f -name *.c)
HEADS = $(shell find ./include 	-type f -name *.h)
HEAD_DIR = include
OBJS = $(SRCS:.c=.o)

CFLAGS = -Wall -Wextra -Werror
LDFLAGS = -lpthread -lm

all		: $(TARGET)

$(TARGET)	: $(OBJS) $(HEADS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS) -I include

%.o 	: %.c
	$(CC) -c $< -o $@ -I include

clean	:
	$(RM) $(OBJS)
	$(RM) $(TARGET)