// potato.h
#ifndef POTATO_H
#define POTATO_H

#define MAX_HOPS 512
#define MAX_HOSTNAME 128

typedef struct {
    int hops;                // Number of remaining hops
    int trace_count;         // Number of players in the trace
    int trace[MAX_HOPS];     // Array to store the trace of player IDs
} Potato;

typedef struct {
    char hostname[MAX_HOSTNAME];  // Player's hostname
    int port;                    // Player's port number
    int id;                      // Player's ID
} PlayerInfo;

#endif