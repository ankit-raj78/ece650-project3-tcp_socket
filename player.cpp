#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <random>
#include <time.h>

#include "potato.h"
#include "network_utils.h"

class Player {
private:
    int id;                // Player's ID
    int num_players;       // Total number of players
    int master_fd;         // Socket to ringmaster
    int left_fd;           // Socket to left neighbor
    int right_fd;          // Socket to right neighbor
    int listen_fd;         // Listening socket for neighbor connections
    int listen_port;       // Port on which player is listening
    int left_id;           // ID of left neighbor
    int right_id;          // ID of right neighbor
    std::mt19937 rng;      // Random number generator

public:
    Player(const std::string& master_hostname, int master_port) {
        // Initialize random number generator
        std::random_device rd;
        rng.seed(rd());
        
        // Create listening socket for neighbors
        try {
            listen_fd = NetworkUtils::create_server_socket(&listen_port);
        } catch (const NetworkError& e) {
            std::cerr << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
        
        // Connect to ringmaster
        try {
            master_fd = NetworkUtils::connect_to_server(master_hostname, master_port);
            
            // Receive player ID and total number of players
            SetupInfo setup = NetworkUtils::receive_setup_info(master_fd);
            id = setup.player_id;
            num_players = setup.total_players;
            
            // Seed RNG with player ID to make each player's randomness different
            rng.seed(rd() + id);
            
            // Send listening port to ringmaster
            NetworkUtils::send_message(master_fd, NEIGHBOR_INFO, &listen_port, sizeof(listen_port));
            
            std::cout << "Connected as player " << id << " out of " << num_players << " total players" << std::endl;
            
            // Receive neighbor information
            NeighborInfo neighbors = NetworkUtils::receive_neighbor_info(master_fd);
            left_id = neighbors.left_id;
            right_id = neighbors.right_id;
            
            // Setup connections with neighbors
            setup_neighbors(neighbors);
            
        } catch (const NetworkError& e) {
            std::cerr << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    
    ~Player() {
        close(master_fd);
        close(left_fd);
        close(right_fd);
        close(listen_fd);
    }
    
    void setup_neighbors(const NeighborInfo& neighbors) {
        // This approach to establishing connections prevents deadlock
        // First, all players connect to their right neighbors
        // Then all players accept connections from their left neighbors
        
        // Connect to right neighbor if this player isn't the highest ID
        // (Highest ID player waits for everyone else to connect first)
        if (id != num_players - 1) {
            try {
                right_fd = NetworkUtils::connect_to_server(neighbors.right_ip, neighbors.right_port);
            } catch (const NetworkError& e) {
                std::cerr << e.what() << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        
        // Accept connection from left neighbor
        try {
            std::string left_ip;
            left_fd = NetworkUtils::accept_connection(listen_fd, &left_ip);
        } catch (const NetworkError& e) {
            std::cerr << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
        
        // If this is the highest ID player, connect to right neighbor (which is player 0)
        if (id == num_players - 1) {
            try {
                right_fd = NetworkUtils::connect_to_server(neighbors.right_ip, neighbors.right_port);
            } catch (const NetworkError& e) {
                std::cerr << e.what() << std::endl;
                exit(EXIT_FAILURE);
            }
        }
    }
    
    void play_game() {
        fd_set read_fds;
        int max_fd = std::max(master_fd, std::max(left_fd, right_fd));
        
        // Main game loop
        while (true) {
            // Set up select() to monitor all sockets
            FD_ZERO(&read_fds);
            FD_SET(master_fd, &read_fds);
            FD_SET(left_fd, &read_fds);
            FD_SET(right_fd, &read_fds);
            
            // Wait for data on any socket
            if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
                std::cerr << "Error in select" << std::endl;
                exit(EXIT_FAILURE);
            }
            
            // Check each socket for data
            try {
                // Check sockets for data
                if (FD_ISSET(master_fd, &read_fds)) {
                    try {
                        Potato potato = NetworkUtils::receive_potato(master_fd);
                        if (potato.get_hops() == 0) {
                            // Game over signal
                            break;
                        }
                        handle_potato(potato);
                    } catch (const NetworkError& e) {
                        // If we get an error from the master, it's likely game over
                        if (std::string(e.what()).find("connection reset") != std::string::npos ||
                            std::string(e.what()).find("connection closed") != std::string::npos) {
                            break;
                        }
                        throw; // Re-throw other errors
                    }
                }
                
                if (FD_ISSET(left_fd, &read_fds)) {
                    try {
                        Potato potato = NetworkUtils::receive_potato(left_fd);
                        if (potato.get_hops() == 0) {
                            break;  // Game over
                        }
                        handle_potato(potato);
                    } catch (const NetworkError& e) {
                        // Likely shutdown in progress
                        break;
                    }
                }
                
                if (FD_ISSET(right_fd, &read_fds)) {
                    try {
                        Potato potato = NetworkUtils::receive_potato(right_fd);
                        if (potato.get_hops() == 0) {
                            break;  // Game over
                        }
                        handle_potato(potato);
                    } catch (const NetworkError& e) {
                        // Likely shutdown in progress
                        break;
                    }
                }
            } catch (const NetworkError& e) {
                // During shutdown, we might get errors when connections close
                if (std::string(e.what()).find("connection reset") != std::string::npos ||
                    std::string(e.what()).find("connection closed") != std::string::npos) {
                    // Normal shutdown - just exit the game loop
                    break;
                } else {
                    // Unexpected error during active game
                    std::cerr << e.what() << std::endl;
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
    
    void handle_potato(Potato& potato) {
        // Decrement hop count
        potato.decrement_hop();
        
        // Add this player to the trace
        potato.add_to_trace(id);
        
        // Check if the potato is done
        if (potato.get_hops() == 0) {
            std::cout << "I'm it" << std::endl;
            
            // Send potato back to ringmaster
            try {
                NetworkUtils::send_potato(master_fd, potato);
            } catch (const NetworkError& e) {
                std::cerr << e.what() << std::endl;
                exit(EXIT_FAILURE);
            }
        } else {
            // Randomly choose left or right neighbor
            std::uniform_int_distribution<int> dist(0, 1);
            int random_choice = dist(rng);
            
            // Pass potato to chosen neighbor
            try {
                if (random_choice == 0) {
                    std::cout << "Sending potato to " << left_id << std::endl;
                    NetworkUtils::send_potato(left_fd, potato);
                } else {
                    std::cout << "Sending potato to " << right_id << std::endl;
                    NetworkUtils::send_potato(right_fd, potato);
                }
            } catch (const NetworkError& e) {
                std::cerr << e.what() << std::endl;
                exit(EXIT_FAILURE);
            }
        }
    }
};

int main(int argc, char* argv[]) {
    // Check command line arguments
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <machine_name> <port_num>" << std::endl;
        return EXIT_FAILURE;
    }
    
    // Parse arguments
    std::string master_hostname(argv[1]);
    int master_port = std::atoi(argv[2]);
    
    // Validate arguments
    if (master_port < 1 || master_port > 65535) {
        std::cerr << "Error: port must be between 1 and 65535" << std::endl;
        return EXIT_FAILURE;
    }
    
    // Create player and run the game
    Player player(master_hostname, master_port);
    player.play_game();
    
    return EXIT_SUCCESS;
}