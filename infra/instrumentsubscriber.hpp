// instrument_subscriber.hpp
#pragma once
#include <string>

template<typename Derived>
class InstrumentSubscriber {
public:
    void onInstrumentUpdate(std::string marketDataUpdate) {
        // Forward to the derived class's implementation
        static_cast<Derived*>(this)->onInstrumentUpdateImpl(marketDataUpdate);
    }
};
