#include "LighterRequests.h"

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <cstdlib>
#include <iostream>
#include <cmath>

LighterRequests::LighterRequests()
    : _baseUrl("https://mainnet.zklighter.elliot.ai"),
      _orderBookPath("/api/v1/orderBookOrders"),
      _sendTxPath("/api/v1/sendTx") {
}

LighterRequests::LighterRequests(const std::string &baseUrl)
    : _baseUrl(baseUrl),
      _orderBookPath("/api/v1/orderBookOrders"),
      _sendTxPath("/api/v1/sendTx") {
}

void LighterRequests::setBaseUrl(const std::string &url) { _baseUrl = url; }
const std::string &LighterRequests::getBaseUrl() const { return _baseUrl; }

void LighterRequests::setAuthToken(const std::string &token) { _authToken = token; }
void LighterRequests::setOrderBookPath(const std::string &path) { _orderBookPath = path; }
void LighterRequests::setSendTxPath(const std::string &path) { _sendTxPath = path; }
void LighterRequests::setSignedTx(const std::string &signedTxPayload) { _signedTx = signedTxPayload; }

void LighterRequests::setSignerConfig(const std::string &dllPath,
                                      const std::string &apiKeyPrivateHex,
                                      int chainId,
                                      int apiKeyIndex,
                                      long long accountIndex,
                                      long long baseAmountScale,
                                      int priceScale) {
    _signerDllPath = dllPath;
    _apiKeyPrivate = apiKeyPrivateHex;
    _chainId = chainId;
    _apiKeyIndex = apiKeyIndex;
    _accountIndex = accountIndex;
    _baseAmountScale = baseAmountScale;
    _priceScale = priceScale;
}

void LighterRequests::setMarketIndex(int marketIndex) { _marketIndex = marketIndex; }

// Парсер стакана
static std::vector<std::pair<float, float> > parseOrdersArray(const std::string &json, const std::string &key) {
    std::vector<std::pair<float, float> > result;
    const std::string marker = '"' + key + '"';
    size_t mpos = json.find(marker);
    if (mpos == std::string::npos) return result;
    size_t apos = json.find('[', mpos);
    if (apos == std::string::npos) return result;
    int depth = 0;
    size_t aend = std::string::npos;
    for (size_t i = apos; i < json.size(); ++i) {
        char c = json[i];
        if (c == '[') depth++;
        else if (c == ']') {
            depth--;
            if (depth == 0) {
                aend = i;
                break;
            }
        }
    }
    if (aend == std::string::npos) return result;

    // Итерируем по объектам внутри массива
    size_t cur = apos;
    while (true) {
        size_t objStart = json.find('{', cur);
        if (objStart == std::string::npos || objStart > aend) break;
        // найти соответствующую '}'
        int d = 0;
        size_t objEnd = std::string::npos;
        for (size_t j = objStart; j <= aend; ++j) {
            char c = json[j];
            if (c == '{') d++;
            else if (c == '}') {
                d--;
                if (d == 0) {
                    objEnd = j;
                    break;
                }
            }
        }
        if (objEnd == std::string::npos) break;

        // Парсим поля внутри объекта
        const std::string obj = json.substr(objStart, objEnd - objStart + 1);
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
        // Используем доступную ликвидность
        std::optional<std::string> amtStr = findStringField("remaining_base_amount");
        if (priceStr && amtStr) {
            char *ep = nullptr;
            float price = (float) std::strtod(priceStr->c_str(), &ep);
            float amount = (float) std::strtod(amtStr->c_str(), &ep);
            if (price > 0.0f && amount > 0.0f) result.emplace_back(price, amount);
        }

        cur = objEnd + 1;
    }
    return result;
}

MarketDepth LighterRequests::parseMarketDepthJson(const std::string &json) {
    MarketDepth depth;
    depth.bids = parseOrdersArray(json, "bids");
    depth.asks = parseOrdersArray(json, "asks");

    std::sort(depth.bids.begin(), depth.bids.end(), [](const auto &l, const auto &r) { return l.first > r.first; });
    std::sort(depth.asks.begin(), depth.asks.end(), [](const auto &l, const auto &r) { return l.first < r.first; });
    return depth;
}

std::string LighterRequests::fetchMarketDepthRaw(const std::string &symbol, int limit) {
    std::ostringstream path;
    path << _orderBookPath << "?market_id=" << symbol << "&limit=" << limit;
    return HttpClient::httpGet(_baseUrl, path.str());
}

// next_nonce: упрощённый REST-запрос и парсер по ключу "nonce"
static std::optional<long long> parseNonceFromJson(const std::string &json) {
    const std::string key = "\"nonce\"";
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

long long LighterRequests::fetchNextNonce() {
    // По API lighter: GET /api/v1/nextNonce?account_index=<>&api_key_index=<>
    std::ostringstream path;
    path << "/api/v1/nextNonce?account_index=" << _accountIndex << "&api_key_index=" << _apiKeyIndex;

    std::vector<std::string> headers;
    const std::string token = _authToken ? _authToken.value() : std::string(std::getenv("LIGHTER_AUTH_TOKEN"));
    if (!token.empty()) headers.emplace_back(std::string("Authorization: Bearer ") + token);

    // httpGet у нас принимает pathWithQuery
    std::string resp = HttpClient::httpGet(_baseUrl, path.str());
    auto n = parseNonceFromJson(resp);
    if (!n.has_value()) throw std::runtime_error("failed to parse nextNonce response: " + resp);
    return *n;
}

long long LighterRequests::acquireNextNonce() {
    std::lock_guard<std::mutex> lk(_nonceMtx);
    if (!_nonceInitialized) {
        _nextNonceCached = fetchNextNonce();
        _nonceInitialized = true;
    }
    long long current = _nextNonceCached;
    ++_nextNonceCached;
    return current;
}

int LighterRequests::getAcceptablePriceInt(const std::optional<double> &price,
                                           const std::string &symbol, double qtyBase, const std::string &side) {
    double acceptablePriceFloat = 1.5;
    if (price.has_value()) {
        acceptablePriceFloat = price.value();
    } else {
        // Получим стакан и посчитаем среднюю цену исполнения для указанного объёма
        const int depthLimit = 50;
        MarketDepth depth = fetchMarketDepth(symbol, depthLimit);
        float avgExec = 0.0f;
        if (side == "SELL") {
            // Продаём в бид — усреднённая цена для объёма
            avgExec = depth.GetBestBidPriceFor((float) qtyBase);
            if (avgExec <= 0.0f) throw std::runtime_error("Недостаточно ликвидности в стакане для SELL объёма");
            acceptablePriceFloat = (double) avgExec * (1.0 - _defaultSlippage);
        } else {
            // Покупаем из аск — усреднённая цена для объёма
            avgExec = depth.GetBestAskPriceFor((float) qtyBase);
            if (avgExec <= 0.0f) throw std::runtime_error("Недостаточно ликвидности в стакане для BUY объёма");
            acceptablePriceFloat = (double) avgExec * (1.0 + _defaultSlippage);
        }
    }
    return static_cast<int>(llround(acceptablePriceFloat * (double) _priceScale));
}


MarketDepth LighterRequests::fetchMarketDepth(const std::string &symbol, int limit) {
    std::string raw = fetchMarketDepthRaw(symbol, limit);
    return parseMarketDepthJson(raw);
}

void LighterRequests::ensureTxWs() {
    if (_txWs) return;
    std::string wssUrl = _baseUrl;
    if (wssUrl.rfind("https://", 0) == 0) {
        wssUrl.replace(0, 5, "wss");
    }
    if (!wssUrl.empty() && wssUrl.back() == '/') wssUrl.pop_back();
    wssUrl += "/stream";

    std::vector<std::string> headers;
    const std::string token = _authToken ? _authToken.value() : std::string(std::getenv("LIGHTER_AUTH_TOKEN"));
    if (!token.empty()) headers.emplace_back(std::string("Authorization: Bearer ") + token);

    LighterTxWS::Config cfg;
    cfg.url = wssUrl;
    cfg.extraHeaders = headers;
    cfg.onMessage = [](const std::string &msg) {
        // Логируем все ответы от сокета Lighter
        std::cout << "[LighterTxWS][recv] " << msg << std::endl;
    };
    _txWs = std::make_unique<LighterTxWS>(cfg);
    _txWs->start();
}

std::string LighterRequests::sendTxOverWs(int txType, const std::string &txInfoJson) {
    ensureTxWs();
    std::ostringstream id;
    id << "mm_" << std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream os;
    os << "{\"type\":\"jsonapi/sendtx\",\"data\":{\"id\":\"" << id.str() <<
            "\",\"tx_type\":" << txType << ",\"tx_info\":" << txInfoJson << "}}";
    _txWs->sendText(os.str());
    return id.str();
}

std::string LighterRequests::createOrder(
    const std::string &symbol,
    const std::string &side,
    const std::string &type,
    std::string quantity,
    const std::optional<double> &price) {
    (void) symbol;
    (void) side;
    (void) type;
    (void) quantity;
    (void) price;
    // Сделал для себя заглушку под виндовс, можно даже и не удалять
    bool signerReady = false;
    if (!_signer.has_value()) _signer = LighterSigner(_signerDllPath.value());
    if (auto err = _signer->createClient(_baseUrl, _apiKeyPrivate.value(), _chainId, _apiKeyIndex, _accountIndex); !
        err) {
        signerReady = true;
    }

    // В лайтере нельзя передавать float, поэтому тут математика со скейлом будет в ридми
    long long baseAmountInt = 0;
    double qtyBase = 0.0;
    if (!quantity.empty()) {
        qtyBase = std::strtod(quantity.c_str(), nullptr);
        baseAmountInt = (long long) llround(qtyBase * (double) _baseAmountScale);
    }

    // Рассчёт защищённой цены (acceptable price):
    // 1) если передана явная цена — используем её
    // 2) иначе — берём среднюю цену исполнения нужного объёма из стакана (для маркет ордеров)
    if (_priceScale <= 0) {
        throw std::runtime_error("priceScale не задан (<= 0)");
    }
    // мб придется раскоментить
    // double acceptablePriceFloat = 1.5;
    // if (price.has_value()) {
    //     acceptablePriceFloat = price.value();
    // } else {
    //     // Получим стакан и посчитаем среднюю цену исполнения для указанного объёма
    //     const int depthLimit = 50;
    //     MarketDepth depth = fetchMarketDepth(symbol, depthLimit);
    //     float avgExec = 0.0f;
    //     if (side == "SELL") {
    //         // Продаём в бид — усреднённая цена для объёма
    //         avgExec = depth.GetBestBidPriceFor((float) qtyBase);
    //         if (avgExec <= 0.0f) throw std::runtime_error("Недостаточно ликвидности в стакане для SELL объёма");
    //         acceptablePriceFloat = (double) avgExec * (1.0 - _defaultSlippage);
    //     } else {
    //         // Покупаем из аск — усреднённая цена для объёма
    //         avgExec = depth.GetBestAskPriceFor((float) qtyBase);
    //         if (avgExec <= 0.0f) throw std::runtime_error("Недостаточно ликвидности в стакане для BUY объёма");
    //         acceptablePriceFloat = (double) avgExec * (1.0 + _defaultSlippage);
    //     }
    // }

    int acceptablePriceInt = this->getAcceptablePriceInt(price, symbol, qtyBase, side);
    // ----------------
    // кастомное шифрование лайтар, работает - не трогаем
    constexpr long long CLIENT_ORDER_INDEX_MAX = ((1LL << 48) - 1);
    long long nowMicros = (long long) std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const long long clientOrderIndex = (nowMicros % CLIENT_ORDER_INDEX_MAX);
    // ----------------

    int isAsk = (side == "SELL" ? 1 : 0);
    const int orderType = 0; // LIMIT
    const int tif = 1; // good till date - для лимиток самое то
    const int reduceOnly = 0;
    const int trigger = 0;
    const long long expiry = -1; // вроде = никогда, todo expire = deadline
    const long long nonce = acquireNextNonce();
    // подпись сделки
    std::string signedPayload;
    if (signerReady) {
        auto signedRes = _signer->signCreateOrder(_marketIndex, clientOrderIndex, baseAmountInt, acceptablePriceInt,
                                                  isAsk, orderType, tif, reduceOnly, trigger, expiry, nonce);
        if (signedRes.second) throw std::runtime_error("LighterSigner signCreateOrder error: " + *signedRes.second);
        signedPayload = *signedRes.first;
    } else {
        // Заглушка для винды
        signedPayload = "dummy";
    }
    std::vector<std::string> headers;
    const std::string token = _authToken ? _authToken.value() : std::string(std::getenv("LIGHTER_AUTH_TOKEN"));
    headers.emplace_back(std::string("Authorization: Bearer ") + token);
    headers.emplace_back("Content-Type: application/x-www-form-urlencoded");

    // application/x-www-form-urlencoded: tx_type=14&price_protection=true&tx_info=<percent-encoded> по доке
    auto isUnreserved = [](unsigned char c) -> bool {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c
               == '.' || c == '~';
    };
    // работает не трогаем, опять же легче написано в их sdk на питоне
    //TODO: ну это не используется, скорее всего убрать надо
    auto percentEncode = [&](const std::string &s) -> std::string {
        static const char *hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() * 3);
        for (unsigned char c: s) {
            if (isUnreserved(c)) out.push_back((char) c);
            else {
                out.push_back('%');
                out.push_back(hex[(c >> 4) & 0xF]);
                out.push_back(hex[c & 0xF]);
            }
        }
        return out;
    };

    // Быстрая отправка по WS
    sendTxOverWs(TX_TYPE_CREATE_ORDER, signedPayload);
    return std::string("sent-via-ws");
}

//todo тут и в httpclient теряется скорость, надо подумать как отправлять максимально быстрые запросы
std::string LighterRequests::modifyOrder(const std::string &symbol,
                                         const std::string &quantity, const std::optional<double> &price,
                                         long long orderIndex, std::string &side, bool hasGoodSpread) {
    (void) symbol;
    (void) quantity;
    (void) price;
    // Сделал для себя заглушку под виндовс, можно даже и не удалять
    bool signerReady = true;
    // if (!_signer.has_value()) _signer = LighterSigner(_signerDllPath.value());
    // if (auto err = _signer->createClient(_baseUrl, _apiKeyPrivate.value(), _chainId, _apiKeyIndex, _accountIndex); !
    //     err) {
    //     signerReady = true;
    // }
    // В лайтере нельзя передавать float, поэтому тут математика со скейлом будет в ридми
    long long baseAmountInt = 0;
    double qtyBase = 0.0;
    if (!quantity.empty()) {
        qtyBase = std::strtod(quantity.c_str(), nullptr);
        baseAmountInt = (long long) llround(qtyBase * (double) _baseAmountScale);
    }

    // Рассчёт защищённой цены (acceptable price):
    // 1) если передана явная цена — используем её
    // 2) иначе — берём среднюю цену исполнения нужного объёма из стакана (для маркет ордеров)
    if (_priceScale <= 0) {
        throw std::runtime_error("priceScale не задан (<= 0)");
    }
    int acceptablePriceInt = this->getAcceptablePriceInt(price, symbol, qtyBase, side);

    const int trigger = 0;
    const long long nonce = acquireNextNonce();
    // подпись сделки
    std::string signedPayload;
    if (signerReady) {
        auto signedRes = _signer->signModifyOrder(_marketIndex, orderIndex, baseAmountInt, acceptablePriceInt,
                                                  trigger, nonce);
        if (signedRes.second) throw std::runtime_error("LighterSigner signModifyOrder error: " + *signedRes.second);
        signedPayload = *signedRes.first;
    } else {
        // Заглушка для винды
        signedPayload = "dummy";
    }
    std::vector<std::string> headers;
    const std::string token = _authToken ? _authToken.value() : std::string(std::getenv("LIGHTER_AUTH_TOKEN"));
    headers.emplace_back(std::string("Authorization: Bearer ") + token);
    headers.emplace_back("Content-Type: application/x-www-form-urlencoded");

    // application/x-www-form-urlencoded: tx_type=14&price_protection=true&tx_info=<percent-encoded> по доке
    auto isUnreserved = [](unsigned char c) -> bool {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c
               == '.' || c == '~';
    };
    // работает не трогаем, опять же легче написано в их sdk на питоне
    //TODO: ну это не используется, скорее всего убрать надо
    auto percentEncode = [&](const std::string &s) -> std::string {
        static const char *hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() * 3);
        for (unsigned char c: s) {
            if (isUnreserved(c)) out.push_back((char) c);
            else {
                out.push_back('%');
                out.push_back(hex[(c >> 4) & 0xF]);
                out.push_back(hex[c & 0xF]);
            }
        }
        return out;
    };

    // Быстрая отправка по WS
    sendTxOverWs(TX_TYPE_MODIFY_ORDER, signedPayload);
    return std::string("sent-via-ws");
}

bool LighterRequests::cancelOrder(const std::string &symbol, const std::string &orderId) {
    (void) symbol;
    (void) orderId;
    // todo если expire = deadline, то не понадобиться
    return false;
}


std::string LighterRequests::changeAccountTier(long long accountIndex, const std::string &newTier) {
    std::string path = "/api/v1/changeAccountTier";

    // form-urlencoded
    auto isUnreserved = [](unsigned char c) -> bool {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c
               == '.' || c == '~';
    };
    auto percentEncode = [&](const std::string &s) -> std::string {
        static const char *hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() * 3);
        for (unsigned char c: s) {
            if (isUnreserved(c)) out.push_back((char) c);
            else {
                out.push_back('%');
                out.push_back(hex[(c >> 4) & 0xF]);
                out.push_back(hex[c & 0xF]);
            }
        }
        return out;
    };

    // Получаем актуальный auth токен: если доступен signer — сгенерируем свежий с дедлайном
    std::string token;
    if (_signer.has_value()) {
        long long deadline = (long long) std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch()).count() + 600; // +10 минут
        auto t = _signer->createAuthToken(deadline);
        if (t.second.has_value()) {
            throw std::runtime_error("createAuthToken error: " + *t.second);
        }
        token = *t.first;
    } else {
        token = _authToken ? _authToken.value() : std::string(std::getenv("LIGHTER_AUTH_TOKEN"));
    }

    std::ostringstream body;
    body << "account_index=" << accountIndex
            << "&new_tier=" << percentEncode(newTier);
    if (!token.empty()) {
        // Дублируем токен в теле как 'auth' для совместимости
        body << "&auth=" << percentEncode(token);
    }

    std::vector<std::string> headers;
    if (!token.empty()) headers.emplace_back(std::string("Authorization: Bearer ") + token);
    headers.emplace_back("Content-Type: application/x-www-form-urlencoded");
    headers.emplace_back("Accept: application/json");

    return HttpClient::httpPost(_baseUrl, path, body.str(), headers);
}
