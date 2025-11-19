#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <optional>

#include "AccountAllOrdersWS.h"
#include "MarketDepths/MarketDepth.h"
#include "requests/lighter/LighterRequests.h"
#include "MarketDepths/AccountAllOrdersWS.h"

class MarketMaker {
public:
    struct Config {
        std::string symbol;     // market_index
        float minSpreadPct;     // минимальный спред в % для начала торговли
        float orderSize;        // размер ордера (в базовой валюте)
        float tickSize;         // размер тика (абсолютный шаг цены)
        std::shared_ptr<LighterRequests> requests; // клиент для отправки ордеров
    };

    explicit MarketMaker(Config config);
    ~MarketMaker();

    void start();
    void stop();

    // Принимаем свежий снимок стакана
    void updateMarketDepth(const MarketDepth &depth);

private:
    void runLoop();
    bool hasGoodSpread(const MarketDepth &depth) const;

    // Выставление заявок (пока заглушка с логированием и фиктивным id)
    std::optional<std::string> placeBidOrder(const MarketDepth &depth);
    std::optional<std::string> placeAskOrder(const MarketDepth &depth, float quantity);
    float waitForOrderExecution(std::string side, float orderBaseQuantity);
    void cutPriceIfBadSpread(bool hasGoodSpread, const std::string &side, double &acceptablePriceInt);
    
public:
    // Обновление одной сделки
    void updateOrder(const AccountAllOrdersWS::Order &order);

private:
    Config _config;
    std::thread _worker;
    std::atomic<bool> _running{false};

    mutable std::mutex _mtx;
    std::condition_variable _cv;
    MarketDepth _lastDepth;
    bool _hasDepth{false};

    // Состояние сделки
    std::atomic<bool> _hasPosition{false};
    std::atomic<bool> _waitingForBid{false};
    std::atomic<bool> _waitingForAsk{false};
    std::shared_ptr<LighterRequests> _requests;

    // Отслеживание статуса ордеров
    struct OrderLite {
        long long order_index{0};
        std::string order_id;
        std::string client_order_id;
        std::string side;
        std::string type;
        std::string status;
        std::string price;
        std::string remaining_base_amount;
        long long timestamp{0};
    };
    mutable std::mutex _ordersMtx;
    std::condition_variable _ordersCv;
    std::optional<OrderLite> _currentOrder; // одна активная сделка

    // Последняя отправленная нами цена активного ордера
    std::optional<double> _lastSubmittedPrice;
};


