#pragma once

#include <string>
namespace Connections {

std::string getBinanceLiveMarket() { return "wss://fstream.binance.com/ws";}

std::string getBinanceMockMarket() { return "wss://fstream.binancefuture.com/ws"; }

std::string getBinanceProxy() { return ""; }

std::string getOkxLiveMarket() { return "wss://ws.okx.com:8443/ws/v5/public"; }

std::string getOkxMockMarket() { return "wss://wspap.okx.com:8443/ws/v5/public"; }

std::string getOkxLiveOrder() { return "wss://ws.okx.com:8443/ws/v5/private"; }

std::string getOkxTestOrder() { return "wss://wspap.okx.com:8443/ws/v5/private?brokerId=9999"; }

std::string getOkxProxy() { return "http://172.28.91.131:8889"; }

std::string getByBitLiveMarket() { return "wss://stream.bybit.com/v5/public/linear"; }

std::string getByBitMockMarket() { return "wss://stream-testnet.bybit.com/v5/public/linear"; }

std::string getByBitProxy() { return ""; }

std::string getByBitLiveOrder() { return "wss://stream.bybit.com/v5/trade"; }

std::string getByBitTestOrder() { return "wss://stream-testnet.bybit.com/v5/trade"; }

std::string getByBitTestFills() { return "wss://stream-testnet.bybit.com/v5/private"; }

std::string getByBitLiveFills() { return "wss://stream.bybit.com/v5/private"; }

std::string getByBitLiveCurlBaseUrl() { return "https://api.bybit.com"; }

std::string getByBitTestCurlBaseUrl() { return "https://api-testnet.bybit.com"; }

std::string getOkxLiveCurlBaseUrl() { return "https://www.okx.com"; }

std::string getOkxMockCurlBaseUrl() { return "https://www.okx.com"; }

}; // namespace Connections
