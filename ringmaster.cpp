#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <algorithm>
#include "potato.hpp"

#define MAX_BUFFER 1024

class Ringmaster {
private:
    int port_num;
    int num_players;
    int num_hops;
    int listener_fd;
    std::vector<int> player_fds;
    std::vector<struct sockaddr_in> player_addrs;

public:
    Ringmaster(int port, int players, int hops) : 
        port_num(port), num_players(players), num_hops(hops), listener_fd(-1) {
        player_fds.resize(players, -1);
        player_addrs.resize(players);
    }

    ~Ringmaster() {
        // Close all connections
        for (int fd : player_fds) {
            if (fd != -1) close(fd);
        }
        if (listener_fd != -1) close(listener_fd);
    }

    void setupServer() {
        struct sockaddr_in addr;
        listener_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_fd < 0) {
            perror("Error creating socket");
            exit(EXIT_FAILURE);
        }

        // Set socket option to reuse address
        int yes = 1;
        setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_num);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(listener_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("Error binding socket");
            close(listener_fd);
            exit(EXIT_FAILURE);
        }

        if (listen(listener_fd, num_players) < 0) {
            perror("Error listening");
            close(listener_fd);
            exit(EXIT_FAILURE);
        }

        std::cout << "Potato Ringmaster\n";
        std::cout << "Players = " << num_players << std::endl;
        std::cout << "Hops = " << num_hops << std::endl;
    }

    void acceptConnections() {
        // Accept connections from all players
        for (int i = 0; i < num_players; i++) {
            struct sockaddr_in player_addr;
            socklen_t addr_len = sizeof(player_addr);
            
            int player_fd = accept(listener_fd, (struct sockaddr*)&player_addr, &addr_len);
            if (player_fd < 0) {
                perror("Error accepting connection");
                exit(EXIT_FAILURE);
            }

            // Receive player's initial data (random ID and listening port)
            char init_buffer[MAX_BUFFER];
            int bytes_received = recv(player_fd, init_buffer, sizeof(init_buffer), 0);
            if (bytes_received <= 0) {
                perror("Error receiving player init data");
                exit(EXIT_FAILURE);
            }
            
            int player_random_id;
            int player_port;
            memcpy(&player_random_id, init_buffer, sizeof(player_random_id));
            memcpy(&player_port, init_buffer + sizeof(player_random_id), sizeof(player_port));

            // Store the player's connection info
            player_fds[i] = player_fd;
            player_addrs[i] = player_addr;
            
            std::cout << "Player " << i << " is ready to play" << std::endl;
            
            // Send player information (total players and neighbors)
            char info_buffer[MAX_BUFFER];
            int left_neighbor = (i - 1 + num_players) % num_players;
            int right_neighbor = (i + 1) % num_players;
            
            memcpy(info_buffer, &num_players, sizeof(num_players));
            memcpy(info_buffer + sizeof(num_players), &left_neighbor, sizeof(left_neighbor));
            memcpy(info_buffer + sizeof(num_players) + sizeof(left_neighbor), 
                   &right_neighbor, sizeof(right_neighbor));
            
            if (send(player_fd, info_buffer, sizeof(num_players) + 2 * sizeof(int), 0) < 0) {
                perror("Error sending player info");
                exit(EXIT_FAILURE);
            }
        }
    }

    void startGame() {
        if (num_hops == 0) {
            // No game to play, just shut down
            shutdownGame();
            return;
        }

        // Create potato
        Potato potato;
        potato.hops = num_hops;
        
        // Choose random player to start with
        srand(time(NULL));
        int random_player = rand() % num_players;
        
        std::cout << "Ready to start the game, sending potato to player " 
                  << random_player << std::endl;
        
        // Send potato to the first player
        char potato_buffer[MAX_BUFFER];
        memcpy(potato_buffer, &potato.hops, sizeof(potato.hops));
        
        if (send(player_fds[random_player], potato_buffer, sizeof(potato.hops), 0) < 0) {
            perror("Error sending potato");
            exit(EXIT_FAILURE);
        }
        
        // Wait for potato to come back
        fd_set readfds;
        FD_ZERO(&readfds);
        
        int max_fd = *std::max_element(player_fds.begin(), player_fds.end());
        
        for (int fd : player_fds) {
            FD_SET(fd, &readfds);
        }
        
        // Wait for potato to come back after all hops
        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("Error in select");
            exit(EXIT_FAILURE);
        }
        
        // Find which player has the potato
        int final_player = -1;
        for (int i = 0; i < num_players; i++) {
            if (FD_ISSET(player_fds[i], &readfds)) {
                final_player = i;
                break;
            }
        }
        
        // Receive final potato with trace
        char final_buffer[MAX_BUFFER];
        int recv_size = recv(player_fds[final_player], final_buffer, sizeof(final_buffer), 0);
        if (recv_size <= 0) {
            perror("Error receiving final potato");
            exit(EXIT_FAILURE);
        }
        
        // Parse the trace from the buffer
        int trace_size;
        memcpy(&trace_size, final_buffer, sizeof(trace_size));
        
        Potato final_potato;
        final_potato.trace.resize(trace_size);
        
        for (int i = 0; i < trace_size; i++) {
            int player_id;
            memcpy(&player_id, final_buffer + sizeof(trace_size) + i * sizeof(int), sizeof(int));
            final_potato.trace[i] = player_id;
        }
        
        // Print the trace
        std::cout << "Trace of potato:" << std::endl;
        std::cout << final_potato.getTraceString() << std::endl;
        
        // Shut down the game
        shutdownGame();
    }

    void shutdownGame() {
        // Send shutdown signal to all players
        for (int fd : player_fds) {
            int shutdown_signal = -1;
            if (send(fd, &shutdown_signal, sizeof(shutdown_signal), 0) < 0) {
                perror("Error sending shutdown signal");
                // Continue with other players
            }
        }
    }
};

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <port_num> <num_players> <num_hops>" << std::endl;
        return EXIT_FAILURE;
    }
    
    int port_num = std::atoi(argv[1]);
    int num_players = std::atoi(argv[2]);
    int num_hops = std::atoi(argv[3]);
    
    // Validate arguments
    if (num_players <= 1) {
        std::cerr << "Error: Number of players must be greater than 1" << std::endl;
        return EXIT_FAILURE;
    }
    
    if (num_hops < 0 || num_hops > MAX_HOPS) {
        std::cerr << "Error: Number of hops must be between 0 and " << MAX_HOPS << std::endl;
        return EXIT_FAILURE;
    }
    
    Ringmaster master(port_num, num_players, num_hops);
    master.setupServer();
    master.acceptConnections();
    master.startGame();
    
    return EXIT_SUCCESS;
}