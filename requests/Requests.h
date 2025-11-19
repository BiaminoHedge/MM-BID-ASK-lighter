#pragma once
#include <string>
#include <vector>
#include <optional>
#include "../MarketDepths/MarketDepth.h"

class Requests {
public:
    virtual ~Requests() = default;

    virtual void setBaseUrl(const std::string &url) = 0;
    virtual const std::string &getBaseUrl() const = 0;

    virtual MarketDepth fetchMarketDepth(const std::string &symbol, int limit) = 0;
    virtual std::string fetchMarketDepthRaw(const std::string &symbol, int limit) = 0;

    virtual std::string createOrder(
            const std::string &symbol,
            const std::string &side,
            const std::string &type,
            std::string quantity,
            const std::optional<double> &price
    ) = 0;

    virtual bool cancelOrder(
            const std::string &symbol,
            const std::string &orderId
    ) = 0;
};
