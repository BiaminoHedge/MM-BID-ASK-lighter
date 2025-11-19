#include "LighterOrderBookWS.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <iostream>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;


static void sortDepthWS(std::vector<std::pair<float, float>> &bids,
                        std::vector<std::pair<float, float>> &asks) {
    std::sort(bids.begin(), bids.end(), [](const auto &l, const auto &r) { return l.first > r.first; });
    std::sort(asks.begin(), asks.end(), [](const auto &l, const auto &r) { return l.first < r.first; });
}

// Применить инкрементальные изменения к списку уровней
static void applyEditsWS(std::vector<std::pair<float, float>> &levels,
                         const std::vector<std::pair<float, float>> &edits) {
    // допускаем сравнение цен в пределах фиксированного грида (≈1e-5)
    const float gridTol = 1e-5f;
    auto findLevel = [&](float p) {
        return std::find_if(levels.begin(), levels.end(), [&](const auto &lvl){
            return std::fabs(lvl.first - p) <= gridTol;
        });
    };
    for (const auto &e : edits) {
        float price = e.first;
        float size = e.second;
        auto it = findLevel(price);
        if (size <= 0.0f) {
            if (it != levels.end()) levels.erase(it);
        } else {
            if (it != levels.end()) it->second = size; else levels.emplace_back(price, size);
        }
    }
}

// извлекаем объект из json по ключу
static std::string extractObjectByKey(const std::string &json, const std::string &key) {
    const std::string marker = '"' + key + '"';
    size_t k = json.find(marker);
    if (k == std::string::npos) return {};
    size_t b = json.find('{', k);
    if (b == std::string::npos) return {};
    int d = 0;
    for (size_t i = b; i < json.size(); ++i) {
        char c = json[i];
        if (c == '{') d++;
        else if (c == '}') {
            d--;
            if (d == 0) return json.substr(b, i - b + 1);
        }
    }
    return {};
}

LighterOrderBookWS::LighterOrderBookWS(Config cfg) : _cfg(std::move(cfg)) {}
LighterOrderBookWS::~LighterOrderBookWS() { stop(); }

void LighterOrderBookWS::start() {
    if (_running.exchange(true)) return;
    _thr = std::thread([this]() { run(); });
}

void LighterOrderBookWS::stop() {
    if (!_running.exchange(false)) return;
    if (_thr.joinable()) _thr.join();
}

MarketDepth LighterOrderBookWS::getSnapshot() const {
    std::lock_guard<std::mutex> lk(_mtx);
    return _depth;
}

std::vector<std::pair<float, float>> LighterOrderBookWS::parseOrdersArray(const std::string &json, const std::string &key) {
    std::vector<std::pair<float, float>> result;
    std::string scope = extractObjectByKey(json, "order_book");
    const std::string &src = scope.empty() ? json : scope;
    const std::string marker = '"' + key + '"';
    size_t mpos = src.find(marker);
    if (mpos == std::string::npos) return result;
    size_t apos = src.find('[', mpos);
    if (apos == std::string::npos) return result;
    int depth = 0;
    size_t aend = std::string::npos;
    for (size_t i = apos; i < src.size(); ++i) {
        char c = src[i];
        if (c == '[') depth++;
        else if (c == ']') {
            depth--;
            if (depth == 0) { aend = i; break; }
        }
    }
    if (aend == std::string::npos) return result;

    size_t cur = apos;
    while (true) {
        size_t objStart = src.find('{', cur);
        if (objStart == std::string::npos || objStart > aend) break;
        int d = 0; size_t objEnd = std::string::npos;
        for (size_t j = objStart; j <= aend; ++j) {
            char c = src[j];
            if (c == '{') d++;
            else if (c == '}') {
                d--;
                if (d == 0) { objEnd = j; break; }
            }
        }
        if (objEnd == std::string::npos) break;

        const std::string obj = src.substr(objStart, objEnd - objStart + 1);
        auto findStringField = [&](const std::string &fname) -> std::optional<std::string> {
            const std::string fmark = '"' + fname + '"';
            size_t p = obj.find(fmark);
            if (p == std::string::npos) return std::nullopt;
            p = obj.find(':', p);
            if (p == std::string::npos) return std::nullopt;
            size_t q1 = obj.find('"', p);
            if (q1 == std::string::npos) return std::nullopt;
            size_t q2 = obj.find('"', q1 + 1);
            if (q2 == std::string::npos) return std::nullopt;
            return obj.substr(q1 + 1, q2 - q1 - 1);
        };

        std::optional<std::string> priceStr = findStringField("price");
        // size <= 0 означает удаление уровня.
        std::optional<std::string> amtStr = findStringField("size");
        if (priceStr && amtStr) {
            char *ep1 = nullptr;
            char *ep2 = nullptr;
            float price = (float)std::strtod(priceStr->c_str(), &ep1);
            float amount = (float)std::strtod(amtStr->c_str(), &ep2);
            // Добавляем запись даже при amount <= 0, чтобы апдейтер смог удалить уровень.
            if (price > 0.0f) result.emplace_back(price, amount);
        }
        cur = objEnd + 1;
    }
    return result;
}

static std::optional<long long> extractOffset(const std::string &json) {
    const std::string key = "\"offset\"";
    size_t p = json.find(key);
    if (p == std::string::npos) return std::nullopt;
    p = json.find(':', p);
    if (p == std::string::npos) return std::nullopt;
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    char *ep = nullptr;
    long long v = std::strtoll(json.c_str() + p, &ep, 10);
    if (ep == json.c_str() + p) return std::nullopt;
    return v;
}

void LighterOrderBookWS::parseAndUpdate(const std::string &jsonText) {
    auto off = extractOffset(jsonText);
    const bool snapshot = jsonText.find("\"type\":\"snapshot/order_book\"") != std::string::npos;

    if (snapshot) {
        MarketDepth md;
        md.bids = parseOrdersArray(jsonText, "bids");
        md.asks = parseOrdersArray(jsonText, "asks");
        sortDepthWS(md.bids, md.asks);
        {
            std::lock_guard<std::mutex> lk(_mtx);
            _depth = std::move(md);
            _hasSnapshot = true;
            _lastOffset = off.value_or(_lastOffset);
        }
        if (_cfg.onDepthUpdated) _cfg.onDepthUpdated(getSnapshot(), _lastOffset);
        return;
    }

    if (!off.has_value()) return; // без offset безопаснее не применять
    if (_lastOffset >= 0 && off.value() != _lastOffset + 1) {
        // offset gap — очищаем книгу и ждём ресинк
        // todo сделать переподключение, пока геп не ловил, поэтому не сделал
        {
            std::lock_guard<std::mutex> lk(_mtx);
            _depth.bids.clear();
            _depth.asks.clear();
        }
        _hasSnapshot = false; // ждём новый снимок
        return;
    }
    _lastOffset = off.value();

    // Применяем инкрементальные изменения: size<=0 удаляет уровень, иначе обновляет/добавляет
    std::vector<std::pair<float, float>> deltaBids = parseOrdersArray(jsonText, "bids");
    std::vector<std::pair<float, float>> deltaAsks = parseOrdersArray(jsonText, "asks");

    {
        std::lock_guard<std::mutex> lk(_mtx);
        applyEditsWS(_depth.bids, deltaBids);
        applyEditsWS(_depth.asks, deltaAsks);
        sortDepthWS(_depth.bids, _depth.asks);
        if (_cfg.depthLimit > 0) {
            if ((int)_depth.bids.size() > _cfg.depthLimit) _depth.bids.resize(_cfg.depthLimit);
            if ((int)_depth.asks.size() > _cfg.depthLimit) _depth.asks.resize(_cfg.depthLimit);
        }
    }
    if (_cfg.onDepthUpdated) _cfg.onDepthUpdated(getSnapshot(), _lastOffset);
}


void LighterOrderBookWS::run() {
    WsClient::Config wcfg;
    wcfg.url = _cfg.url;
    wcfg.extraHeaders = _cfg.extraHeaders;
    wcfg.initialText = _cfg.subscribeJson;
    wcfg.onMessage = [this](const std::string &data){
        parseAndUpdate(data);
    };
    std::cout << "[OrderBookWS] starting: " << wcfg.url << std::endl;
    WsClient ws(wcfg);
    _ws = &ws;
    ws.start();
    // Блокируем поток до stop()
    while (_running.load()) {
        std::unique_lock<std::mutex> lk(_mtx);
        // просыпаемся только при остановке; никаких активных слипов
        // используем условие: _running меняется в stop()
        if (!_running.load()) break;
        // чтобы не занять мьютекс надолго, отпустим его сразу
        lk.unlock();
        std::this_thread::yield();
    }
    std::cout << "[OrderBookWS] stopped" << std::endl;
}


