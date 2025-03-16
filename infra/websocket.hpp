#pragma once
#include "../utils/logger.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <string>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

typedef websocketpp::client<websocketpp::config::asio_tls_client> client_tls;
typedef websocketpp::client<websocketpp::config::asio_client> client_non_tls;

// CRTP base class
template<typename Derived>
class WebSocketClient {
public:
    WebSocketClient(const uint32_t retry_limit,
                    const std::string& uri,
                    const std::string& proxy_uri = "",
                    bool tls = true)
        : retry_limit(retry_limit)
        , uri(uri)
        , proxy_uri(proxy_uri)
        , tls(tls) {
        if(tls) {
            ws_client = std::make_unique<client_tls>();
        } else {
            binance_client = std::make_unique<client_non_tls>();
        }
    }

    void request_shutdown() {
        shutdown_requested = true;
        stop();
    }

    // Start connection
    void start() {
        reconnect_attempt = 0; // reset
        connect_to_websocket();
    }

    void connect_to_websocket() {
        setupClient();
        websocketpp::lib::error_code ec;
        client_tls::connection_ptr con;
        client_non_tls::connection_ptr con_non_tls;
        if(tls) {
            con = ws_client->get_connection(uri, ec);
            LOG_INFRA_DEBUG("con setup ok");
        } else {
            con_non_tls = binance_client->get_connection(uri, ec);
            LOG_INFRA_DEBUG("con_non_tls setup ok");
        }
        if(ec) {
            LoggerSingleton::get().infra().error("connection error: ", ec.message());
            return;
        }

        // Optional proxy setup
        if(!proxy_uri.empty()) {
            con->set_proxy(proxy_uri);
        }
        if(tls)
            ws_client->connect(con);
        else
            binance_client->connect(con_non_tls);
        if(tls)
            ws_client->run();
        else
            binance_client->run();
    }

    void stop() {
        if(cleaning_up.exchange(true)) {
            return; // cleanup already underway, so return immediately.
        }
        try {
            if(tls) {
                if(ws_client) {
                    LOG_INFRA_DEBUG("stopping TLS client");
                    ws_client->stop(); // Stop the client
                    ws_client.reset(); // Fully destroy the client
                }
            } else {
                if(binance_client) {
                    LOG_INFRA_DEBUG("stopping non-TLS client");
                    binance_client->stop(); // Stop the client
                    binance_client.reset(); // Fully destroy the client
                }
            }
        } catch(const std::exception& e) {
            LoggerSingleton::get().infra().error("error stopping websocket client: ", e.what());
        }
    }

    void schedule_reconnection() {
        if(shutdown_requested) {
            LOG_INFRA_DEBUG("Shutdown requested; not scheduling reconnection");
            return;
        }
        LOG_INFRA_DEBUG("attempting to restart md channel");
        try {
            stop();
            cleaning_up = false;
            if(tls) {
                LOG_INFRA_DEBUG("creating new TLS client instance");
                ws_client = std::make_unique<client_tls>();
            } else {
                LOG_INFRA_DEBUG("creating new non-TLS client instance");
                binance_client = std::make_unique<client_non_tls>();
            }
            // Reconnect
            LOG_INFRA_DEBUG("attempting to reconnect");
            connect_to_websocket();
        } catch(const std::exception& e) {
            LoggerSingleton::get().infra().error("error during reconnection: ", e.what());
        }
    }

protected:
    void setupClient() {
        if(!tls) {
            // if (!binance_client) {
            //     LOG_INFRA_DEBUG("Non tls client already exists");
            //     return;
            // }
            binance_client->init_asio();
            binance_client->clear_access_channels(websocketpp::log::alevel::all);
            binance_client->clear_error_channels(websocketpp::log::elevel::all);
            binance_client->set_message_handler(
                [this](websocketpp::connection_hdl hdl, client_non_tls::message_ptr msg) {
                    static_cast<Derived*>(this)->onMessage(hdl, msg);
                });
            binance_client->set_open_handler([this](websocketpp::connection_hdl hdl) {
                current_hdl = hdl;
                static_cast<Derived*>(this)->onOpen(hdl);
            });
            binance_client->set_close_handler([this](websocketpp::connection_hdl hdl) {
                if(reconnect_attempt + 1 > retry_limit) {
                    std::string message = "connection_end";
                    static_cast<Derived*>(this)->onClose(hdl, message);
                } else {
                    std::string message = "disconnect";
                    static_cast<Derived*>(this)->onClose(hdl, message);
                    schedule_reconnection();
                }
            });
            binance_client->set_fail_handler([this](websocketpp::connection_hdl hdl) {
                if(reconnect_attempt + 1 > retry_limit) {
                    std::string message = "connection_end";
                    static_cast<Derived*>(this)->onClose(hdl, message);
                } else {
                    std::string message = "disconnect";
                    static_cast<Derived*>(this)->onClose(hdl, message);
                    schedule_reconnection();
                }
            });
            return;
        }
        // if (!ws_client) {
        //     LOG_INFRA_DEBUG("tls client already exists");
        //     return;
        // }
        ws_client->init_asio();
        ws_client->clear_access_channels(websocketpp::log::alevel::all);
        ws_client->clear_error_channels(websocketpp::log::elevel::all);
        ws_client->set_tls_init_handler([](websocketpp::connection_hdl) {
            auto ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
            try {
                ctx->set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 |
                                 boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::single_dh_use);
            } catch(std::exception& e) {
                LoggerSingleton::get().infra().error("error in tls initialization: ", e.what());
            }
            return ctx;
        });
        // Set handlers using CRTP to access derived class methods
        ws_client->set_message_handler([this](websocketpp::connection_hdl hdl, client_tls::message_ptr msg) {
            static_cast<Derived*>(this)->onMessage(hdl, msg);
        });
        ws_client->set_open_handler([this](websocketpp::connection_hdl hdl) {
            current_hdl = hdl;
            static_cast<Derived*>(this)->onOpen(hdl);
        });
        ws_client->set_close_handler([this](websocketpp::connection_hdl hdl) {
            if(reconnect_attempt + 1 > retry_limit) {
                std::string message = "connection_end";
                static_cast<Derived*>(this)->onClose(hdl, message);
            } else {
                std::string message = "disconnect";
                static_cast<Derived*>(this)->onClose(hdl, message);
                schedule_reconnection();
            }
        });
        ws_client->set_fail_handler([this](websocketpp::connection_hdl hdl) {
            if(reconnect_attempt + 1 > retry_limit) {
                std::string message = "connection_end";
                static_cast<Derived*>(this)->onClose(hdl, message);
            } else {
                std::string message = "disconnect";
                static_cast<Derived*>(this)->onClose(hdl, message);
                schedule_reconnection();
            }
        });
    }

    websocketpp::connection_hdl current_hdl;
    std::atomic<bool> cleaning_up{false};
    std::atomic<bool> shutdown_requested{false};
    std::unique_ptr<client_tls> ws_client;
    std::unique_ptr<client_non_tls> binance_client;
    bool tls = true;
    std::string uri;
    std::string proxy_uri;
    const uint32_t retry_limit = 0;
    uint32_t reconnect_attempt = 0;
};
