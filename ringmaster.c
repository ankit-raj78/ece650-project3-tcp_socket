// ringmaster.c
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
#include <stdbool.h>     // Add after existing includes

// Function to validate command line arguments
void validate_args(int argc, char *argv[], int *port_num, int *num_players, int *num_hops) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <port_num> <num_players> <num_hops>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    *port_num = atoi(argv[1]);
    *num_players = atoi(argv[2]);
    *num_hops = atoi(argv[3]);
    
    if (*num_players <= 1) {
        fprintf(stderr, "Error: Number of players must be greater than 1\n");
        exit(EXIT_FAILURE);
    }
    
    if (*num_hops < 0 || *num_hops > MAX_HOPS) {
        fprintf(stderr, "Error: Number of hops must be between 0 and %d\n", MAX_HOPS);
        exit(EXIT_FAILURE);
    }
}

// Function to set up the server socket
int setup_server_socket(int port_num) {
    int sockfd;
    struct sockaddr_in server_addr;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error opening socket");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options to allow reuse of port
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Error setting socket options");
        exit(EXIT_FAILURE);
    }
    
    // Initialize server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_num);
    
    // Bind socket to the address
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }
    
    // Start listening for connections
    if (listen(sockfd, 5) < 0) {
        perror("Error listening");
        exit(EXIT_FAILURE);
    }
    
    return sockfd;
}

int main(int argc, char *argv[]) {
    int port_num, num_players, num_hops;
    int master_socket, new_socket;
    int player_sockets[MAX_HOPS];  // To store player connection file descriptors
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    fd_set read_fds;
    FD_ZERO(&read_fds);  // Initialize the fd_set
    int max_fd, activity;
    PlayerInfo *players;  // Dynamic array to store player information

    // Validate and parse command line arguments
    validate_args(argc, argv, &port_num, &num_players, &num_hops);
    
    // Print initial output
    printf("Potato Ringmaster\n");
    printf("Players = %d\n", num_players);
    printf("Hops = %d\n", num_hops);
    
    // Setup server socket
    master_socket = setup_server_socket(port_num);
    
    players = malloc(num_players * sizeof(PlayerInfo));
    if (!players) {
        perror("Failed to allocate memory for player info");
        exit(EXIT_FAILURE);
    }

    // Accept connections from all players
    for (int i = 0; i < num_players; i++) {
        new_socket = accept(master_socket, (struct sockaddr *)&client_addr, &client_len);
        if (new_socket < 0) {
            perror("Error accepting connection");
            exit(EXIT_FAILURE);
        }
        
        // Send player ID and total number of players
        int player_info[2] = {i, num_players};
        if (send(new_socket, player_info, sizeof(player_info), 0) < 0) {
            perror("Error sending player info");
            exit(EXIT_FAILURE);
        }
        
        // Receive player's port and hostname
        if (recv(new_socket, &players[i], sizeof(PlayerInfo), 0) <= 0) {
            perror("Error receiving player information");
            exit(EXIT_FAILURE);
        }
        
        player_sockets[i] = new_socket;
        printf("Player %d is ready to play\n", i);
    }

    // Send neighbor information to each player
    for (int i = 0; i < num_players; i++) {
        int left = (i - 1 + num_players) % num_players;
        int right = (i + 1) % num_players;
        
        PlayerInfo neighbors[2] = {players[left], players[right]};
        if (send(player_sockets[i], neighbors, sizeof(neighbors), 0) < 0) {
            perror("Error sending neighbor info");
            exit(EXIT_FAILURE);
        }
    }
    
    // If num_hops is 0, just shut down the game
    if (num_hops == 0) {
        // Shut down the game
        for (int i = 0; i < num_players; i++) {
            close(player_sockets[i]);
        }
        close(master_socket);
        return 0;
    }
    
    // Create and initialize potato
    Potato potato;
    memset(&potato, 0, sizeof(Potato));
    potato.hops = num_hops;
    potato.trace_count = 0;
    
    // Randomly select first player
    srand(time(NULL));
    int random_player = rand() % num_players;
    
    printf("Ready to start the game, sending potato to player %d\n", random_player);
    
    // Send potato to first player
    if (send(player_sockets[random_player], &potato, sizeof(Potato), 0) < 0) {
        perror("Error sending potato");
        exit(EXIT_FAILURE);
    }
    
    // Wait for potato to come back
    FD_ZERO(&read_fds);
    max_fd = master_socket;
    
    for (int i = 0; i < num_players; i++) {
        FD_SET(player_sockets[i], &read_fds);
        if (player_sockets[i] > max_fd) {
            max_fd = player_sockets[i];
        }
    }
    
    // Wait for activity on any socket
    activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
    
    if (activity < 0) {
        perror("Select error");
        exit(EXIT_FAILURE);
    }
    
    // Check which socket has data
    for (int i = 0; i < num_players; i++) {
        if (FD_ISSET(player_sockets[i], &read_fds)) {
            // Receive potato
            if (recv(player_sockets[i], &potato, sizeof(Potato), 0) <= 0) {
                perror("Error receiving potato");
                exit(EXIT_FAILURE);
            }
            
            // Print trace
            printf("Trace of potato:\n");
            for (int j = 0; j < potato.trace_count; j++) {
                printf("%d", potato.trace[j]);
                if (j < potato.trace_count - 1) {
                    printf(",");
                }
            }
            printf("\n");
            
            break;
        }
    }
    
    // Shut down the game
    for (int i = 0; i < num_players; i++) {
        close(player_sockets[i]);
    }
    close(master_socket);
    
    return 0;
}