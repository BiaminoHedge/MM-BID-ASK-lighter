//
// Created by Royal Computers on 24.09.2025.
//

#ifndef MM_BID_ASK_MARKETDEPTH_H
#define MM_BID_ASK_MARKETDEPTH_H
#include <string>
#include <vector>
#include <utility>


class MarketDepth {
public:
    std::vector<std::pair<float, float>> bids;
    std::vector<std::pair<float, float>> asks;

    ~MarketDepth() = default;

    //  лучшая цена для исполнения указанного объёма
    // 0.0f если ликвидности не хватает
    float GetBestBidPriceFor(float targetVolume) const;
    //  вернёт среднюю цену покупки для targetVolume,
    // 0.0f если ликвидности не хватает
    float GetBestAskPriceFor(float targetVolume) const;

    void update(std::vector<std::pair<float, float>> bidsEditions, std::vector<std::pair<float, float>> asksEditions);

    void snapshot(std::vector<std::pair<float, float>> _bids, std::vector<std::pair<float, float>> _asks);
};


#endif //MM_BID_ASK_MARKETDEPTH_H