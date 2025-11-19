#include "MarketMaker.h"
#include <iostream>
#include <chrono>
#include <cmath>
#include <sstream>
#include <limits>

MarketMaker::MarketMaker(Config config) : _config(std::move(config)), _requests(_config.requests) {}

MarketMaker::~MarketMaker() { stop(); }

void MarketMaker::start() {
    if (_running.exchange(true)) return;
    _worker = std::thread([this](){ runLoop(); });
}

void MarketMaker::stop() {
    if (!_running.exchange(false)) return;
    _cv.notify_all();
    if (_worker.joinable()) _worker.join();
}

void MarketMaker::updateMarketDepth(const MarketDepth &depth) {
    {
        std::lock_guard<std::mutex> lk(_mtx);
        _lastDepth = depth;
        _hasDepth = true;
    }
    _cv.notify_one();
}

void MarketMaker::runLoop() {
    std::unique_lock<std::mutex> lk(_mtx);
    while (_running.load()) {
        // мб убрать
        _cv.wait(lk, [this] { return !_running.load() || _hasDepth; }); // ждём новый стакан
        if (!_running.load()) break;
        MarketDepth depth = _lastDepth;
        _hasDepth = false;
        lk.unlock();

        if (!hasGoodSpread(depth)) {
            lk.lock();
            continue;
        }
        // если позиции нет и не ждём заполнения, можно переписать на запросы, но эт медленно
        if (!_hasPosition.load() && !_waitingForBid.load()) {
            auto bidId = placeBidOrder(depth);
            std::cout << "закончили placeBidOrder" << std::endl;
            if (bidId) {
                _waitingForBid.store(true);
                std::cout << "начали waitForOrderExecution" << std::endl;
                float filledBuy = waitForOrderExecution("BUY", _config.orderSize);
                std::cout << filledBuy << "fieldbuy" << std::endl;
                if (std::abs(filledBuy - 0.0f) <= 0.000000001) { // 0 != 0 c++
                    _waitingForBid.store(false);
                    lk.lock();
                    continue;
                }
                _waitingForBid.store(false);
                _hasPosition.store(true);
                std::cout << "1" << std::endl;
                auto askId = placeAskOrder(depth, filledBuy);
                std::cout << "11" << std::endl;
                if (askId) {
                        _waitingForAsk.store(true);
                        std::cout << filledBuy <<  "I\n";
                        float filledSell = waitForOrderExecution("SELL", filledBuy);
                        std::cout <<  filledSell << "II\n";
                        if (filledSell == 0.0f) { // TODO: здесь надо наверно сравнивать с _config.orderSize
                            _waitingForAsk.store(false);
                            lk.lock();
                            continue;
                        }
                        _waitingForAsk.store(false);
                        _hasPosition.store(false);
                    }
            }
        }
        std::cout << "----------------" << std::endl;
        lk.lock();
    }
}

bool MarketMaker::hasGoodSpread(const MarketDepth &depth) const {
    if (depth.bids.empty() || depth.asks.empty()) return false;
    const float bid = depth.bids.front().first;
    const float ask = depth.asks.front().first;
    if (bid <= 0.0f || ask <= 0.0f || ask <= bid) return false;
    const float mid = (bid + ask) * 0.5f;
    const float spreadPct = ((ask - bid) / mid) * 100.0f;
    return spreadPct >= _config.minSpreadPct;
}

std::optional<std::string> MarketMaker::placeBidOrder(const MarketDepth &depth) {
    const float bestBid = depth.bids.empty() ? 0.0f : depth.bids.front().first;
    const float bidPrice = bestBid + _config.tickSize;
    try {
        if (_requests) {
            std::ostringstream qty;
            qty << (_config.orderSize);
            double px = static_cast<double>(bidPrice);
            std::string resp = _requests->createOrder(
                    _config.symbol,
                    "BUY",
                    "LIMIT", // todo это не учитывается
                    qty.str(),
                    px
            );
            {
                //
                std::lock_guard<std::mutex> lk(_ordersMtx);
                _currentOrder.reset();
                _lastSubmittedPrice = px; // фиксируем последнюю отправленную цену
            }
            return std::make_optional<std::string>(resp);
        }
    } catch (const std::exception &ex) {
        std::cerr << "createOrder BID error: " << ex.what() << "\n";
    }
    return std::nullopt;
}

std::optional<std::string> MarketMaker::placeAskOrder(const MarketDepth &depth, float quantity) {
    const float bestAsk = depth.asks.empty() ? 0.0f : depth.asks.front().first;
    const float askPrice = bestAsk - _config.tickSize;
    std::cout << quantity << "sell qu" << std::endl;
    try {
        if (_requests) {
            std::ostringstream qty;
            qty << quantity;
            double px = static_cast<double>(askPrice);
            std::cout << px << " SELLLLLLLLLLLLLLLLL"<< std::endl;
            std::string resp = _requests->createOrder(
                    _config.symbol,
                    "SELL",
                    "LIMIT", // todo это не учитывается
                    qty.str(),
                    px
            );
            {
                std::lock_guard<std::mutex> lk(_ordersMtx);
                _currentOrder.reset();
                _lastSubmittedPrice = px; // фиксируем последнюю отправленную цену
            }
            return std::make_optional<std::string>(resp);
        }
    } catch (const std::exception &ex) {
        std::cerr << "createOrder ASK error: " << ex.what() << "\n";
    }
    return std::nullopt;
}

float MarketMaker::waitForOrderExecution(std::string side, float orderBaseQuantity) {
    float filledVolume = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    bool continueTrading = true;
    while (continueTrading) { // можно улучшить чтобы продажа безх запроса времени была

        // Проверка статуса без блокировки на длительное время
        // todo попробовать избавиться
        std::optional<OrderLite> cur;
        {
            std::lock_guard<std::mutex> lk(_ordersMtx);
            cur = _currentOrder;
        }

        if (cur && cur->status == "filled") {
            filledVolume = _config.orderSize - std::stof(cur->remaining_base_amount);
            std::cout << filledVolume << " 3" << std::endl;
            return filledVolume;
        }

        // Дождаться СЛЕДУЮЩЕГО обновления стакана и только затем пересчитать цену
        MarketDepth depthSnapshot;
        {
            std::unique_lock<std::mutex> lk(_mtx);
            _hasDepth = false; // ждём именно новое обновление
            auto deadlineDepth = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
            _cv.wait_until(lk, deadlineDepth, [this]{ return !_running.load() || _hasDepth; });
            if (!_running.load()) {
                if (cur) {
                    filledVolume = _config.orderSize - std::stof(cur->remaining_base_amount);
                } else {
                    filledVolume = 0.0f;
                }
                std::cout << filledVolume << " 33" << std::endl;
                return filledVolume;
            }
            depthSnapshot = _lastDepth;
            _hasDepth = false;
        }
        float best = 0.0f;
        float second = 0.0f;
        if (side == "BUY") {
            best = depthSnapshot.bids.empty() ? 0.0f : depthSnapshot.bids.front().first;
            second = (depthSnapshot.bids.size() >= 2) ? depthSnapshot.bids[1].first : 0.0f;
        } else {
            best = depthSnapshot.asks.empty() ? 0.0f : depthSnapshot.asks.front().first;
            second = (depthSnapshot.asks.size() >= 2) ? depthSnapshot.asks[1].first : 0.0f;
        }

        // База для нового цены: если топ-1 — наш, используем топ-2; иначе — топ-1
        float base = best;
        const double eps = std::max(1e-9, (double)_config.tickSize);

        if (_lastSubmittedPrice.has_value() && std::fabs(_lastSubmittedPrice.value() - best) <= _config.tickSize) {
            if (second > 0.0f) base = second; // топ-1 наш — используем топ-2
        }
        double newPrice = (side == "BUY") ? ((double)base + _config.tickSize)
                                            : ((double)base - _config.tickSize);

        this->cutPriceIfBadSpread(hasGoodSpread(depthSnapshot), side, newPrice);

        // Если новая цена совпадает с уже отправленной — ничего не делаем
        if (_lastSubmittedPrice.has_value() && std::fabs(_lastSubmittedPrice.value() - newPrice) <= eps) {
            //std::cout << "CONTINUE -----------------------------------" << std::endl;
            if (side == "SELL") {
                continueTrading = true;
            } else {
                continueTrading = std::chrono::steady_clock::now() < deadline;
                if (cur) {
                    filledVolume = _config.orderSize - std::stof(cur->remaining_base_amount);
                }
            }
            continue; // ждём обновления книги/ордера
        }

        std::ostringstream qty;
        qty << orderBaseQuantity;

        if (cur && _requests) {
            // 1) Если статус уже filled/cancelled — прекращаем
            long long orderIndex = 0;
            try { orderIndex = std::stoll(cur->order_id); } catch (...) { /* skip */ }
            if (orderIndex != 0) {
                try {
                    //std::cout << newPrice << std::endl;
                    //std::this_thread::sleep_for(std::chrono::milliseconds(5)); // задержка 50мс перед модификацией
                    (void)_requests->modifyOrder(_config.symbol, qty.str(), newPrice, orderIndex, side, hasGoodSpread(depthSnapshot));
                    //std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    _lastSubmittedPrice = newPrice; // обновляем локально целью
                } catch (const std::exception &ex) {
                    std::cerr << "modifyOrder error: " << ex.what() << "\n";
                }
            }
        }
        if (side == "SELL") {
            continueTrading = true;
        } else {
            continueTrading = std::chrono::steady_clock::now() < deadline;
            if (!continueTrading) {
                if (cur) {
                    filledVolume = _config.orderSize - std::stof(cur->remaining_base_amount);
                } else {
                    filledVolume = 0.0f;
                }
                return filledVolume;
            }
        }
    }
    std::cout << filledVolume << " 333" << std::endl;

    return filledVolume;
}

void MarketMaker::cutPriceIfBadSpread(bool hasGoodSpread, const std::string &side, double &acceptablePriceInt) {
    if (!hasGoodSpread && side == "BUY") // если плохой спред
    {
        acceptablePriceInt *= 0.9;
    }
}

void MarketMaker::updateOrder(const AccountAllOrdersWS::Order &o) {
    OrderLite L;
    L.order_index = o.order_index;
    L.order_id = o.order_id;
    L.client_order_id = o.client_order_id;
    L.side = o.side;
    L.type = o.type;
    L.status = o.status;
    L.price = o.price;
    L.remaining_base_amount = o.remaining_base_amount;
    L.timestamp = o.timestamp;
    {
        std::lock_guard<std::mutex> lk(_ordersMtx);
        _currentOrder = std::move(L);
    }
    _ordersCv.notify_all();
}
