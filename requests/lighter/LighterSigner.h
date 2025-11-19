#pragma once

#include <string>
#include <optional>

#ifndef _WIN32
#ifndef __cdecl
#define __cdecl
#endif
#endif

struct LighterStrOrErr {
    const char *str;
    const char *err;
};

class LighterSigner {
public:
    explicit LighterSigner(const std::string &dllPath);
    ~LighterSigner();

    std::optional<std::string> createClient(
            const std::string &url,
            const std::string &apiKeyPrivateHex,
            int chainId,
            int apiKeyIndex,
            long long accountIndex
    );

    std::pair<std::optional<std::string>, std::optional<std::string>> signCreateOrder(
            int marketIndex,
            long long clientOrderIndex,
            long long baseAmount,
            int price,
            int isAsk,
            int orderType,
            int timeInForce,
            int reduceOnly,
            int triggerPrice,
            long long orderExpiry,
            long long nonce
    );

    std::pair<std::optional<std::string>, std::optional<std::string>> signModifyOrder(
        int marketIndex,
        long long clientOrderIndex,
        long long baseAmount,
        int price,
        int triggerPrice,
        long long nonce
    );

    // Создать auth-токен с истечением срока в секундах с эпохи (Unix time)
    // не понял как сделать без expire todo
    std::pair<std::optional<std::string>, std::optional<std::string>> createAuthToken(long long deadlineEpochSeconds);

private:
    std::string m_dllPath;
    void *m_lib;

    // работает, значит не трогаем
    using CreateClientFn = const char *(*)(const char *, const char *, int, int, long long);
    using SignCreateOrderFn = LighterStrOrErr(*)(int, long long, long long, int, int, int, int, int, int, long long, long long);
    using CreateAuthTokenFn = LighterStrOrErr(*)(long long);
    using SignModifyOrderFn = LighterStrOrErr(*)(int, long long, long long, long long, long long, long long);


    CreateClientFn m_createClient;
    SignCreateOrderFn m_signCreateOrder;
    CreateAuthTokenFn m_createAuthToken;
    SignModifyOrderFn m_signModifyOrder;
};


