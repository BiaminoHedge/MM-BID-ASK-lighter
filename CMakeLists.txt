cmake_minimum_required(VERSION 3.22)
project(MM-BID-ASK)

set(CMAKE_CXX_STANDARD 20)

add_executable(MM-BID-ASK main.cpp
        MarketDepths/MarketDepth.cpp
        MarketDepths/MarketDepth.h
        MarketDepths/WsClient.cpp
        MarketDepths/WsClient.h
        MarketDepths/LighterOrderBookWS.h
        MarketDepths/LighterOrderBookWS.cpp
        requests/Requests.h
        requests/http/HttpClient.cpp
        requests/http/HttpClient.h
        requests/lighter/LighterRequests.cpp
        requests/lighter/LighterRequests.h
        requests/lighter/LighterTxWS.cpp
        requests/lighter/LighterTxWS.h
        requests/lighter/LighterSigner.cpp
        requests/lighter/LighterSigner.h
        Arbitrage/MarketMaker.cpp
        Arbitrage/MarketMaker.h
        MarketDepths/AccountAllOrdersWS.cpp
)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# Добавляем директории с заголовками в пути поиска
target_include_directories(MM-BID-ASK PRIVATE .)
target_include_directories(MM-BID-ASK PRIVATE MarketDepths)
target_include_directories(MM-BID-ASK PRIVATE requests)
target_include_directories(MM-BID-ASK PRIVATE Arbitrage)
# secp256k1 из vcpkg: стандартный порт secp256k1
# vcpkg экспортирует таргеты как unofficial-secp256k1
#find_package(unofficial-secp256k1 CONFIG REQUIRED)
# unofficial::secp256k1 unofficial::secp256k1_precomputed
target_link_libraries(MM-BID-ASK CURL::libcurl OpenSSL::SSL OpenSSL::Crypto Boost::system )

if (WIN32)
    target_link_libraries(MM-BID-ASK ws2_32)
endif()