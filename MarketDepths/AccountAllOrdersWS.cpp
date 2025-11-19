#include "AccountAllOrdersWS.h"
#include <iostream>
#include <sstream>

static std::string escape(const std::string &s){ return s; }

std::string AccountAllOrdersWS::buildSubscribe(const std::string &accountId, const std::string &auth) {
    std::ostringstream os;
    os << "{\"type\":\"subscribe\",\"channel\":\"account_all_orders/" << accountId
       << "\",\"auth\":\"" << escape(auth) << "\"}";
    return os.str();
}

AccountAllOrdersWS::AccountAllOrdersWS(Config cfg) : _cfg(std::move(cfg)) {}
AccountAllOrdersWS::~AccountAllOrdersWS() { stop(); }

void AccountAllOrdersWS::start() {
    if (_running.exchange(true)) return;
    _thr = std::thread([this](){ run(); });
}

void AccountAllOrdersWS::stop() {
    if (!_running.exchange(false)) return;
    if (_thr.joinable()) _thr.join();
}

std::unordered_map<int, std::vector<AccountAllOrdersWS::Order>> AccountAllOrdersWS::getOrders() const {
    std::lock_guard<std::mutex> lk(_mtx);
    return _ordersByMarket;
}

static std::string extractBlock(const std::string &src, const std::string &key, char open, char close) {
    std::string marker = '"' + key + '"';
    size_t p = src.find(marker);
    if (p == std::string::npos) return {};
    p = src.find(open, p);
    if (p == std::string::npos) return {};
    int d = 0;
    for (size_t i = p; i < src.size(); ++i) {
        if (src[i] == open) d++;
        else if (src[i] == close) { d--; if (d == 0) return src.substr(p, i - p + 1); }
    }
    return {};
}

// парсер
void AccountAllOrdersWS::handleMessage(const std::string &json) {
    if (json.find("\"type\":\"update/account_all_orders\"") == std::string::npos) return;
    std::unordered_map<int, std::vector<Order>> newMap;
    std::string ordersObj = extractBlock(json, "orders", '{', '}');
    size_t cur = 0;
    while (true) {
        size_t keyQ1 = ordersObj.find('"', cur);
        if (keyQ1 == std::string::npos) break;
        size_t keyQ2 = ordersObj.find('"', keyQ1 + 1);
        if (keyQ2 == std::string::npos) break;
        std::string keyStr = ordersObj.substr(keyQ1 + 1, keyQ2 - keyQ1 - 1);
        int market = std::atoi(keyStr.c_str());
        size_t arrStart = ordersObj.find('[', keyQ2);
        if (arrStart == std::string::npos) break;
        int d = 0; size_t arrEnd = std::string::npos;
        for (size_t i = arrStart; i < ordersObj.size(); ++i) {
            if (ordersObj[i] == '[') d++;
            else if (ordersObj[i] == ']') { d--; if (d == 0) { arrEnd = i; break; } }
        }
        if (arrEnd == std::string::npos) break;
        std::string arr = ordersObj.substr(arrStart, arrEnd - arrStart + 1);
        size_t p = 0; std::vector<Order> list;
        while (true) {
            size_t oStart = arr.find('{', p);
            if (oStart == std::string::npos) break;
            int dd = 0; size_t oEnd = std::string::npos;
            for (size_t i = oStart; i < arr.size(); ++i) {
                if (arr[i] == '{') dd++; else if (arr[i] == '}') { dd--; if (dd == 0) { oEnd = i; break; } }
            }
            if (oEnd == std::string::npos) break;
            std::string o = arr.substr(oStart, oEnd - oStart + 1);

            auto getString = [&](const char* key)->std::string{
                std::string mk = std::string("\"") + key + "\"";
                size_t p = o.find(mk); if (p == std::string::npos) return {};
                p = o.find(':', p); if (p == std::string::npos) return {};
                size_t q1 = o.find('"', p); if (q1 == std::string::npos) return {};
                size_t q2 = o.find('"', q1 + 1); if (q2 == std::string::npos) return {};
                return o.substr(q1 + 1, q2 - q1 - 1);
            };
            auto getInt = [&](const char* key)->long long{
                std::string mk = std::string("\"") + key + "\"";
                size_t p = o.find(mk); if (p == std::string::npos) return 0;
                p = o.find(':', p); if (p == std::string::npos) return 0;
                ++p; while (p < o.size() && (o[p]==' '||o[p]=='\t')) ++p;
                return std::strtoll(o.c_str()+p, nullptr, 10);
            };
            auto getBool = [&](const char* key)->bool{
                std::string mk = std::string("\"") + key + "\"";
                size_t p = o.find(mk); if (p == std::string::npos) return false;
                p = o.find(':', p); if (p == std::string::npos) return false;
                ++p; while (p < o.size() && (o[p]==' '||o[p]=='\t')) ++p;
                return o.compare(p, 4, "true") == 0;
            };

            Order ord;
            ord.order_index = getInt("order_index");
            ord.client_order_index = getInt("client_order_index");
            ord.order_id = getString("order_id");
            ord.client_order_id = getString("client_order_id");
            ord.market_index = (int)getInt("market_index");
            ord.owner_account_index = (int)getInt("owner_account_index");
            ord.initial_base_amount = getString("initial_base_amount");
            ord.price = getString("price");
            ord.nonce = getInt("nonce");
            ord.remaining_base_amount = getString("remaining_base_amount");
            ord.is_ask = getBool("is_ask");
            ord.filled_base_amount = getString("filled_base_amount");
            ord.filled_quote_amount = getString("filled_quote_amount");
            ord.side = getString("side");
            ord.type = getString("type");
            ord.time_in_force = getString("time_in_force");
            ord.reduce_only = getBool("reduce_only");
            ord.trigger_price = getString("trigger_price");
            ord.order_expiry = getInt("order_expiry");
            ord.status = getString("status");
            ord.trigger_status = getString("trigger_status");
            ord.trigger_time = getInt("trigger_time");
            ord.parent_order_index = getInt("parent_order_index");
            ord.parent_order_id = getString("parent_order_id");
            ord.to_trigger_order_id_0 = getString("to_trigger_order_id_0");
            ord.to_trigger_order_id_1 = getString("to_trigger_order_id_1");
            ord.to_cancel_order_id_0 = getString("to_cancel_order_id_0");
            ord.block_height = getInt("block_height");
            ord.timestamp = getInt("timestamp");

            list.push_back(std::move(ord));
            p = oEnd + 1;
        }
        newMap[market] = std::move(list);
        cur = arrEnd + 1;
    }

    {
        std::lock_guard<std::mutex> lk(_mtx);
        _ordersByMarket = std::move(newMap);
    }
    if (_cfg.onOrdersUpdated) _cfg.onOrdersUpdated(getOrders());
}

void AccountAllOrdersWS::run() {
    WsClient::Config wcfg;
    wcfg.url = _cfg.url;
    wcfg.extraHeaders = _cfg.extraHeaders;
    if (!_cfg.authToken.empty()) wcfg.extraHeaders.emplace_back(std::string("Authorization: Bearer ") + _cfg.authToken);
    wcfg.initialText = buildSubscribe(_cfg.accountId, _cfg.authToken);
    wcfg.onMessage = [this](const std::string &data){ handleMessage(data); };
    std::cout << "[AccountAllOrdersWS] starting: " << wcfg.url << " account=" << _cfg.accountId << std::endl;
    WsClient ws(wcfg);
    ws.start();
    // Блокируем поток до stop(), без активных задержек
    while (_running.load()) {
        std::unique_lock<std::mutex> lk(_mtx);
        if (!_running.load()) break;
        lk.unlock();
        std::this_thread::yield();
    }
    std::cout << "[AccountAllOrdersWS] stopped" << std::endl;
}


