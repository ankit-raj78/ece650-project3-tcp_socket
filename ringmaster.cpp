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
        std::vector<int> player_ports(num_players);
        
        for (int i = 0; i < num_players; i++) {
            struct sockaddr_in player_addr;
            socklen_t addr_len = sizeof(player_addr);
            
            int player_fd = accept(listener_fd, (struct sockaddr*)&player_addr, &addr_len);
            if (player_fd < 0) {
                perror("Error accepting connection");
                exit(EXIT_FAILURE);
            }
            
            // Receive player's listening port
            char init_buffer[MAX_BUFFER];
            int bytes_received = recv(player_fd, init_buffer, sizeof(int), 0);
            if (bytes_received <= 0) {
                perror("Error receiving player init data");
                exit(EXIT_FAILURE);
            }
            
            int player_port;
            memcpy(&player_port, init_buffer, sizeof(player_port));
            player_ports[i] = player_port;
            
            // Store the player's connection info and update port
            player_fds[i] = player_fd;
            player_addrs[i] = player_addr;
            player_addrs[i].sin_port = htons(player_port);  // Update with correct listening port
            
            // Send player ID and total players
            char info_buffer[MAX_BUFFER];
            memcpy(info_buffer, &num_players, sizeof(num_players));
            memcpy(info_buffer + sizeof(num_players), &i, sizeof(i));
            
            if (send(player_fd, info_buffer, sizeof(num_players) + sizeof(i), 0) < 0) {
                perror("Error sending player info");
                exit(EXIT_FAILURE);
            }
            
            std::cout << "Player " << i << " is ready to play" << std::endl;
        }
        
        // Send neighbor information to each player
        for (int i = 0; i < num_players; i++) {
            int left_id = (i - 1 + num_players) % num_players;
            int right_id = (i + 1) % num_players;
            
            // Send left neighbor info
            char left_info[MAX_BUFFER];
            struct in_addr left_ip = player_addrs[left_id].sin_addr;
            int left_port = player_ports[left_id];
            
            memcpy(left_info, &left_ip, sizeof(left_ip));
            memcpy(left_info + sizeof(left_ip), &left_port, sizeof(left_port));
            
            if (send(player_fds[i], left_info, sizeof(left_ip) + sizeof(left_port), 0) < 0) {
                perror("Error sending left neighbor info");
                exit(EXIT_FAILURE);
            }
            
            // Send right neighbor info
            char right_info[MAX_BUFFER];
            struct in_addr right_ip = player_addrs[right_id].sin_addr;
            int right_port = player_ports[right_id];
            
            memcpy(right_info, &right_ip, sizeof(right_ip));
            memcpy(right_info + sizeof(right_ip), &right_port, sizeof(right_port));
            
            if (send(player_fds[i], right_info, sizeof(right_ip) + sizeof(right_port), 0) < 0) {
                perror("Error sending right neighbor info");
                exit(EXIT_FAILURE);
            }
        }
    }

    void startGame() {
        // If no hops, just end the game
        if (num_hops == 0) {
            shutdownGame();
            return;
        }
    
        // Create and initialize potato
        Potato potato;
        potato.hops = num_hops;
        
        // Choose a random player to start with
        srand(time(NULL));
        int random_player = rand() % num_players;
        std::cout << "Ready to start the game, sending potato to player " << random_player << std::endl;
        
        // Send potato to the first player
        char potato_buffer[MAX_BUFFER];
        memcpy(potato_buffer, &potato.hops, sizeof(potato.hops));
        
        size_t trace_size = potato.trace.size();
        memcpy(potato_buffer + sizeof(potato.hops), &trace_size, sizeof(trace_size));
        
        if (send(player_fds[random_player], potato_buffer, sizeof(potato.hops) + sizeof(trace_size), 0) < 0) {
            perror("Error sending initial potato");
            exit(EXIT_FAILURE);
        }
        
        // Wait for potato to come back
        fd_set readfds;
        FD_ZERO(&readfds);
        
        int max_fd = -1;
        for (int fd : player_fds) {
            FD_SET(fd, &readfds);
            max_fd = std::max(max_fd, fd);
        }
        
        // Wait for the potato to come back from any player
        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("Error in select");
            exit(EXIT_FAILURE);
        }
        
        for (int i = 0; i < num_players; i++) {
            if (FD_ISSET(player_fds[i], &readfds)) {
                char buffer[MAX_BUFFER];
                int bytes_received = recv(player_fds[i], buffer, sizeof(buffer), 0);
                
                if (bytes_received <= 0) {
                    perror("Error receiving final potato");
                    exit(EXIT_FAILURE);
                }
                
                // Extract trace
                size_t trace_size;
                memcpy(&trace_size, buffer, sizeof(trace_size));
                
                std::vector<int> trace(trace_size);
                for (size_t j = 0; j < trace_size; j++) {
                    memcpy(&trace[j], buffer + sizeof(trace_size) + j * sizeof(int), sizeof(int));
                }
                
                // Print trace
                std::cout << "Trace of potato:" << std::endl;
                for (size_t j = 0; j < trace_size; j++) {
                    std::cout << trace[j];
                    if (j < trace_size - 1) {
                        std::cout << ",";
                    }
                }
                std::cout << std::endl;
                
                break;
            }
        }
        
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