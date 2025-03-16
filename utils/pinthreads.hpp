#ifndef PINTHREADS_HPP
#define PINTHREADS_HPP
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <thread>

inline void setThreadAffinity(std::thread& t, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset); // Clear the CPU set
    CPU_SET(core_id, &cpuset); // Add the core to the set

    // Get the native handle of the thread
    pthread_t native_handle = t.native_handle();

    // Set the affinity of the thread to the specified core
    int result = pthread_setaffinity_np(native_handle, sizeof(cpu_set_t), &cpuset);
    if(result != 0) {
        throw std::runtime_error("Error setting thread affinity: " + std::string(std::strerror(errno)));
    }
}
#endif