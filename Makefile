CC = g++
CFLAGS = -Wall -g -std=c++11
OBJECTS = ringmaster.o player.o

all: ringmaster player

ringmaster: ringmaster.cpp potato.hpp
	$(CC) $(CFLAGS) -o ringmaster ringmaster.cpp

player: player.cpp potato.hpp
	$(CC) $(CFLAGS) -o player player.cpp

clean:
	rm -f *.o ringmaster player