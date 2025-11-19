#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>

class WsClient {
public:
    struct Config {
        std::string url;                       // wss://host[:port]/path
        std::vector<std::string> extraHeaders;
        std::function<void(const std::string&)> onMessage; // callback для текстовых сообщений
        std::string initialText;
    };

    explicit WsClient(Config cfg);
    ~WsClient();

    void start();
    void stop();
private:
    void run();

    Config _cfg;
    std::thread _thr;
    std::atomic<bool> _running{false};
};


