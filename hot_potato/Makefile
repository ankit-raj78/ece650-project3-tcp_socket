CXX = g++
CXXFLAGS = -Wall -Wextra -g -std=c++11

all: ringmaster player

ringmaster: ringmaster.cpp potato.h network_utils.h
	$(CXX) $(CXXFLAGS) -o ringmaster ringmaster.cpp

player: player.cpp potato.h network_utils.h
	$(CXX) $(CXXFLAGS) -o player player.cpp

clean:
	rm -f ringmaster player *.o

.PHONY: all clean