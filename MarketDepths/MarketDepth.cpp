//
// Created by Royal Computers on 24.09.2025.
//

#include "MarketDepth.h"
#include <algorithm>
#include <cmath>

static inline bool floatEqual(float a, float b, float eps = 1e-6f) {
    return std::fabs(a - b) <= eps * std::max(1.0f, std::max(std::fabs(a), std::fabs(b)));
}

static void sortDepth(std::vector<std::pair<float, float>> &bids,
                      std::vector<std::pair<float, float>> &asks) {
    // bids: по убыванию цены; asks: по возрастанию цены
    std::sort(bids.begin(), bids.end(), [](const auto &l, const auto &r) {
        return l.first > r.first;
    });
    std::sort(asks.begin(), asks.end(), [](const auto &l, const auto &r) {
        return l.first < r.first;
    });
}

static void applyEdits(std::vector<std::pair<float, float>> &levels,
                       const std::vector<std::pair<float, float>> &edits) {
    for (const auto &e : edits) {
        const float price = e.first;
        const float volume = e.second;
        auto it = std::find_if(levels.begin(), levels.end(), [&](const auto &lvl) {
            return floatEqual(lvl.first, price);
        });
        if (volume <= 0.0f) {
            if (it != levels.end()) levels.erase(it);
            continue;
        }
        if (it != levels.end()) {
            it->second = volume;
        } else {
            levels.emplace_back(price, volume);
        }
    }
}

float MarketDepth::GetBestBidPriceFor(float targetVolume) const {
    if (targetVolume <= 0.0f) return 0.0f;
    float remaining = targetVolume;
    float notional = 0.0;
    for (const auto &lvl : bids) {
        const float price = lvl.first;
        const float volume = lvl.second;
        if (volume <= 0.0f) continue;
        const float take = volume < remaining ? volume : remaining;
        notional += take * price;
        remaining -= take;
        if (remaining <= 0.0f) break;
    }
    if (remaining > 0.0f) return 0.0f; // не хватило ликвидности
    return notional / targetVolume;
}

float MarketDepth::GetBestAskPriceFor(float targetVolume) const {
    if (targetVolume <= 0.0f) return 0.0f;
    float remaining = targetVolume;
    float notional = 0.0;
    for (const auto &lvl : asks) {
        const float price = lvl.first;
        const float volume = lvl.second;
        if (volume <= 0.0f) continue;
        const float take = volume < remaining ? volume : remaining;
        notional += take * price;
        remaining -= take;
        if (remaining <= 0.0f) break;
    }
    if (remaining > 0.0f) return 0.0f; // не хватило ликвидности
    return notional / targetVolume;
}

void MarketDepth::snapshot(std::vector<std::pair<float, float>> _bids,
                           std::vector<std::pair<float, float>> _asks) {
    // Отфильтровать нулевые/отрицательные объёмы
    bids.clear();
    asks.clear();
    bids.reserve(_bids.size());
    asks.reserve(_asks.size());
    for (const auto &v : _bids) if (v.second > 0.0f) bids.push_back(v);
    for (const auto &v : _asks) if (v.second > 0.0f) asks.push_back(v);
    sortDepth(bids, asks);
}

void MarketDepth::update(std::vector<std::pair<float, float>> bidsEditions,
                         std::vector<std::pair<float, float>> asksEditions) {
    applyEdits(bids, bidsEditions);
    applyEdits(asks, asksEditions);
    sortDepth(bids, asks);
}