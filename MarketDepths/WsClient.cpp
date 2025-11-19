#include "WsClient.h"

#include <iostream>
#include <chrono>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

static bool parseWssUrlWsClient(const std::string &url, std::string &host, std::string &port, std::string &target) {
    const std::string scheme = "wss://";
    if (url.rfind(scheme, 0) != 0) return false;
    const std::string rest = url.substr(scheme.size());
    size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    target = (slash == std::string::npos) ? std::string("/") : std::string("/") + rest.substr(slash + 1);
    size_t colon = hostport.find(':');
    if (colon == std::string::npos) {
        host = hostport; port = "443";
    } else {
        host = hostport.substr(0, colon);
        port = hostport.substr(colon + 1);
        if (port.empty()) port = "443";
    }
    return true;
}

WsClient::WsClient(Config cfg) : _cfg(std::move(cfg)) {}
WsClient::~WsClient() { stop(); }

void WsClient::start() {
    if (_running.exchange(true)) return;
    _thr = std::thread([this]() { run(); });
}

void WsClient::stop() {
    if (!_running.exchange(false)) return;
    if (_thr.joinable()) _thr.join();
}

void WsClient::run() {
    // база буста, всё взято из примеров доки
    std::string host, port, target;
    if (!parseWssUrlWsClient(_cfg.url, host, port, target)) return;

    net::io_context ioc;
    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none);

    tcp::resolver resolver{ioc};
    beast::ssl_stream<beast::tcp_stream> sslStream{ioc, ctx};
    beast::error_code ec;

    std::cout << "[WsClient] resolve " << host << ":" << port << " target=" << target << std::endl;
    auto const results = resolver.resolve(host, port, ec);
    if (ec) return;
    beast::get_lowest_layer(sslStream).expires_after(std::chrono::seconds(10));
    beast::get_lowest_layer(sslStream).connect(results, ec);
    if (ec) {
        std::cerr << "[WsClient] connect error: " << ec.message() << std::endl;
        return;
    }
    if (ec) return;

    if (!SSL_set_tlsext_host_name(sslStream.native_handle(), host.c_str())) {
        return;
    }
    beast::get_lowest_layer(sslStream).expires_after(std::chrono::seconds(10));
    sslStream.handshake(ssl::stream_base::client, ec);
    if (ec) {
        std::cerr << "[WsClient] ssl handshake error: " << ec.message() << std::endl;
        return;
    }

    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws{std::move(sslStream)};
    ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    ws.text(true);
    ws.set_option(websocket::stream_base::decorator([&](websocket::request_type &req){
        req.set(beast::http::field::user_agent, std::string("MM-WSClient/1.0"));
        for (const auto &h : _cfg.extraHeaders) {
            auto p = h.find(':');
            if (p == std::string::npos) continue;
            std::string name = h.substr(0, p);
            std::string value = h.substr(p + 1);
            size_t i = 0; while (i < value.size() && (value[i] == ' ' || value[i] == '\t')) ++i;
            value = value.substr(i);
            if (name == "Authorization" || name == "authorization") {
                req.set(beast::http::field::authorization, value);
            }
        }
    }));

    std::string hostHeader = (port == "443" ? host : host + ":" + port);
    ws.handshake(hostHeader, target, ec);
    if (ec) {
        std::cerr << "[WsClient] ws handshake error: " << ec.message() << std::endl;
        return;
    }
    std::cout << "[WsClient] connected to wss://" << hostHeader << target << std::endl;

    if (!_cfg.initialText.empty()) {
        ws.write(net::buffer(_cfg.initialText), ec);
        if (ec) return;
    }

    // Цикл чтения, без активных задержек
    while (_running.load()) {
        beast::flat_buffer buffer;
        beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(5));
        ws.read(buffer, ec);
        if (ec) {
            if (ec == net::error::operation_aborted) break;
            if (ec == websocket::error::closed) {
                try {
                    auto cr = ws.reason();
                    std::cerr << "[WsClient] closed by peer url=" << _cfg.url
                              << " code=" << static_cast<int>(cr.code) << " reason=\"" << cr.reason << "\""
                              << std::endl;
                } catch (...) {
                    std::cerr << "[WsClient] closed by peer url=" << _cfg.url << std::endl;
                }
                break;
            }
            if (ec == beast::error::timeout) {
                // Таймаут тишины: отправим ping, чтобы держать соединение живым
                beast::error_code pec;
                ws.ping(websocket::ping_data{"ka"}, pec);
                if (pec) {
                    std::cerr << "[WsClient] ping error: " << pec.message() << std::endl;
                }
                // Некоторые серверы ожидают текстовый pong на уровне протокола JSON
                beast::error_code wec;
                static const std::string pongText = std::string("{\"type\":\"pong\"}");
                ws.write(net::buffer(pongText), wec);
                continue;
            }
            std::cerr << "[WsClient] read error: " << ec.message() << std::endl;
            continue;
        }
        std::string data = beast::buffers_to_string(buffer.data());
        if (!data.empty()) {
            // Ответ на текстовый ping по протоколу приложения: через внешнюю очередь (initialText уже прошёл)
            if (data.find("\"type\":\"ping\"") != std::string::npos || data.find("\"message_type\":\"ping\"") != std::string::npos) {
                // Для базового WsClient нет своей очереди записи, поэтому отправим немедленно
                std::cout << data << std::endl;
                static const std::string pongText = std::string("{\"type\":\"pong\"}");
                beast::error_code wec;
                ws.write(net::buffer(pongText), wec);
            }
            if (_cfg.onMessage) _cfg.onMessage(data);
        }
    }
    beast::error_code _;
    ws.close(websocket::close_code::normal, _);
    std::cout << "[WsClient] closed url=" << _cfg.url << std::endl;
}


