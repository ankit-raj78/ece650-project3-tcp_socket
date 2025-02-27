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

class Player {
private:
    std::string master_hostname;
    int master_port;
    int player_id;
    int num_players;
    int master_socket;
    int left_socket;  // Connection to left neighbor
    int right_socket; // Connection to right neighbor
    int left_id;
    int right_id;
    int server_socket; // For accepting neighbor connection

public:
    Player(const std::string& hostname, int port) : 
        master_hostname(hostname), master_port(port), 
        player_id(-1), num_players(0), 
        master_socket(-1), left_socket(-1), right_socket(-1), server_socket(-1) {}

    ~Player() {
        if (master_socket != -1) close(master_socket);
        if (left_socket != -1) close(left_socket);
        if (right_socket != -1) close(right_socket);
        if (server_socket != -1) close(server_socket);
    }

    void connectToMaster() {
        struct hostent *server;
        struct sockaddr_in server_addr;

        master_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (master_socket < 0) {
            perror("Error creating socket");
            exit(EXIT_FAILURE);
        }

        server = gethostbyname(master_hostname.c_str());
        if (server == NULL) {
            std::cerr << "Error: no such host " << master_hostname << std::endl;
            exit(EXIT_FAILURE);
        }

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        server_addr.sin_port = htons(master_port);

        if (connect(master_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Error connecting to ringmaster");
            exit(EXIT_FAILURE);
        }

        // Create server socket for neighbor connections
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
            perror("Error creating server socket");
            exit(EXIT_FAILURE);
        }

        // Set socket option to reuse address
        int yes = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        struct sockaddr_in player_addr;
        memset(&player_addr, 0, sizeof(player_addr));
        player_addr.sin_family = AF_INET;
        player_addr.sin_addr.s_addr = INADDR_ANY;
        player_addr.sin_port = 0; // Let OS choose a port

        if (bind(server_socket, (struct sockaddr*)&player_addr, sizeof(player_addr)) < 0) {
            perror("Error binding player socket");
            exit(EXIT_FAILURE);
        }

        if (listen(server_socket, 2) < 0) { // Listen for 2 potential neighbors
            perror("Error listening");
            exit(EXIT_FAILURE);
        }

        // Get the port assigned by OS
        socklen_t addr_len = sizeof(player_addr);
        if (getsockname(server_socket, (struct sockaddr*)&player_addr, &addr_len) < 0) {
            perror("Error getting socket name");
            exit(EXIT_FAILURE);
        }

        // Generate a random player ID for initial communication
        srand(time(NULL) ^ getpid());
        player_id = rand();

        // Send player ID and listening port to ringmaster
        char init_buffer[MAX_BUFFER];
        int port = ntohs(player_addr.sin_port);
        
        memcpy(init_buffer, &player_id, sizeof(player_id));
        memcpy(init_buffer + sizeof(player_id), &port, sizeof(port));
        
        if (send(master_socket, init_buffer, sizeof(player_id) + sizeof(port), 0) < 0) {
            perror("Error sending initial data");
            exit(EXIT_FAILURE);
        }

        // Receive player information from ringmaster
        char info_buffer[MAX_BUFFER];
        int bytes_received = recv(master_socket, info_buffer, sizeof(info_buffer), 0);
        if (bytes_received <= 0) {
            perror("Error receiving player info");
            exit(EXIT_FAILURE);
        }

        // Extract information
        memcpy(&num_players, info_buffer, sizeof(num_players));
        memcpy(&left_id, info_buffer + sizeof(num_players), sizeof(left_id));
        memcpy(&right_id, info_buffer + sizeof(num_players) + sizeof(left_id), sizeof(right_id));
        
        // Now we know our real player ID
        player_id = (right_id + num_players - 1) % num_players;
        
        std::cout << "Connected as player " << player_id << " out of " << num_players << " total players" << std::endl;
        
        // Set random seed with player ID for true randomness between players
        srand((unsigned int)time(NULL) + player_id);
    }

    void setupNeighbors() {
        // Instead of trying to connect directly to neighbors, we should get connection info from ringmaster
        // We need to implement a proper neighbor connection mechanism that avoids race conditions
        // This is a simplified version that waits for instructions from ringmaster

        // Step 1: Get neighbor IP and port information from ringmaster
        struct sockaddr_in neighbor_addrs[2]; // Left and right neighbor address info
        char neighbor_buffer[MAX_BUFFER];
        
        // Receive neighbor connection information from ringmaster
        int bytes_received = recv(master_socket, neighbor_buffer, sizeof(neighbor_buffer), 0);
        if (bytes_received <= 0) {
            perror("Error receiving neighbor info");
            exit(EXIT_FAILURE);
        }
        
        // Extract neighbor information (left and right IP/port)
        // This needs to be implemented by the ringmaster side as well
        
        // Step 2: Set up listening server for incoming connections
        // We already have server_socket set up from connectToMaster()
        
        // Step 3: Connect to neighbors
        // First accept one connection (the left neighbor trying to connect to us)
        struct sockaddr_in incoming_addr;
        socklen_t addr_len = sizeof(incoming_addr);
        left_socket = accept(server_socket, (struct sockaddr*)&incoming_addr, &addr_len);
        if (left_socket < 0) {
            perror("Error accepting left neighbor");
            exit(EXIT_FAILURE);
        }
        
        // Connect to our right neighbor
        right_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (right_socket < 0) {
            perror("Error creating right socket");
            exit(EXIT_FAILURE);
        }
        
        // Use the information received from ringmaster
        if (connect(right_socket, (struct sockaddr*)&neighbor_addrs[1], sizeof(neighbor_addrs[1])) < 0) {
            perror("Error connecting to right neighbor");
            exit(EXIT_FAILURE);
        }
    }

    void playGame() {
        fd_set readfds;
        int max_fd = std::max({master_socket, left_socket, right_socket});
        Potato potato;
        
        while (true) {
            FD_ZERO(&readfds);
            FD_SET(master_socket, &readfds);
            FD_SET(left_socket, &readfds);
            FD_SET(right_socket, &readfds);
            
            if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
                perror("Error in select");
                exit(EXIT_FAILURE);
            }
            
            // Check if ringmaster is sending something
            if (FD_ISSET(master_socket, &readfds)) {
                char buffer[MAX_BUFFER];
                int bytes_received = recv(master_socket, buffer, sizeof(buffer), 0);
                
                if (bytes_received <= 0) {
                    perror("Error receiving from ringmaster");
                    exit(EXIT_FAILURE);
                }
                
                // Extract hops
                int hops;
                memcpy(&hops, buffer, sizeof(hops));
                
                if (hops < 0) {
                    // Shutdown signal
                    break;
                }
                
                potato.hops = hops;
                potato.trace.clear();
                
                // Process potato
                potato.hops--;
                potato.addTrace(player_id);
                
                // Pass the potato
                if (potato.hops > 0) {
                    int random_neighbor = rand() % 2;
                    int neighbor_socket = (random_neighbor == 0) ? left_socket : right_socket;
                    int neighbor_id = (random_neighbor == 0) ? left_id : right_id;
                    
                    std::cout << "Sending potato to " << neighbor_id << std::endl;
                    
                    char potato_buffer[MAX_BUFFER];
                    memcpy(potato_buffer, &potato.hops, sizeof(potato.hops));
                    
                    size_t trace_size = potato.trace.size();
                    memcpy(potato_buffer + sizeof(potato.hops), &trace_size, sizeof(trace_size));
                    
                    for (size_t i = 0; i < trace_size; i++) {
                        memcpy(potato_buffer + sizeof(potato.hops) + sizeof(trace_size) + i * sizeof(int),
                               &potato.trace[i], sizeof(int));
                    }
                    
                    if (send(neighbor_socket, potato_buffer, sizeof(potato.hops) + sizeof(trace_size) + trace_size * sizeof(int), 0) < 0) {
                        perror("Error sending potato");
                        exit(EXIT_FAILURE);
                    }
                } else {
                    // Last hop, send it back to ringmaster
                    std::cout << "I'm it" << std::endl;
                    
                    char potato_buffer[MAX_BUFFER];
                    size_t trace_size = potato.trace.size();
                    
                    memcpy(potato_buffer, &trace_size, sizeof(trace_size));
                    
                    for (size_t i = 0; i < trace_size; i++) {
                        memcpy(potato_buffer + sizeof(trace_size) + i * sizeof(int),
                               &potato.trace[i], sizeof(int));
                    }
                    
                    if (send(master_socket, potato_buffer, sizeof(trace_size) + trace_size * sizeof(int), 0) < 0) {
                        perror("Error sending potato to ringmaster");
                        exit(EXIT_FAILURE);
                    }
                }
            }
            
            // Check if left neighbor is sending something
            if (FD_ISSET(left_socket, &readfds)) {
                char buffer[MAX_BUFFER];
                int bytes_received = recv(left_socket, buffer, sizeof(buffer), 0);
                
                if (bytes_received <= 0) {
                    perror("Error receiving from left neighbor");
                    exit(EXIT_FAILURE);
                }
                
                // Extract potato
                memcpy(&potato.hops, buffer, sizeof(potato.hops));
                
                size_t trace_size;
                memcpy(&trace_size, buffer + sizeof(potato.hops), sizeof(trace_size));
                
                potato.trace.resize(trace_size);
                for (size_t i = 0; i < trace_size; i++) {
                    memcpy(&potato.trace[i], buffer + sizeof(potato.hops) + sizeof(trace_size) + i * sizeof(int), sizeof(int));
                }
                
                // Process potato
                potato.hops--;
                potato.addTrace(player_id);
                
                // Pass the potato
                if (potato.hops > 0) {
                    int random_neighbor = rand() % 2;
                    int neighbor_socket = (random_neighbor == 0) ? left_socket : right_socket;
                    int neighbor_id = (random_neighbor == 0) ? left_id : right_id;
                    
                    std::cout << "Sending potato to " << neighbor_id << std::endl;
                    
                    char potato_buffer[MAX_BUFFER];
                    memcpy(potato_buffer, &potato.hops, sizeof(potato.hops));
                    
                    trace_size = potato.trace.size();
                    memcpy(potato_buffer + sizeof(potato.hops), &trace_size, sizeof(trace_size));
                    
                    for (size_t i = 0; i < trace_size; i++) {
                        memcpy(potato_buffer + sizeof(potato.hops) + sizeof(trace_size) + i * sizeof(int),
                               &potato.trace[i], sizeof(int));
                    }
                    
                    if (send(neighbor_socket, potato_buffer, sizeof(potato.hops) + sizeof(trace_size) + trace_size * sizeof(int), 0) < 0) {
                        perror("Error sending potato");
                        exit(EXIT_FAILURE);
                    }
                } else {
                    // Last hop, send it back to ringmaster
                    std::cout << "I'm it" << std::endl;
                    
                    char potato_buffer[MAX_BUFFER];
                    trace_size = potato.trace.size();
                    
                    memcpy(potato_buffer, &trace_size, sizeof(trace_size));
                    
                    for (size_t i = 0; i < trace_size; i++) {
                        memcpy(potato_buffer + sizeof(trace_size) + i * sizeof(int),
                               &potato.trace[i], sizeof(int));
                    }
                    
                    if (send(master_socket, potato_buffer, sizeof(trace_size) + trace_size * sizeof(int), 0) < 0) {
                        perror("Error sending potato to ringmaster");
                        exit(EXIT_FAILURE);
                    }
                }
            }
            
            // Check if right neighbor is sending something
            if (FD_ISSET(right_socket, &readfds)) {
                char buffer[MAX_BUFFER];
                int bytes_received = recv(right_socket, buffer, sizeof(buffer), 0);
                
                if (bytes_received <= 0) {
                    perror("Error receiving from right neighbor");
                    exit(EXIT_FAILURE);
                }
                
                // Extract potato
                memcpy(&potato.hops, buffer, sizeof(potato.hops));
                
                size_t trace_size;
                memcpy(&trace_size, buffer + sizeof(potato.hops), sizeof(trace_size));
                
                potato.trace.resize(trace_size);
                for (size_t i = 0; i < trace_size; i++) {
                    memcpy(&potato.trace[i], buffer + sizeof(potato.hops) + sizeof(trace_size) + i * sizeof(int), sizeof(int));
                }
                
                // Process potato
                potato.hops--;
                potato.addTrace(player_id);
                
                // Pass the potato
                if (potato.hops > 0) {
                    int random_neighbor = rand() % 2;
                    int neighbor_socket = (random_neighbor == 0) ? left_socket : right_socket;
                    int neighbor_id = (random_neighbor == 0) ? left_id : right_id;
                    
                    std::cout << "Sending potato to " << neighbor_id << std::endl;
                    
                    char potato_buffer[MAX_BUFFER];
                    memcpy(potato_buffer, &potato.hops, sizeof(potato.hops));
                    
                    trace_size = potato.trace.size();
                    memcpy(potato_buffer + sizeof(potato.hops), &trace_size, sizeof(trace_size));
                    
                    for (size_t i = 0; i < trace_size; i++) {
                        memcpy(potato_buffer + sizeof(potato.hops) + sizeof(trace_size) + i * sizeof(int),
                               &potato.trace[i], sizeof(int));
                    }
                    
                    if (send(neighbor_socket, potato_buffer, sizeof(potato.hops) + sizeof(trace_size) + trace_size * sizeof(int), 0) < 0) {
                        perror("Error sending potato");
                        exit(EXIT_FAILURE);
                    }
                } else {
                    // Last hop, send it back to ringmaster
                    std::cout << "I'm it" << std::endl;
                    
                    char potato_buffer[MAX_BUFFER];
                    trace_size = potato.trace.size();
                    
                    memcpy(potato_buffer, &trace_size, sizeof(trace_size));
                    
                    for (size_t i = 0; i < trace_size; i++) {
                        memcpy(potato_buffer + sizeof(trace_size) + i * sizeof(int),
                               &potato.trace[i], sizeof(int));
                    }
                    
                    if (send(master_socket, potato_buffer, sizeof(trace_size) + trace_size * sizeof(int), 0) < 0) {
                        perror("Error sending potato to ringmaster");
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
    }
};

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <machine_name> <port_num>" << std::endl;
        return EXIT_FAILURE;
    }
    
    std::string machine_name = argv[1];
    int port_num = std::atoi(argv[2]);
    
    Player player(machine_name, port_num);
    player.connectToMaster();
    player.setupNeighbors();
    player.playGame();
    
    return EXIT_SUCCESS;
}