#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <iostream>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

#include "potato.h"

// Exception class for network operations
class NetworkError : public std::runtime_error {
public:
    NetworkError(const std::string& message) : std::runtime_error(message) {}
};

// Class that handles network operations
class NetworkUtils {
public:
    // Create a server socket that listens for connections
    static int create_server_socket(int port) {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw NetworkError("Failed to create socket");
        }
        
        // Allow reuse of address
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(server_fd);
            throw NetworkError("Failed to set socket options");
        }
        
        // Bind to port
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            close(server_fd);
            throw NetworkError("Failed to bind to port " + std::to_string(port));
        }
        
        // Listen for connections
        if (listen(server_fd, 10) < 0) {
            close(server_fd);
            throw NetworkError("Failed to listen on socket");
        }
        
        return server_fd;
    }
    
    // Create a server socket with automatic port assignment
    static int create_server_socket(int* assigned_port) {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw NetworkError("Failed to create socket");
        }
        
        // Allow reuse of address
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(server_fd);
            throw NetworkError("Failed to set socket options");
        }
        
        // Bind to port 0 (let OS assign port)
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(0);
        
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            close(server_fd);
            throw NetworkError("Failed to bind to automatic port");
        }
        
        // Get assigned port
        socklen_t len = sizeof(address);
        if (getsockname(server_fd, (struct sockaddr*)&address, &len) < 0) {
            close(server_fd);
            throw NetworkError("Failed to get socket name");
        }
        
        *assigned_port = ntohs(address.sin_port);
        
        // Listen for connections
        if (listen(server_fd, 10) < 0) {
            close(server_fd);
            throw NetworkError("Failed to listen on socket");
        }
        
        return server_fd;
    }
    
    // Accept a connection on a server socket
    static int accept_connection(int server_fd, std::string* client_ip = nullptr) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            throw NetworkError("Failed to accept connection");
        }
        
        if (client_ip != nullptr) {
            *client_ip = inet_ntoa(client_addr.sin_addr);
        }
        
        return client_fd;
    }
    
    // Connect to a server
    static int connect_to_server(const std::string& hostname, int port) {
        struct hostent* server = gethostbyname(hostname.c_str());
        if (server == nullptr) {
            throw NetworkError("Failed to resolve hostname " + hostname);
        }
        
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (client_fd < 0) {
            throw NetworkError("Failed to create socket");
        }
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        
        if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(client_fd);
            throw NetworkError("Failed to connect to " + hostname + ":" + std::to_string(port));
        }
        
        return client_fd;
    }
    
    // Get the hostname of the local machine
    static std::string get_hostname() {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) < 0) {
            throw NetworkError("Failed to get hostname");
        }
        return std::string(hostname);
    }
    
    // Get the local IP address
    static std::string get_local_ip() {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) < 0) {
            throw NetworkError("Failed to get hostname");
        }
        
        struct hostent* host = gethostbyname(hostname);
        if (host == nullptr) {
            throw NetworkError("Failed to resolve hostname");
        }
        
        char* ip = inet_ntoa(*(struct in_addr*)host->h_addr);
        return std::string(ip);
    }
    
    // Send a message with a header
    static void send_message(int fd, MessageType type, const void* data, int size) {
        MessageHeader header;
        header.type = type;
        header.size = size;
        
        char header_buf[MessageHeader::HEADER_SIZE];
        header.serialize(header_buf);
        
        if (send_all(fd, header_buf, MessageHeader::HEADER_SIZE) < 0) {
            throw NetworkError("Failed to send message header");
        }
        
        if (size > 0 && data != nullptr) {
            if (send_all(fd, data, size) < 0) {
                throw NetworkError("Failed to send message data");
            }
        }
    }
    
    // Receive a message with a header
    static MessageHeader receive_message(int fd, std::vector<char>& data) {
        MessageHeader header;
        char header_buf[MessageHeader::HEADER_SIZE];
        
        if (recv_all(fd, header_buf, MessageHeader::HEADER_SIZE) < 0) {
            throw NetworkError("Failed to receive message header");
        }
        
        header.deserialize(header_buf);
        
        if (header.size > 0) {
            data.resize(header.size);
            if (recv_all(fd, data.data(), header.size) < 0) {
                throw NetworkError("Failed to receive message data");
            }
        } else {
            data.clear();
        }
        
        return header;
    }
    
    // Send a potato
    static void send_potato(int fd, const Potato& potato) {
        int size = potato.get_serialized_size();
        std::vector<char> buffer(size);
        potato.serialize(buffer.data());
        
        send_message(fd, POTATO_TRANSFER, buffer.data(), size);
    }
    
    // Receive a potato
    static Potato receive_potato(int fd) {
        std::vector<char> data;
        MessageHeader header = receive_message(fd, data);
        
        if (header.type != POTATO_TRANSFER) {
            throw NetworkError("Expected POTATO_TRANSFER message, got " + std::to_string(header.type));
        }
        
        Potato potato;
        potato.deserialize(data.data());
        return potato;
    }
    
    // Send setup info
    static void send_setup_info(int fd, int player_id, int total_players) {
        SetupInfo info;
        info.player_id = player_id;
        info.total_players = total_players;
        
        char buffer[SetupInfo::SIZE];
        info.serialize(buffer);
        
        send_message(fd, SETUP_INFO, buffer, SetupInfo::SIZE);
    }
    
    // Receive setup info
    static SetupInfo receive_setup_info(int fd) {
        std::vector<char> data;
        MessageHeader header = receive_message(fd, data);
        
        if (header.type != SETUP_INFO) {
            throw NetworkError("Expected SETUP_INFO message, got " + std::to_string(header.type));
        }
        
        SetupInfo info;
        info.deserialize(data.data());
        return info;
    }
    
    // Send neighbor info
    static void send_neighbor_info(int fd, int left_id, int right_id, 
                                  const std::string& left_ip, const std::string& right_ip,
                                  int left_port, int right_port) {
        NeighborInfo info;
        info.left_id = left_id;
        info.right_id = right_id;
        info.left_port = left_port;
        info.right_port = right_port;
        
        strncpy(info.left_ip, left_ip.c_str(), 64);
        strncpy(info.right_ip, right_ip.c_str(), 64);
        
        char buffer[NeighborInfo::SIZE];
        info.serialize(buffer);
        
        send_message(fd, NEIGHBOR_INFO, buffer, NeighborInfo::SIZE);
    }
    
    // Receive neighbor info
    static NeighborInfo receive_neighbor_info(int fd) {
        std::vector<char> data;
        MessageHeader header = receive_message(fd, data);
        
        if (header.type != NEIGHBOR_INFO) {
            throw NetworkError("Expected NEIGHBOR_INFO message, got " + std::to_string(header.type));
        }
        
        NeighborInfo info;
        info.deserialize(data.data());
        return info;
    }
    
    // Send game over message
    static void send_game_over(int fd) {
        send_message(fd, GAME_OVER, nullptr, 0);
    }
    
private:
    // Send all data
    static int send_all(int fd, const void* data, int size) {
        const char* ptr = static_cast<const char*>(data);
        int remaining = size;
        
        while (remaining > 0) {
            int sent = send(fd, ptr, remaining, 0);
            if (sent <= 0) {
                return -1;
            }
            
            ptr += sent;
            remaining -= sent;
        }
        
        return size;
    }
    
    // Receive all data
    static int recv_all(int fd, void* data, int size) {
        char* ptr = static_cast<char*>(data);
        int remaining = size;
        
        while (remaining > 0) {
            int received = recv(fd, ptr, remaining, 0);
            if (received <= 0) {
                return -1;
            }
            
            ptr += received;
            remaining -= received;
        }
        
        return size;
    }
};

#endif // NETWORK_UTILS_H