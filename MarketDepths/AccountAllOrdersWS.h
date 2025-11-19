#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <vector>

#include "WsClient.h"

// Поддержка канала account_all_orders/{ACCOUNT_ID}
class AccountAllOrdersWS {
public:
    struct Order {
        long long order_index{0};
        long long client_order_index{0};
        std::string order_id;
        std::string client_order_id;
        int market_index{0};
        int owner_account_index{0};
        std::string initial_base_amount;
        std::string price;
        long long nonce{0};
        std::string remaining_base_amount;
        bool is_ask{false};
        int base_size{0};
        int base_price{0};
        std::string filled_base_amount;
        std::string filled_quote_amount;
        std::string side;
        std::string type;
        std::string time_in_force;
        bool reduce_only{false};
        std::string trigger_price;
        long long order_expiry{0};
        std::string status;
        std::string trigger_status;
        long long trigger_time{0};
        long long parent_order_index{0};
        std::string parent_order_id;
        std::string to_trigger_order_id_0;
        std::string to_trigger_order_id_1;
        std::string to_cancel_order_id_0;
        long long block_height{0};
        long long timestamp{0};
    };

    struct Config {
        std::string url;
        std::string accountId;
        std::string authToken;
        std::vector<std::string> extraHeaders;
        std::function<void(const std::unordered_map<int, std::vector<Order>>&)> onOrdersUpdated;
    };

    explicit AccountAllOrdersWS(Config cfg);
    ~AccountAllOrdersWS();

    void start();
    void stop();

    std::unordered_map<int, std::vector<Order>> getOrders() const;

private:
    void run();
    void handleMessage(const std::string &json);
    static std::string buildSubscribe(const std::string &accountId, const std::string &auth);

    Config _cfg;
    std::thread _thr;
    std::atomic<bool> _running{false};

    mutable std::mutex _mtx;
    std::unordered_map<int, std::vector<Order>> _ordersByMarket;
};


