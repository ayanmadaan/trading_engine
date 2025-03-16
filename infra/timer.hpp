#include <any>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class CallbackInterface {
public:
    virtual ~CallbackInterface() = default;
    virtual void invoke() = 0;
};

template<typename Func, typename... Args>
class Callback : public CallbackInterface {
public:
    Callback(Func&& func, Args&&... args)
        : m_func(std::forward<Func>(func))
        , m_args(std::forward<Args>(args)...) {}

    void invoke() override { invokeImpl(std::index_sequence_for<Args...>{}); }

private:
    template<size_t... Is>
    void invokeImpl(std::index_sequence<Is...>) {
        m_func(std::get<Is>(m_args)...);
    }

    Func m_func;
    std::tuple<Args...> m_args;
};

class Timer {
public:
    Timer()
        : running(false) {}

    template<typename Func>
    void addCallback(Func&& callback) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        callbacks.push_back(std::make_unique<Callback<Func>>(std::forward<Func>(callback)));
    }

    template<typename Func, typename... Args>
    void addCallback(Func&& callback, Args&&... args) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        callbacks.push_back(
            std::make_unique<Callback<Func, Args...>>(std::forward<Func>(callback), std::forward<Args>(args)...));
    }

    void clearCallbacks() {
        std::lock_guard<std::mutex> lock(callbackMutex);
        callbacks.clear();
    }

    void start(uint64_t milliseconds) {
        if(running) {
            stop();
        }
        running = true;

        timerThread = std::thread([this, milliseconds]() {
            while(running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
                if(running) {
                    triggerCallbacks();
                }
            }
        });
    }

    void stop() {
        running = false;
        if(timerThread.joinable()) {
            timerThread.join();
        }
    }

    ~Timer() { stop(); }

private:
    void triggerCallbacks() {
        std::lock_guard<std::mutex> lock(callbackMutex);
        for(auto& callback : callbacks) {
            if(callback) {
                callback->invoke();
            }
        }
    }

    std::atomic<bool> running;
    std::thread timerThread;
    std::mutex callbackMutex;
    std::vector<std::unique_ptr<CallbackInterface>> callbacks;
};
