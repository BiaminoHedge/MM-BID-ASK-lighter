#pragma once

#include <string>
#include <vector>

class HttpClient {
protected:
    // Обычные curl запросы, переиспользую логику

    static std::string httpGet(const std::string &baseUrl, const std::string &pathWithQuery);

    static std::string httpPost(
            const std::string &baseUrl,
            const std::string &path,
            const std::string &body,
            const std::vector<std::string> &headers
    );
};


