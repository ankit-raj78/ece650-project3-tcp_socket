CC = g++
CFLAGS = -Wall -Werror -g -std=c++11

all: ringmaster player

ringmaster: ringmaster.cpp potato.h
	$(CC) $(CFLAGS) -o ringmaster ringmaster.cpp

player: player.cpp potato.h
	$(CC) $(CFLAGS) -o player player.cpp

clean:
	rm -f ringmaster player