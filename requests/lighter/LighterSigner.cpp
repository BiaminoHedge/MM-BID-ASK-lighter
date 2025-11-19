#include "LighterSigner.h"
// заглушка под виндовс
// Довольно сложная логика подписи, понятнее написано на питоне в доке лайтера

static std::string cstrOrEmpty(const char *p) { return p ? std::string(p) : std::string(); }

#ifdef _WIN32

LighterSigner::LighterSigner(const std::string &dllPath)
        : m_dllPath(dllPath), m_lib(nullptr), m_createClient(nullptr), m_signCreateOrder(nullptr), m_createAuthToken(nullptr) {}

LighterSigner::~LighterSigner() {}

std::optional<std::string> LighterSigner::createClient(
        const std::string &url,
        const std::string &apiKeyPrivateHex,
        int chainId,
        int apiKeyIndex,
        long long accountIndex) {
    (void)url; (void)apiKeyPrivateHex; (void)chainId; (void)apiKeyIndex; (void)accountIndex;
    return std::make_optional<std::string>("LighterSigner is not supported on Windows in this setup");
}

std::pair<std::optional<std::string>, std::optional<std::string>> LighterSigner::signCreateOrder(
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
        long long nonce) {
    (void)marketIndex; (void)clientOrderIndex; (void)baseAmount; (void)price; (void)isAsk;
    (void)orderType; (void)timeInForce; (void)reduceOnly; (void)triggerPrice; (void)orderExpiry; (void)nonce;
    return {std::nullopt, std::make_optional<std::string>("LighterSigner not available on Windows")};
}

std::pair<std::optional<std::string>, std::optional<std::string>> LighterSigner::signModifyOrder(
        int marketIndex,
        long long clientOrderIndex,
        long long baseAmount,
        int price,
        int triggerPrice,
        long long nonce
        ) {
    (void)marketIndex; (void)clientOrderIndex; (void)baseAmount;
    (void)price; (void)triggerPrice; (void)nonce;
    return {std::nullopt, std::make_optional<std::string>("LighterSigner not available on Windows")};
}

std::pair<std::optional<std::string>, std::optional<std::string>> LighterSigner::createAuthToken(long long deadlineEpochSeconds) {
    (void)deadlineEpochSeconds;
    return {std::nullopt, std::make_optional<std::string>("CreateAuthToken not available on Windows")};
}

#else

#include <dlfcn.h>

LighterSigner::LighterSigner(const std::string &dllPath) : m_dllPath(dllPath), m_lib(nullptr), m_createClient(nullptr), m_signCreateOrder(nullptr), m_createAuthToken(nullptr) {
    m_lib = dlopen(dllPath.c_str(), RTLD_LAZY);
    if (m_lib) {
        m_createClient = reinterpret_cast<CreateClientFn>(dlsym(m_lib, "CreateClient"));
        m_signCreateOrder = reinterpret_cast<SignCreateOrderFn>(dlsym(m_lib, "SignCreateOrder"));
        m_createAuthToken = reinterpret_cast<CreateAuthTokenFn>(dlsym(m_lib, "CreateAuthToken"));
        m_signModifyOrder = reinterpret_cast<SignModifyOrderFn>(dlsym(m_lib, "SignModifyOrder"));
    }
}

LighterSigner::~LighterSigner() {
    if (m_lib) dlclose(m_lib);
}

std::optional<std::string> LighterSigner::createClient(
        const std::string &url,
        const std::string &apiKeyPrivateHex,
        int chainId,
        int apiKeyIndex,
        long long accountIndex) {

    if (!m_lib) return std::make_optional<std::string>("Signer DLL not loaded: " + m_dllPath);
    if (!m_createClient) return std::make_optional<std::string>("Signer DLL: CreateClient not loaded");

    const char *err = m_createClient(url.c_str(), apiKeyPrivateHex.c_str(), chainId, apiKeyIndex, accountIndex);

    if (err) return std::make_optional<std::string>(cstrOrEmpty(err));
    return std::nullopt;
}

std::pair<std::optional<std::string>, std::optional<std::string>> LighterSigner::signModifyOrder(
    int marketIndex,
    long long clientOrderIndex,
    long long baseAmount,
    int price,
    int triggerPrice,
    long long nonce) {
    if (!m_lib) return {std::nullopt, std::make_optional<std::string>("DLL not loaded: " + m_dllPath)};
    if (!m_signModifyOrder) return {std::nullopt, std::make_optional<std::string>("SignModifyOrder not loaded")};

    LighterStrOrErr r = m_signModifyOrder(marketIndex, clientOrderIndex, baseAmount, price, triggerPrice, nonce);

    const std::string s = cstrOrEmpty(r.str);
    const std::string e = cstrOrEmpty(r.err);

    if (!e.empty()) return {std::nullopt, std::make_optional<std::string>(e)};

    return {std::make_optional<std::string>(s), std::nullopt};
}

std::pair<std::optional<std::string>, std::optional<std::string>> LighterSigner::signCreateOrder(
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
        long long nonce) {
    if (!m_lib) return {std::nullopt, std::make_optional<std::string>("DLL not loaded: " + m_dllPath)};
    if (!m_signCreateOrder) return {std::nullopt, std::make_optional<std::string>("SignCreateOrder not loaded")};
    LighterStrOrErr r = m_signCreateOrder(marketIndex, clientOrderIndex, baseAmount, price, isAsk, orderType, timeInForce, reduceOnly, triggerPrice, orderExpiry, nonce);
    const std::string s = cstrOrEmpty(r.str);
    const std::string e = cstrOrEmpty(r.err);
    if (!e.empty()) return {std::nullopt, std::make_optional<std::string>(e)};
    return {std::make_optional<std::string>(s), std::nullopt};
}

std::pair<std::optional<std::string>, std::optional<std::string>> LighterSigner::createAuthToken(long long deadlineEpochSeconds) {
    if (!m_lib) return {std::nullopt, std::make_optional<std::string>("DLL not loaded: " + m_dllPath)};
    if (!m_createAuthToken) return {std::nullopt, std::make_optional<std::string>("CreateAuthToken not loaded")};
    LighterStrOrErr r = m_createAuthToken(deadlineEpochSeconds);
    const std::string s = cstrOrEmpty(r.str);
    const std::string e = cstrOrEmpty(r.err);
    if (!e.empty()) return {std::nullopt, std::make_optional<std::string>(e)};
    return {std::make_optional<std::string>(s), std::nullopt};
}

#endif