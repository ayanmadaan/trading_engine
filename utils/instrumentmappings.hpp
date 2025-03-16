#pragma once
#include <unordered_map>
#include <string>

namespace mapping {

    struct InstrumentInfo {
        std::string instrument;
        std::string category;
    };

    InstrumentInfo getInstrumentInfo(std::string_view key) {
        if  (key == "okx_perp_btc_usdt") return { "BTC-USDT-SWAP", "SWAP" };
        else if  (key == "okx_perp_eth_usdt") return { "ETH-USDT-SWAP", "SWAP" };
        else if  (key == "okx_spot_btc_usdt") return { "BTC-USDT", "spot" };
        else if  (key == "okx_perp_doge_usdt") return { "DOGE-USDT-SWAP", "SWAP"};
        else if  (key == "bybit_perp_doge_usdt") return { "DOGEUSDT", "linear"};
        else if  (key == "bybit_perp_btc_usdt") return {"BTCUSDT", "linear"};
        else if  (key == "bybit_perp_eth_usdt") return {"ETHUSDT", "linear"};
        else if  (key == "binance_perp_btc_usdt") return {"btcusdt","PERP"};
        else if  (key == "binance_perp_doge_usdt") return {"dogeusdt", "PERP"};
        else if  (key == "binance_perp_eth_usdt") return {"ethusdt","PERP"};
        else return { "", "" }; // Default empty result if not found
    }

    std::string getMockInstrument(std::string instr) {
        if (instr == "67824") return "btcusdt";
        else if (instr == "67825") return "ethusdt";
        else if (instr == "72026") return "dogeusdt";
        else if (instr == "binance_perp_btc_usdt") return "btcusdt";
        else if (instr == "binance_perp_doge_usdt") return "dogeusdt";
        else if (instr == "binance_perp_eth_usdt") return "ethusdt";
        else return "";
    }


}
