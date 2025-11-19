#include "HttpClient.h"
#include <stdexcept>
#include <curl/curl.h>

static size_t curlWriteToStringInternal(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t totalSize = size * nmemb;
    std::string *buffer = static_cast<std::string *>(userp);
    buffer->append(static_cast<const char *>(contents), totalSize);
    return totalSize;
}

static std::string buildUrl(const std::string &baseUrl, const std::string &path) {
    if (path.empty()) return baseUrl;
    std::string url = baseUrl;
    bool urlEndsWithSlash = !url.empty() && url.back() == '/';
    bool pathStartsWithSlash = !path.empty() && path.front() == '/';
    if (urlEndsWithSlash && pathStartsWithSlash) url.pop_back();
    else if (!urlEndsWithSlash && !pathStartsWithSlash) url.push_back('/');
    url += path;
    return url;
}

std::string HttpClient::httpGet(const std::string &baseUrl, const std::string &pathWithQuery) {
    CURL *curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");
    std::string response;
    std::string url = buildUrl(baseUrl, pathWithQuery);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToStringInternal);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MM-BID-ASK/1.0");
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("curl_easy_perform(GET) failed: ") + curl_easy_strerror(res));
    }
    curl_easy_cleanup(curl);
    return response;
}
// todo вот тут надо выжить все соки, так как запрос идёт максимально долго и мм (он есть) перебивает просто
std::string HttpClient::httpPost(const std::string &baseUrl, const std::string &path,
                                 const std::string &body,
                                 const std::vector<std::string> &headersIn) {
    CURL *curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");
    std::string response;
    std::string url = buildUrl(baseUrl, path);
    struct curl_slist *headers = nullptr;
    for (const auto &h : headersIn) headers = curl_slist_append(headers, h.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToStringInternal);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MM-BID-ASK/1.0");
    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    } else {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    }
    CURLcode res = curl_easy_perform(curl);
    if (headers) curl_slist_free_all(headers);
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("curl_easy_perform(POST) failed: ") + curl_easy_strerror(res));
    }
    curl_easy_cleanup(curl);
    return response;
}


