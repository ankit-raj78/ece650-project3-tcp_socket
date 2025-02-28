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

    void connectToMaster(const char* hostname, int port) {
        // Create a socket for connecting to the ringmaster
        master_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (master_socket < 0) {
            perror("Error creating socket");
            exit(EXIT_FAILURE);
        }
        
        // Set up the ringmaster's address
        struct sockaddr_in master_addr;
        memset(&master_addr, 0, sizeof(master_addr));
        master_addr.sin_family = AF_INET;
        master_addr.sin_port = htons(port);
        
        // Convert hostname to IP address
        struct hostent* host_info = gethostbyname(hostname);
        if (host_info == NULL) {
            herror("Error getting host info");
            exit(EXIT_FAILURE);
        }
        memcpy(&master_addr.sin_addr.s_addr, host_info->h_addr, host_info->h_length);
        
        // Connect to the ringmaster
        if (connect(master_socket, (struct sockaddr*)&master_addr, sizeof(master_addr)) < 0) {
            perror("Error connecting to ringmaster");
            exit(EXIT_FAILURE);
        }
        
        // Set up server socket for other players to connect to
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
            perror("Error creating server socket");
            exit(EXIT_FAILURE);
        }
        
        // Set up the server address (bind to any available port)
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = 0; // Let OS choose port
        
        if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Error binding server socket");
            exit(EXIT_FAILURE);
        }
        
        if (listen(server_socket, 5) < 0) {
            perror("Error listening on server socket");
            exit(EXIT_FAILURE);
        }
        
        // Get the assigned port number
        socklen_t len = sizeof(server_addr);
        if (getsockname(server_socket, (struct sockaddr*)&server_addr, &len) < 0) {
            perror("Error getting socket name");
            exit(EXIT_FAILURE);
        }
        int my_port = ntohs(server_addr.sin_port);
        
        // Send our listening port to ringmaster
        char init_buffer[MAX_BUFFER];
        memcpy(init_buffer, &my_port, sizeof(my_port));
        
        if (send(master_socket, init_buffer, sizeof(my_port), 0) < 0) {
            perror("Error sending player info");
            exit(EXIT_FAILURE);
        }
        
        // Receive player ID and total number of players from ringmaster
        char player_info[MAX_BUFFER];
        int bytes_received = recv(master_socket, player_info, sizeof(player_info), 0);
        if (bytes_received <= 0) {
            perror("Error receiving player info");
            exit(EXIT_FAILURE);
        }
        
        // Parse player information
        memcpy(&num_players, player_info, sizeof(num_players));
        memcpy(&player_id, player_info + sizeof(num_players), sizeof(player_id));
        
        std::cout << "Connected as player " << player_id << " out of " << num_players << " total players" << std::endl;
        
        // Set random seed with player ID for true randomness between players
        srand((unsigned int)time(NULL) + player_id);
    }

    void setupNeighbors() {
        std::cout << "Player " << player_id << " is ready to play" << std::endl;

        // Calculate neighbor IDs
        left_id = (player_id - 1 + num_players) % num_players;
        right_id = (player_id + 1) % num_players;

        // Receive information about neighbors from ringmaster
        char left_info[MAX_BUFFER], right_info[MAX_BUFFER];
        if (recv(master_socket, left_info, sizeof(struct in_addr) + sizeof(int), 0) <= 0) {
            perror("Error receiving left neighbor info");
            exit(EXIT_FAILURE);
        }
        
        if (recv(master_socket, right_info, sizeof(struct in_addr) + sizeof(int), 0) <= 0) {
            perror("Error receiving right neighbor info");
            exit(EXIT_FAILURE);
        }

        // Extract neighbor information
        struct in_addr left_ip, right_ip;
        int left_port, right_port;
        
        memcpy(&left_ip, left_info, sizeof(left_ip));
        memcpy(&left_port, left_info + sizeof(left_ip), sizeof(left_port));
        
        memcpy(&right_ip, right_info, sizeof(right_ip));
        memcpy(&right_port, right_info + sizeof(right_ip), sizeof(right_port));

        // For a 2-player game, set up direct connections (special case)
        if (num_players == 2) {
            if (player_id == 0) {
                // Player 0 listens for connection from Player 1
                struct sockaddr_in addr;
                socklen_t addr_len = sizeof(addr);
                
                std::cout << "Player " << player_id << " waiting for connection..." << std::endl;
                right_socket = accept(server_socket, (struct sockaddr*)&addr, &addr_len);
                if (right_socket < 0) {
                    perror("Error accepting connection from player 1");
                    exit(EXIT_FAILURE);
                }
                std::cout << "Player " << player_id << " accepted connection from player 1" << std::endl;
                
                // Both neighbors are the same in a 2-player game
                left_socket = right_socket;
            }
            else if (player_id == 1) {
                // Player 1 connects to Player 0
                left_socket = socket(AF_INET, SOCK_STREAM, 0);
                if (left_socket < 0) {
                    perror("Error creating socket");
                    exit(EXIT_FAILURE);
                }
                
                struct sockaddr_in addr;
                memset(&addr, 0, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_addr = left_ip;
                addr.sin_port = htons(left_port);
                
                std::cout << "Player " << player_id << " connecting to player 0..." << std::endl;
                if (connect(left_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    perror("Error connecting to player 0");
                    exit(EXIT_FAILURE);
                }
                std::cout << "Player " << player_id << " connected to player 0" << std::endl;
                
                // Both neighbors are the same in a 2-player game
                right_socket = left_socket;
            }
            return;
        }

        // For more than 2 players, use the alternating pattern
        if (player_id % 2 == 0) {
            // Even players: accept connections first, then connect
            
            // Accept connections from one of the neighbors
            struct sockaddr_in addr;
            socklen_t addr_len = sizeof(addr);
            
            // Accept connection from left neighbor
            std::cout << "Player " << player_id << " waiting for connection from player " << left_id << "..." << std::endl;
            left_socket = accept(server_socket, (struct sockaddr*)&addr, &addr_len);
            if (left_socket < 0) {
                perror("Error accepting left neighbor");
                exit(EXIT_FAILURE);
            }
            std::cout << "Player " << player_id << " accepted connection from player " << left_id << std::endl;
            
            // Connect to right neighbor
            right_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (right_socket < 0) {
                perror("Error creating right socket");
                exit(EXIT_FAILURE);
            }
            
            struct sockaddr_in right_addr;
            memset(&right_addr, 0, sizeof(right_addr));
            right_addr.sin_family = AF_INET;
            right_addr.sin_addr = right_ip;
            right_addr.sin_port = htons(right_port);
            
            // Add a short delay to ensure the other side is ready
            sleep(1);
            std::cout << "Player " << player_id << " connecting to player " << right_id << "..." << std::endl;
            if (connect(right_socket, (struct sockaddr*)&right_addr, sizeof(right_addr)) < 0) {
                perror("Error connecting to right neighbor");
                exit(EXIT_FAILURE);
            }
            std::cout << "Player " << player_id << " connected to player " << right_id << std::endl;
        } else {
            // Odd players: connect first, then accept
            
            // Connect to left neighbor
            left_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (left_socket < 0) {
                perror("Error creating left socket");
                exit(EXIT_FAILURE);
            }
            
            struct sockaddr_in left_addr;
            memset(&left_addr, 0, sizeof(left_addr));
            left_addr.sin_family = AF_INET;
            left_addr.sin_addr = left_ip;
            left_addr.sin_port = htons(left_port);
            
            std::cout << "Player " << player_id << " connecting to player " << left_id << "..." << std::endl;
            if (connect(left_socket, (struct sockaddr*)&left_addr, sizeof(left_addr)) < 0) {
                perror("Error connecting to left neighbor");
                exit(EXIT_FAILURE);
            }
            std::cout << "Player " << player_id << " connected to player " << left_id << std::endl;
            
            // Accept connection from right neighbor
            struct sockaddr_in addr;
            socklen_t addr_len = sizeof(addr);
            
            std::cout << "Player " << player_id << " waiting for connection from player " << right_id << "..." << std::endl;
            right_socket = accept(server_socket, (struct sockaddr*)&addr, &addr_len);
            if (right_socket < 0) {
                perror("Error accepting right neighbor");
                exit(EXIT_FAILURE);
            }
            std::cout << "Player " << player_id << " accepted connection from player " << right_id << std::endl;
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
    player.connectToMaster(machine_name.c_str(), port_num);
    player.setupNeighbors();
    player.playGame();
    
    return EXIT_SUCCESS;
}