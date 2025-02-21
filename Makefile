# Makefile
CC = gcc
CFLAGS = -Wall -Werror -g
DEPS = potato.h

all: ringmaster player

ringmaster: ringmaster.c $(DEPS)
	$(CC) $(CFLAGS) -o ringmaster ringmaster.c

player: player.c $(DEPS)
	$(CC) $(CFLAGS) -o player player.c

clean:
	rm -f ringmaster player *.o