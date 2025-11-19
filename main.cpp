#include <chrono>
#include <iostream>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <iomanip>
#include <limits>
#include <thread>
#include <vector>
#include <curl/curl.h>
#ifdef _WIN32
#include <windows.h>
#include <clocale>
#endif

#include "MarketDepths/AccountAllOrdersWS.h"
#include "MarketDepths/LighterOrderBookWS.h"
#include "Arbitrage/MarketMaker.h"
#include "requests/lighter/LighterSigner.h"
// убрал helper — теперь используем метод на LighterRequests

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::setlocale(LC_ALL, ".UTF-8");
#endif
    // Подписка на все позиции аккаунта и вывод в консоль
    std::string url = "wss://mainnet.zklighter.elliot.ai/stream";
    const char *accEnv = std::getenv("LIGHTER_ACCOUNT_INDEX"); // у них в доке его можно найти по l1 адресу, будет скрин
    // Генерация auth token через LighterSigner (fallback на LIGHTER_AUTH_TOKEN)
    std::string authToken;
    const char *signerPathEnv = std::getenv("LIGHTER_SIGNER_DLL"); // путь к файлу .so
    const char *apiKeyPrivEnv = std::getenv("LIGHTER_API_KEY_PRIVATE"); // будет скрин
    const char *apiKeyIndexEnv = std::getenv("LIGHTER_API_KEY_INDEX"); // будет скрин
    const char *lighterBaseEnv = std::getenv("LIGHTER_BASE_URL");
    const std::string lighterBase = lighterBaseEnv && *lighterBaseEnv ? lighterBaseEnv : std::string("https://mainnet.zklighter.elliot.ai");
    const int chainId = (lighterBase.find("mainnet") != std::string::npos) ? 304 : 300;
    // создаем auth token через signer
    if (signerPathEnv && *signerPathEnv && apiKeyPrivEnv && *apiKeyPrivEnv && apiKeyIndexEnv && *apiKeyIndexEnv && accEnv && *accEnv) {
        try {
            LighterSigner signer(signerPathEnv);
            int apiKeyIndex = std::atoi(apiKeyIndexEnv);
            long long accountIndex = std::strtoll(accEnv, nullptr, 10);
            if (auto err = signer.createClient(lighterBase, apiKeyPrivEnv, chainId, apiKeyIndex, accountIndex); err.has_value()) {
                std::cerr << "Signer createClient error: " << *err << "\n";
                return 1;
            }
            long long deadline = (long long)std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count() + 600;
            auto tokenRes = signer.createAuthToken(deadline);
            if (tokenRes.second.has_value()) {
                std::cerr << "CreateAuthToken error: " << *tokenRes.second << "\n";
                return 1;
            }
            authToken = *tokenRes.first;
        } catch (const std::exception &ex) {
            std::cerr << "Signer exception: " << ex.what() << "\n";
            return 1;
        }
    } else {
        const char *envTok = std::getenv("LIGHTER_AUTH_TOKEN");
        if (envTok) authToken = envTok;
        if (authToken.empty()) {
            std::cerr << "Нет auth токена: задайте переменные для LighterSigner или LIGHTER_AUTH_TOKEN." << "\n";
            //return 1;
        }
    }

    
    // Режим работы: TRADE_MODE=1 — торговля; 0 — только вывод стакана, сделал для себя
    const bool tradeMode = false;

    // Конфигурация MarketMaker
    const char *mktEnv = std::getenv("LIGHTER_MARKET_INDEX");
    std::string marketIndex = (mktEnv && *mktEnv) ? mktEnv : std::string("71");
    // Инициализация клиента для ордеров
    const char *baseUrlEnv = std::getenv("LIGHTER_BASE_URL");
    std::string baseUrl = baseUrlEnv && *baseUrlEnv ? baseUrlEnv : std::string("https://mainnet.zklighter.elliot.ai");
    auto req = std::make_shared<LighterRequests>(baseUrl);
    // Тот же токен для REST
    if (!authToken.empty()) req->setAuthToken(authToken);
    // Настройка сайнера из окружения (если доступно) — используем уже считанные переменные
    if (signerPathEnv && *signerPathEnv && apiKeyPrivEnv && *apiKeyPrivEnv && apiKeyIndexEnv && *apiKeyIndexEnv && accEnv && *accEnv) {
        req->setSignerConfig(
                signerPathEnv,
                apiKeyPrivEnv,
                (lighterBase.find("mainnet") != std::string::npos) ? 304 : 300,
                std::atoi(apiKeyIndexEnv),
                std::atoll(accEnv),
                10, // amount scale будет в ридми сложная система
                100000 // price scale аналогично
        );
    }
    req->setMarketIndex(std::atoi(marketIndex.c_str()));

    // WS стакан
    LighterOrderBookWS::Config obCfg;
    obCfg.url = url;
    if (!authToken.empty()) obCfg.extraHeaders.emplace_back(std::string("Authorization: Bearer ") + authToken);
    obCfg.symbol = marketIndex;
    obCfg.subscribeJson = std::string("{") +
                          "\"type\":\"subscribe\"," +
                          "\"channel\":\"order_book/" + marketIndex + "\"" +
                          "}";
    obCfg.depthLimit = 10;

    // для тестов оставлю
    if (!tradeMode) {
        // Режим: просто печатаем стакан и спред
        obCfg.onDepthUpdated = [](const MarketDepth &depth, long long /*offset*/){
            float bestBid = depth.bids.empty() ? 0.0f : depth.bids.front().first;
            float bestAsk = depth.asks.empty() ? 0.0f : depth.asks.front().first;
            float mid = (bestBid + bestAsk) * 0.5f;
            float spreadPct = (mid > 0.0f && bestAsk > bestBid) ? ((bestAsk - bestBid) / mid) * 100.0f : 0.0f;
            std::cout << std::fixed << std::setprecision(6)
                      << "bid=" << bestBid << " ask=" << bestAsk << " spread%=" << spreadPct << "\n";
        };
        LighterOrderBookWS obws(obCfg);
        obws.start();
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
        return 0;
    }

    // Вызов изменения tier аккаунта через LighterRequests (HttpClient внутри)
    {
        const char *accEnv2 = std::getenv("LIGHTER_ACCOUNT_INDEX");
        long long accountIndex = 143858;
        const char *tierEnv = std::getenv("LIGHTER_NEW_TIER");
        std::string newTier = "premium";

        if (accountIndex > 0 && !authToken.empty()) {
            try {
                std::string resp = req->changeAccountTier(accountIndex, newTier);
                std::cout << "changeAccountTier response: " << resp << "\n";
            } catch (const std::exception &ex) {
                std::cerr << "changeAccountTier error: " << ex.what() << "\n";
            }
        } else {
            std::cerr << "Пропускаю changeAccountTier: нет LIGHTER_ACCOUNT_INDEX или токена." << "\n";
        }
    }

    // торговля
    MarketMaker::Config mmCfg;
    mmCfg.symbol = marketIndex;
    mmCfg.minSpreadPct =0.080f;
    mmCfg.orderSize = 200.0f;
                   //0.47937
    mmCfg.tickSize = 0.00001f;
    mmCfg.requests = req;
    MarketMaker mm(mmCfg);
    mm.start();

    obCfg.onDepthUpdated = [&mm](const MarketDepth &depth, long long /*offset*/){
        mm.updateMarketDepth(depth);
    };
    LighterOrderBookWS obws(obCfg);
    obws.start();

    // Подписка на все ордера аккаунта
    AccountAllOrdersWS::Config aoCfg;
    aoCfg.url = url;
    aoCfg.accountId = accEnv ? accEnv : "143858";
    aoCfg.authToken = authToken;
    // колбэк для канала ордеров аккаунта. отдаёт последний ордер по ts
    aoCfg.onOrdersUpdated = [&mm, marketIndex](const std::unordered_map<int, std::vector<AccountAllOrdersWS::Order>> &byMarket){
        int mkt = std::atoi(marketIndex.c_str());
        auto it = byMarket.find(mkt);
        if (it != byMarket.end()) {
            const auto &orders = it->second;
            const AccountAllOrdersWS::Order *best = nullptr;
            long long bestTs = std::numeric_limits<long long>::min();
            for (const auto &o : orders) {
                if (o.timestamp >= bestTs) { best = &o; bestTs = o.timestamp; }
            }
            if (best) {
                mm.updateOrder(*best);
            }
        }
    };
    AccountAllOrdersWS ao(aoCfg);
    ao.start();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(60000));
    }
    return 0;
}
