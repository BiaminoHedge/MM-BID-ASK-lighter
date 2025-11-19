#include "LighterTxWS.h"

#include <chrono>
#include <iostream>

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

static bool parseWssUrlLighterTx(const std::string &url, std::string &host, std::string &port, std::string &target) {
    const std::string scheme = "wss://";
    if (url.rfind(scheme, 0) != 0) return false;
    const std::string rest = url.substr(scheme.size());
    size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    target = (slash == std::string::npos) ? std::string("/") : std::string("/") + rest.substr(slash + 1);
    size_t colon = hostport.find(':');
    if (colon == std::string::npos) { host = hostport; port = "443"; }
    else { host = hostport.substr(0, colon); port = hostport.substr(colon + 1); if (port.empty()) port = "443"; }
    return true;
}

LighterTxWS::LighterTxWS(Config cfg) : _cfg(std::move(cfg)) {}
LighterTxWS::~LighterTxWS() { stop(); }

void LighterTxWS::start() {
    if (_running.exchange(true)) return;
    _readThread = std::thread([this]{ run(); });
}

void LighterTxWS::stop() {
    if (!_running.exchange(false)) return;
    {
        std::lock_guard<std::mutex> lk(_sendMtx);
        _sendCv.notify_all();
    }
    if (_readThread.joinable()) _readThread.join();
    if (_writeThread.joinable()) _writeThread.join();
}

void LighterTxWS::run() {
    std::string host, port, target;
    if (!parseWssUrlLighterTx(_cfg.url, host, port, target)) return;

    net::io_context ioc;
    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none);

    tcp::resolver resolver{ioc};
    beast::ssl_stream<beast::tcp_stream> sslStream{ioc, ctx};
    beast::error_code ec;

    auto const results = resolver.resolve(host, port, ec);
    if (ec) return;
    beast::get_lowest_layer(sslStream).expires_after(std::chrono::seconds(10));
    beast::get_lowest_layer(sslStream).connect(results, ec);
    if (ec) return;

    if (!SSL_set_tlsext_host_name(sslStream.native_handle(), host.c_str())) { return; }
    beast::get_lowest_layer(sslStream).expires_after(std::chrono::seconds(10));
    sslStream.handshake(ssl::stream_base::client, ec);
    if (ec) return;

    auto wsPtr = std::make_shared<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(std::move(sslStream));
    auto &ws = *wsPtr;
    ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    ws.text(true);
    ws.set_option(websocket::stream_base::decorator([&](websocket::request_type &req){
        req.set(beast::http::field::user_agent, std::string("MM-LighterTxWS/1.0"));
        for (const auto &h : _cfg.extraHeaders) {
            auto p = h.find(':'); if (p == std::string::npos) continue;
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
    if (ec) return;

    _wsHolder = std::static_pointer_cast<void>(wsPtr);

    // Запускаем поток записи
    _writeThread = std::thread([this]{ writerLoop(); });

    // Читаем приветствие и ответы
    while (_running.load()) {
        beast::flat_buffer buffer;
        beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(10));
        ws.read(buffer, ec);
        if (ec) {
            if (ec == net::error::operation_aborted || ec == websocket::error::closed) break;
            if (ec == beast::error::timeout) {
                // Keepalive: шлём app-level pong через очередь записи (единая точка write)
                sendText(std::string("{\"type\":\"pong\"}"));
                continue;
            }
            continue;
        }
        std::string data = beast::buffers_to_string(buffer.data());
        if (!data.empty()) {
            // Ответ на текстовый ping по протоколу приложения: через внешнюю очередь (initialText уже прошёл)
            if (data.find("\"type\":\"ping\"") != std::string::npos || data.find("\"message_type\":\"ping\"") != std::string::npos) {
                // Для базового WsClient нет своей очереди записи, поэтому отправим немедленно
                static const std::string pongText = std::string("{\"type\":\"pong\"}");
                beast::error_code wec;
                ws.write(net::buffer(pongText), wec);
                std::cout << wec.value() << std::endl;
            }
            if (_cfg.onMessage) _cfg.onMessage(data);
        }
    }
    beast::error_code _;
    ws.close(websocket::close_code::normal, _);
    try {
        auto cr = ws.reason();
        std::cout << "[LighterTxWS] closed url=" << _cfg.url
                  << " code=" << static_cast<int>(cr.code) << " reason=\"" << cr.reason << "\"" << std::endl;
    } catch (...) {
        std::cout << "[LighterTxWS] closed url=" << _cfg.url << std::endl;
    }
    _wsHolder.reset();
}

void LighterTxWS::sendText(const std::string &text) {
    if (!_running.load()) return;
    {
        std::lock_guard<std::mutex> lk(_sendMtx);
        _sendQueue.emplace_back(text);
    }
    _sendCv.notify_one();
}

void LighterTxWS::writerLoop() {
    while (_running.load()) {
        std::unique_lock<std::mutex> lk(_sendMtx);
        _sendCv.wait(lk, [this]{ return !_sendQueue.empty() || !_running.load(); });
        if (!_running.load()) break;
        if (_sendQueue.empty()) continue;
        std::string msg = std::move(_sendQueue.front());
        _sendQueue.pop_front();
        lk.unlock();

        auto wsPtr = std::static_pointer_cast<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(_wsHolder);
        if (!wsPtr) continue;
        beast::error_code ec;
        wsPtr->write(net::buffer(msg), ec);
        if (ec) {
            // ошибка записи — пропускаем; поток чтения обработает закрытие
        }
    }
}


