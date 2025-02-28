#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <random>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <time.h>

#include "potato.h"
#include "network_utils.h"

class Ringmaster {
private:
    int num_players;
    int num_hops;
    int server_fd;
    std::vector<int> player_fds;
    std::vector<std::string> player_ips;
    std::vector<int> player_ports;
    std::mt19937 rng;  // Random number generator

public:
    Ringmaster(int port, int players, int hops) 
        : num_players(players), num_hops(hops) {
        // Initialize random number generator
        std::random_device rd;
        rng.seed(rd());
        
        // Create server socket
        try {
            server_fd = NetworkUtils::create_server_socket(port);
        } catch (const NetworkError& e) {
            std::cerr << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
        
        std::cout << "Potato Ringmaster" << std::endl;
        std::cout << "Players = " << num_players << std::endl;
        std::cout << "Hops = " << num_hops << std::endl;
    }
    
    ~Ringmaster() {
        // Close all player connections
        for (int fd : player_fds) {
            close(fd);
        }
        
        // Close server socket
        close(server_fd);
    }
    
    void setup_game() {
        // Wait for all players to connect
        for (int i = 0; i < num_players; i++) {
            try {
                std::string player_ip;
                int player_fd = NetworkUtils::accept_connection(server_fd, &player_ip);
                player_fds.push_back(player_fd);
                player_ips.push_back(player_ip);
                
                // Send player its ID and the total number of players
                NetworkUtils::send_setup_info(player_fd, i, num_players);
                
                // Receive player's port for neighbor connections
                std::vector<char> data;
                NetworkUtils::receive_message(player_fd, data);
                int player_port = *reinterpret_cast<int*>(data.data());
                player_ports.push_back(player_port);
                
                std::cout << "Player " << i << " is ready to play" << std::endl;
            } catch (const NetworkError& e) {
                std::cerr << e.what() << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        
        // Send each player information about its neighbors
        for (int i = 0; i < num_players; i++) {
            int left_id = (i + num_players - 1) % num_players;
            int right_id = (i + 1) % num_players;
            
            try {
                NetworkUtils::send_neighbor_info(
                    player_fds[i],
                    left_id, right_id,
                    player_ips[left_id], player_ips[right_id],
                    player_ports[left_id], player_ports[right_id]
                );
            } catch (const NetworkError& e) {
                std::cerr << e.what() << std::endl;
                exit(EXIT_FAILURE);
            }
        }
    }
    
    void play_game() {
        // If num_hops is 0, just end the game immediately
        if (num_hops == 0) {
            for (int fd : player_fds) {
                try {
                    NetworkUtils::send_game_over(fd);
                } catch (const NetworkError& e) {
                    std::cerr << e.what() << std::endl;
                }
            }
            return;
        }
        
        // Create potato with specified number of hops
        Potato potato(num_hops);
        
        // Choose a random player to start with
        std::uniform_int_distribution<int> dist(0, num_players - 1);
        int random_player = dist(rng);
        
        std::cout << "Ready to start the game, sending potato to player " << random_player << std::endl;
        
        // Send potato to the first player
        try {
            NetworkUtils::send_potato(player_fds[random_player], potato);
        } catch (const NetworkError& e) {
            std::cerr << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
        
        // Wait for potato to come back
        fd_set read_fds;
        FD_ZERO(&read_fds);
        
        int max_fd = 0;
        for (int fd : player_fds) {
            FD_SET(fd, &read_fds);
            max_fd = std::max(max_fd, fd);
        }
        
        // Wait for potato to come back from any player
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            std::cerr << "Error in select" << std::endl;
            exit(EXIT_FAILURE);
        }
        
        // Find the player that sent the potato back
        Potato final_potato;
        for (int i = 0; i < num_players; i++) {
            if (FD_ISSET(player_fds[i], &read_fds)) {
                try {
                    final_potato = NetworkUtils::receive_potato(player_fds[i]);
                    break;
                } catch (const NetworkError& e) {
                    std::cerr << e.what() << std::endl;
                    exit(EXIT_FAILURE);
                }
            }
        }
        
        // Print trace of potato
        std::cout << "Trace of potato:" << std::endl;
        std::cout << final_potato.get_trace_string() << std::endl;
        
        // Send termination signal to all players
        for (int fd : player_fds) {
            try {
                NetworkUtils::send_game_over(fd);
            } catch (const NetworkError& e) {
                std::cerr << e.what() << std::endl;
            }
        }
    }
};

int main(int argc, char* argv[]) {
    // Check command line arguments
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <port_num> <num_players> <num_hops>" << std::endl;
        return EXIT_FAILURE;
    }
    
    // Parse arguments
    int port = std::atoi(argv[1]);
    int num_players = std::atoi(argv[2]);
    int num_hops = std::atoi(argv[3]);
    
    // Validate arguments
    if (port < 1 || port > 65535) {
        std::cerr << "Error: port must be between 1 and 65535" << std::endl;
        return EXIT_FAILURE;
    }
    
    if (num_players < 2) {
        std::cerr << "Error: number of players must be at least 2" << std::endl;
        return EXIT_FAILURE;
    }
    
    if (num_hops < 0 || num_hops > 512) {
        std::cerr << "Error: hops must be between 0 and 512" << std::endl;
        return EXIT_FAILURE;
    }
    
    // Create ringmaster and run the game
    Ringmaster ringmaster(port, num_players, num_hops);
    ringmaster.setup_game();
    ringmaster.play_game();
    
    return EXIT_SUCCESS;
}