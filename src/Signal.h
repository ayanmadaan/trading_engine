#include "../utils/logger.hpp"
#include "format.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <signal.h>
#include <sys/epoll.h>
#include <thread>
#include <unistd.h>

class Signal {
public:
    Signal() { this->instance = nullptr; }
    ~Signal() {
        instance = nullptr;
        if(epoll_fd != -1) {
            close(epoll_fd);
        }
        if(signal_pipe[0] != -1) {
            close(signal_pipe[0]);
        }
        if(signal_pipe[1] != -1) {
            close(signal_pipe[1]);
        }
    }

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&&) = delete;
    Signal& operator=(Signal&&) = delete;

    void stop() {
        this->running = false;
        if(signal_pipe[1] != -1) {
            char buf = 1;
            auto val = write(signal_pipe[1], &buf, 1);
        }
    }

    void start() { this->running = true; }

    bool isRunning() { return this->running; }

    bool setupSignalHandlers() {
        instance = this;
        struct sigaction sa;
        sa.sa_handler = &Signal::staticSignalHandler;
        sa.sa_flags = SA_RESTART;
        sigemptyset(&sa.sa_mask);

        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        epoll_fd = epoll_create1(0);
        if(epoll_fd == -1) {
            perror("epoll_create");
            return false;
        }

        // Setup signal pipe for event notification

        if(pipe(signal_pipe) == -1) {
            perror("pipe");
            close(epoll_fd);
            return false;
        }

        // Add read end of pipe to epoll
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = signal_pipe[0];
        if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, signal_pipe[0], &ev) == -1) {
            perror("epoll_ctl");
            close(epoll_fd);
            close(signal_pipe[0]);
            close(signal_pipe[1]);
            return false;
        }
        return true;
    }

    template<typename Strategy>
    void handleStrategy(Strategy& strategy, const std::chrono::seconds& timeout_duration = std::chrono::seconds(30)) {
        auto start_time = std::chrono::steady_clock::now();

        while(isRunning()) {
            // Wait for trading to be ready
            while(!is_trading_ready) {
                // Check if trading is ready
                is_trading_ready = strategy.is_trading_ready();

                // If trading is ready, break out of the loop
                if(is_trading_ready) {
                    break;
                }

                // Wait for the next event in the epoll loop
                struct epoll_event events[1];
                int nfds = epoll_wait(epoll_fd, events, 1, 1000);

                // If an error occurs, return
                if(nfds == -1) {
                    perror("epoll_wait");
                    return;
                }

                // If an event is received, break out of the loop
                if(nfds > 0) {
                    char buf;
                    auto val = read(events[0].data.fd, &buf, 1);
                    break;
                }

                // If the timeout duration has been exceeded, throw an error
                auto current_time = std::chrono::steady_clock::now();
                auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time);

                if(elapsed_time >= timeout_duration) {
                    throw std::runtime_error("Timeout: strategy did not become ready within the specified time");
                }

                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            if(!is_trading_initialized) {
                strategy.initialize_trading();
                strategy.start_trading();
                is_trading_initialized = true;
            }

            if(!isRunning()) {
                break;
            }

            struct epoll_event events[1];
            int nfds = epoll_wait(epoll_fd, events, 1, 1000); // 1-second timeout

            if(nfds == -1) {
                perror("epoll_wait");
                break;
            }

            if(nfds > 0) {
                // Signal received
                char buf;
                auto val = read(events[0].data.fd, &buf, 1);
                break;
            }
        }
    }

    int epoll_fd = -1;

private:
    int signal_pipe[2] = {-1, -1};
    static Signal* instance;

    static void staticSignalHandler(int signum) {
        if(instance) {
            instance->signalHandler(signum);
        }
    }

    void signalHandler(int signum) {
        LoggerSingleton::get().infra().info(f("event", "signal_received") + " " + f("signal", signum));
        stop();
    }

    std::atomic<bool> running{true};

private:
    bool is_trading_ready = false;
    bool is_trading_initialized = false;
};

Signal* Signal::instance = nullptr;
