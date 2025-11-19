#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <functional>
#include "MarketDepth.h"
#include "WsClient.h"

class LighterOrderBookWS {
public:
    struct Config {
        std::string url;
        std::string subscribeJson;
        std::vector<std::string> extraHeaders;
        std::string symbol;
        int depthLimit = 50;
        std::function<void(const std::string&)> onMessage; // колбэк для сырых сообщений
		std::function<void(const MarketDepth&, long long)> onDepthUpdated; // вызывается после каждого обновления стакана (depth, offset)
    };

    explicit LighterOrderBookWS(Config cfg);
    ~LighterOrderBookWS();

    void start();
    void stop();

    MarketDepth getSnapshot() const;

private:
    void run();
    void parseAndUpdate(const std::string &jsonText);

    static std::vector<std::pair<float, float>> parseOrdersArray(const std::string &json, const std::string &key);

    Config _cfg;
    mutable std::mutex _mtx;
    MarketDepth _depth;
    std::thread _thr;
    std::atomic<bool> _running{false};

    // Состояние синхронизации актуального стакана
    bool _hasSnapshot{false};
    long long _lastOffset{-1};

    // Внутренний клиент WebSocket
    WsClient *_ws{nullptr};
};


