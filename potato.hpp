#ifndef _POTATO_HPP_
#define _POTATO_HPP_

#include <vector>
#include <string> // Use standard string header instead of bits/basic_string.h

#define MAX_HOPS 512

class Potato {
public:
    int hops;
    std::vector<int> trace;

    Potato() : hops(0) {}
    
    void addTrace(int player_id) {
        trace.push_back(player_id);
    }
    
    std::string getTraceString() const {
        std::string result;
        for (size_t i = 0; i < trace.size(); i++) {
            result += std::to_string(trace[i]);
            if (i < trace.size() - 1) {
                result += ",";
            }
        }
        return result;
    }
};

#endif // _POTATO_HPP_