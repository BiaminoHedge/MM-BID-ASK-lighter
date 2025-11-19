#pragma once

#include <string>
#include <optional>
#include <mutex>
#include "../Requests.h"
#include "../http/HttpClient.h"
#include "LighterSigner.h"
#include "LighterTxWS.h"

// Интеграция Lighter API: стакан (OrderApi.orderBookDetails/orderBookOrders)
// и отправка подписанной транзакции (TransactionApi.sendTx) для маркет-ордера.
class LighterRequests : public Requests, protected HttpClient {
public:
    LighterRequests();
    explicit LighterRequests(const std::string &baseUrl);

    void setBaseUrl(const std::string &url) override;
    const std::string &getBaseUrl() const override;

    // Bearer-токен для /sendTx
    void setAuthToken(const std::string &token);

    // Переопределение путей (при необходимости)
    void setOrderBookPath(const std::string &path);
    void setSendTxPath(const std::string &path);

    // Готовая подписанная транзакция (fallback, если не используем signer)
    void setSignedTx(const std::string &signedTxPayload);

    // Конфиг локального signer'а (.so/.dll)
    void setSignerConfig(
            const std::string &dllPath,
            const std::string &apiKeyPrivateHex,
            int chainId,
            int apiKeyIndex,
            long long accountIndex,
            long long baseAmountScale,
            int priceScale
    );

    // Рыночный индекс и дефолтный слиппедж для защиты цены
    void setMarketIndex(int marketIndex);
    void setDefaultSlippage(double slippagePct) { _defaultSlippage = slippagePct; }

    // Public market data
    MarketDepth fetchMarketDepth(const std::string &symbol, int limit) override;
    std::string fetchMarketDepthRaw(const std::string &symbol, int limit) override;

    // Trading (MARKET) — отправляет /sendTx с tx_type=14
    std::string createOrder(
            const std::string &symbol,
            const std::string &side,
            const std::string &type,
            std::string quantity,
            const std::optional<double> &price
    ) override;

    std::string modifyOrder(
        const std::string &symbol,
        const std::string& quantity,
        const std::optional<double> &price,
        long long orderIndex,
        std::string& side,
        bool hasGoodSpread
    ) ;

    bool cancelOrder(
            const std::string &symbol,
            const std::string &orderId
    ) override;

    // Change account tier via REST
    std::string changeAccountTier(long long accountIndex, const std::string &newTier);

private:
    std::string _baseUrl;
    std::optional<std::string> _authToken;
    std::string _orderBookPath;  // relative path
    std::string _sendTxPath;     // relative path
    std::optional<std::string> _signedTx;

    // Локальный signer
    std::optional<LighterSigner> _signer;
    std::optional<std::string> _signerDllPath;
    std::optional<std::string> _apiKeyPrivate;
    int _chainId = 304;
    int _apiKeyIndex = 0;
    long long _accountIndex = 0;
    int _marketIndex = -1;
    long long _baseAmountScale = 0; // множитель количества -> base_amount(int)
    int _priceScale = 0;            // множитель цены -> price(int)
    double _defaultSlippage = 0.075; // 0.5%

    static MarketDepth parseMarketDepthJson(const std::string &json);

    /*
     * getAccettablePriceInt считает цену для операции без учета спреда
     */
    int getAcceptablePriceInt(const std::optional<double> &price,
                              const std::string &symbol, double qtyBase, const std::string&side);
    /*
     * changePriceIfBadSpread делит цену на 100 если спред плохой
     */

    // WS для ускоренной отправки jsonapi/sendtx
    std::unique_ptr<LighterTxWS> _txWs;
    void ensureTxWs();
    std::string sendTxOverWs(int txType, const std::string &txInfoJson);

    // Nonce: потокобезопасное получение next_nonce с кэшем и авто-инкрементом
    mutable std::mutex _nonceMtx;
    bool _nonceInitialized = false;
    long long _nextNonceCached = 0;
    long long fetchNextNonce();
    long long acquireNextNonce();

};


#define TX_TYPE_CHANGE_PUB_KEY 8
#define    TX_TYPE_CREATE_SUB_ACCOUNT 9
#define    TX_TYPE_CREATE_PUBLIC_POOL 10
#define    TX_TYPE_UPDATE_PUBLIC_POOL 11
#define    TX_TYPE_TRANSFER  12
#define    TX_TYPE_WITHDRAW  13
#define    TX_TYPE_CREATE_ORDER  14
#define    TX_TYPE_CANCEL_ORDER  15
#define    TX_TYPE_CANCEL_ALL_ORDERS 16
#define    TX_TYPE_MODIFY_ORDER 17
#define    TX_TYPE_MINT_SHARES 18
#define    TX_TYPE_BURN_SHARES 19
#define    TX_TYPE_UPDATE_LEVERAGE 20

#define      ORDER_TYPE_LIMIT 0
#define      ORDER_TYPE_MARKET 1
#define      ORDER_TYPE_STOP_LOSS 2
#define      ORDER_TYPE_STOP_LOSS_LIMIT 3
#define      ORDER_TYPE_TAKE_PROFIT 4
#define      ORDER_TYPE_TAKE_PROFIT_LIMIT 5
#define      ORDER_TYPE_TWAP 6

#define      ORDER_TIME_IN_FORCE_IMMEDIATE_OR_CANCEL 0
#define      ORDER_TIME_IN_FORCE_GOOD_TILL_TIME 1
#define      ORDER_TIME_IN_FORCE_POST_ONLY 2

#define      CANCEL_ALL_TIF_IMMEDIATE 0
#define      CANCEL_ALL_TIF_SCHEDULED 1
#define      CANCEL_ALL_TIF_ABORT 2

#define      NIL_TRIGGER_PRICE 0
#define      DEFAULT_28_DAY_ORDER_EXPIRY -1
#define      DEFAULT_IOC_EXPIRY 0
#define      DEFAULT_10_MIN_AUTH_EXPIRY -1
#define      MINUTE = 60

#define      CROSS_MARGIN_MODE  = 0
#define      ISOLATED_MARGIN_MODE = 1