#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <memory>

// Класс веб‑сокета для сделок Lighter: держит соединение и
// предоставляет неблокирующую отправку текстовых сообщений.
class LighterTxWS {
public:
    struct Config {
        std::string url;                       // wss://host[:port]/stream
        std::vector<std::string> extraHeaders; // например Authorization: Bearer <token>
        std::function<void(const std::string&)> onMessage; // входящие текстовые сообщения
    };

    explicit LighterTxWS(Config cfg);
    ~LighterTxWS();

    void start();
    void stop();

    // Потокобезопасная отправка произвольного текстового сообщения
    void sendText(const std::string &text);

private:
    void run();
    void writerLoop();

    Config _cfg;
    std::thread _readThread;
    std::thread _writeThread;
    std::atomic<bool> _running{false};

    // Очередь исходящих сообщений
    std::mutex _sendMtx;
    std::condition_variable _sendCv;
    std::deque<std::string> _sendQueue;

    // type-erased указатель на внутренний websocket stream boost::beast
    std::shared_ptr<void> _wsHolder;
};


