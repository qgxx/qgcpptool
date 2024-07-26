#pragma once

#include <thread>
#include <vector>

namespace qg {

class join_threads {

public:
    explicit join_threads(std::vector<std::thread>& threads_) : threads(threads_) {}
    ~join_threads() {
        for (unsigned long i = 0; i < threads.size(); ++i) {
            if (threads[i].joinable()) threads[i].join();
        }
    }

private:
    std::vector<std::thread>& threads;

};

} // namespace qg