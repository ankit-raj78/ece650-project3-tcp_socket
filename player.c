// player.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include "potato.h"

#include <sys/select.h>  // for fd_set
#include <sys/types.h>   // for hostent

// Function to validate command line arguments
void validate_args(int argc, char *argv[], char **hostname, int *port_num) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <machine_name> <port_num>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    *hostname = argv[1];
    *port_num = atoi(argv[2]);
}

// Function to connect to a server
int connect_to_server(char *hostname, int port_num) {
    int sockfd;
    struct hostent *server;
    struct sockaddr_in server_addr;
    
    // Get server information
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr, "ERROR: no such host: %s\n", hostname);
        exit(EXIT_FAILURE);
    }
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error opening socket");
        exit(EXIT_FAILURE);
    }
    
    // Initialize server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr_list[0], (char *)&server_addr.sin_addr.s_addr, server->h_length);
    server_addr.sin_port = htons(port_num);
    
    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to server");
        exit(EXIT_FAILURE);
    }
    
    return sockfd;
}

// Function to set up a server socket for accepting connections
int setup_server_socket(PlayerInfo *my_info) {
    int sockfd;
    struct sockaddr_in server_addr;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error opening socket");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Error setting socket options");
        exit(EXIT_FAILURE);
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = 0;  // Let OS assign port
    
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }
    
    // Get assigned port number
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) < 0) {
        perror("Error getting socket name");
        exit(EXIT_FAILURE);
    }
    my_info->port = ntohs(sin.sin_port);
    
    if (listen(sockfd, 5) < 0) {
        perror("Error listening");
        exit(EXIT_FAILURE);
    }
    
    return sockfd;
}

int main(int argc, char *argv[]) {
    char *hostname;
    int port_num, master_socket, left_socket, right_socket, server_socket;
    int player_id, num_players, left_id, right_id;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    fd_set read_fds;
    FD_ZERO(&read_fds);  // Initialize the fd_set
    int max_fd, activity;
    
    // Validate and parse command line arguments
    validate_args(argc, argv, &hostname, &port_num);
    
    // Connect to ringmaster
    master_socket = connect_to_server(hostname, port_num);
    
    // Receive player ID and total number of players
    int player_info[2];
    if (recv(master_socket, player_info, sizeof(player_info), 0) <= 0) {
        perror("Error receiving player info");
        exit(EXIT_FAILURE);
    }
    
    player_id = player_info[0];
    num_players = player_info[1];
    
    PlayerInfo my_info;
    PlayerInfo neighbors[2];  // [0] = left neighbor, [1] = right neighbor
    
    // After connecting to ringmaster and receiving ID:
    if (gethostname(my_info.hostname, MAX_HOSTNAME) < 0) {
        perror("Error getting hostname");
        exit(EXIT_FAILURE);
    }
    my_info.hostname[MAX_HOSTNAME - 1] = '\0';  // Ensure null termination
    my_info.id = player_id;
    
    // Set up listening socket
    server_socket = setup_server_socket(&my_info);
    
    // Send my information to ringmaster
    if (send(master_socket, &my_info, sizeof(PlayerInfo), 0) < 0) {
        perror("Error sending player info");
        exit(EXIT_FAILURE);
    }
    
    // Receive neighbor information
    if (recv(master_socket, neighbors, sizeof(neighbors), 0) <= 0) {
        perror("Error receiving neighbor info");
        exit(EXIT_FAILURE);
    }
    
    left_id = neighbors[0].id;
    right_id = neighbors[1].id;
    
    // Seed random number generator
    srand((unsigned int)time(NULL) + player_id);
    
    // Print connection info
    printf("Connected as player %d out of %d total players\n", player_id, num_players);
    
    // Set up connections with neighbors
    // For simplicity, player with lower ID connects to player with higher ID
    if (player_id < right_id || (player_id == num_players - 1 && right_id == 0)) {
        // Wait for connection from right neighbor
        right_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (right_socket < 0) {
            perror("Error accepting connection from right neighbor");
            exit(EXIT_FAILURE);
        }
        
        // Connect to left neighbor
        left_socket = connect_to_server(neighbors[0].hostname, neighbors[0].port);
    } else {
        // Connect to right neighbor
        right_socket = connect_to_server(neighbors[1].hostname, neighbors[1].port);
        
        // Wait for connection from left neighbor
        left_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (left_socket < 0) {
            perror("Error accepting connection from left neighbor");
            exit(EXIT_FAILURE);
        }
    }
    
    // Main game loop
    while (1) {
        // Set up for select
        FD_ZERO(&read_fds);
        FD_SET(master_socket, &read_fds);
        FD_SET(left_socket, &read_fds);
        FD_SET(right_socket, &read_fds);
        
        max_fd = master_socket;
        if (left_socket > max_fd) max_fd = left_socket;
        if (right_socket > max_fd) max_fd = right_socket;
        
        // Wait for activity on any socket
        activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        
        if (activity < 0) {
            perror("Select error");
            exit(EXIT_FAILURE);
        }
        
        // Check if master sent a message (could be a potato or shut down command)
        if (FD_ISSET(master_socket, &read_fds)) {
            Potato potato;
            int bytes = recv(master_socket, &potato, sizeof(Potato), 0);
            
            if (bytes <= 0) {
                // Master closed connection - game over
                break;
            }
            
            // Process the potato
            potato.hops--;
            potato.trace[potato.trace_count++] = player_id;
            
            if (potato.hops > 0) {
                // Randomly choose left or right neighbor
                int next_player = rand() % 2;
                int next_socket = (next_player == 0) ? left_socket : right_socket;
                int next_id = (next_player == 0) ? left_id : right_id;
                
                printf("Sending potato to %d\n", next_id);
                
                if (send(next_socket, &potato, sizeof(Potato), 0) < 0) {
                    perror("Error sending potato");
                    exit(EXIT_FAILURE);
                }
            } else {
                // Last hop, send potato back to ringmaster
                printf("I'm it\n");
                
                if (send(master_socket, &potato, sizeof(Potato), 0) < 0) {
                    perror("Error sending potato to ringmaster");
                    exit(EXIT_FAILURE);
                }
            }
        }
        
        // Check if left neighbor sent a message
        if (FD_ISSET(left_socket, &read_fds)) {
            Potato potato;
            int bytes = recv(left_socket, &potato, sizeof(Potato), 0);
            
            if (bytes <= 0) {
                // Left neighbor closed connection
                break;
            }
            
            // Process the potato
            potato.hops--;
            potato.trace[potato.trace_count++] = player_id;
            
            if (potato.hops > 0) {
                // Randomly choose left or right neighbor
                int next_player = rand() % 2;
                int next_socket = (next_player == 0) ? left_socket : right_socket;
                int next_id = (next_player == 0) ? left_id : right_id;
                
                printf("Sending potato to %d\n", next_id);
                
                if (send(next_socket, &potato, sizeof(Potato), 0) < 0) {
                    perror("Error sending potato");
                    exit(EXIT_FAILURE);
                }
            } else {
                // Last hop, send potato back to ringmaster
                printf("I'm it\n");
                
                if (send(master_socket, &potato, sizeof(Potato), 0) < 0) {
                    perror("Error sending potato to ringmaster");
                    exit(EXIT_FAILURE);
                }
            }
        }
        
        // Check if right neighbor sent a message
        if (FD_ISSET(right_socket, &read_fds)) {
            Potato potato;
            int bytes = recv(right_socket, &potato, sizeof(Potato), 0);
            
            if (bytes <= 0) {
                // Right neighbor closed connection
                break;
            }
            
            // Process the potato
            potato.hops--;
            potato.trace[potato.trace_count++] = player_id;
            
            if (potato.hops > 0) {
                // Randomly choose left or right neighbor
                int next_player = rand() % 2;
                int next_socket = (next_player == 0) ? left_socket : right_socket;
                int next_id = (next_player == 0) ? left_id : right_id;
                
                printf("Sending potato to %d\n", next_id);
                
                if (send(next_socket, &potato, sizeof(Potato), 0) < 0) {
                    perror("Error sending potato");
                    exit(EXIT_FAILURE);
                }
            } else {
                // Last hop, send potato back to ringmaster
                printf("I'm it\n");
                
                if (send(master_socket, &potato, sizeof(Potato), 0) < 0) {
                    perror("Error sending potato to ringmaster");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
    
    // Clean up
    close(master_socket);
    close(left_socket);
    close(right_socket);
    close(server_socket);
    
    return 0;
}