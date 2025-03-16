#pragma once
#include <cstring>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include "../utils/logger.hpp"

void pinThreadToCore(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();

    // Set the affinity for the thread to the specified core
    int result = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    if(result != 0) {
        LoggerSingleton::get().infra().error("error setting thread affinity: ", strerror(result));
    } else {
        LoggerSingleton::get().infra().info("thread pinned to requested core ",core_id ,", actually running on core ",sched_getcpu());
    }
}
