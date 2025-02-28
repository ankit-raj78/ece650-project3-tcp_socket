#ifndef POTATO_H
#define POTATO_H

#include <iostream>
#include <string>
#include <vector>

#define MAX_HOPS 512

// Potato class: represents the "hot potato" that gets passed between players
class Potato {
private:
    int remaining_hops;
    std::vector<int> trace;

public:
    // Default constructor - creates a potato with 0 hops
    Potato() : remaining_hops(0) {}
    
    // Create a potato with a specific number of hops
    Potato(int hops) : remaining_hops(hops) {}
    
    // Get the number of remaining hops
    int get_hops() const { return remaining_hops; }
    
    // Decrement the number of hops
    void decrement_hop() { remaining_hops--; }
    
    // Add a player ID to the trace
    void add_to_trace(int player_id) {
        trace.push_back(player_id);
    }
    
    // Get the trace as a comma-separated string
    std::string get_trace_string() const {
        if (trace.empty()) return "";
        
        std::string result;
        for (size_t i = 0; i < trace.size() - 1; i++) {
            result += std::to_string(trace[i]) + ",";
        }
        result += std::to_string(trace.back());
        return result;
    }
    
    // Get the trace vector
    const std::vector<int>& get_trace() const {
        return trace;
    }
    
    // Serialize the potato for network transmission
    void serialize(char* buffer) const {
        int* int_buf = reinterpret_cast<int*>(buffer);
        int_buf[0] = remaining_hops;
        int_buf[1] = static_cast<int>(trace.size());
        
        for (size_t i = 0; i < trace.size(); i++) {
            int_buf[2 + i] = trace[i];
        }
    }
    
    // Deserialize the potato from network transmission
    void deserialize(const char* buffer) {
        const int* int_buf = reinterpret_cast<const int*>(buffer);
        remaining_hops = int_buf[0];
        
        int trace_size = int_buf[1];
        trace.clear();
        
        for (int i = 0; i < trace_size; i++) {
            trace.push_back(int_buf[2 + i]);
        }
    }
    
    // Get the size of the serialized potato
    static int get_serialized_size(int trace_size) {
        return (2 + trace_size) * sizeof(int);
    }
    
    // Get the size of the serialized potato
    int get_serialized_size() const {
        return get_serialized_size(trace.size());
    }
};

// Message types for communication between ringmaster and players
enum MessageType {
    SETUP_INFO = 1,       // Initial setup info
    NEIGHBOR_INFO = 2,    // Neighbor connection info
    POTATO_TRANSFER = 3,  // Potato being passed
    GAME_OVER = 4         // Signal game termination
};

// Structure for a network message header
struct MessageHeader {
    MessageType type;
    int size;  // Size of the payload
    
    void serialize(char* buffer) const {
        int* int_buf = reinterpret_cast<int*>(buffer);
        int_buf[0] = static_cast<int>(type);
        int_buf[1] = size;
    }
    
    void deserialize(const char* buffer) {
        const int* int_buf = reinterpret_cast<const int*>(buffer);
        type = static_cast<MessageType>(int_buf[0]);
        size = int_buf[1];
    }
    
    static const int HEADER_SIZE = 2 * sizeof(int);
};

// Structure for setup information
struct SetupInfo {
    int player_id;
    int total_players;
    
    void serialize(char* buffer) const {
        int* int_buf = reinterpret_cast<int*>(buffer);
        int_buf[0] = player_id;
        int_buf[1] = total_players;
    }
    
    void deserialize(const char* buffer) {
        const int* int_buf = reinterpret_cast<const int*>(buffer);
        player_id = int_buf[0];
        total_players = int_buf[1];
    }
    
    static const int SIZE = 2 * sizeof(int);
};

// Structure for neighbor information
struct NeighborInfo {
    int left_id;
    int right_id;
    char left_ip[64];
    char right_ip[64];
    int left_port;
    int right_port;
    
    void serialize(char* buffer) const {
        int* int_buf = reinterpret_cast<int*>(buffer);
        int_buf[0] = left_id;
        int_buf[1] = right_id;
        int_buf[2] = left_port;
        int_buf[3] = right_port;
        
        char* char_buf = buffer + 4 * sizeof(int);
        std::strncpy(char_buf, left_ip, 64);
        std::strncpy(char_buf + 64, right_ip, 64);
    }
    
    void deserialize(const char* buffer) {
        const int* int_buf = reinterpret_cast<const int*>(buffer);
        left_id = int_buf[0];
        right_id = int_buf[1];
        left_port = int_buf[2];
        right_port = int_buf[3];
        
        const char* char_buf = buffer + 4 * sizeof(int);
        std::strncpy(left_ip, char_buf, 64);
        std::strncpy(right_ip, char_buf + 64, 64);
    }
    
    static const int SIZE = 4 * sizeof(int) + 128;
};

#endif // POTATO_H