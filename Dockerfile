FROM ubuntu:22.04

# 1) Базовые зависимости + Boost
RUN apt-get update && apt-get install -y \
  curl ca-certificates gnupg lsb-release build-essential pkg-config \
  libcurl4-openssl-dev libssl-dev git wget \
  libboost-system-dev libboost-all-dev && rm -rf /var/lib/apt/lists/*

# 2) Установить CMake 4.x из репозитория Kitware (требуется CMakeLists.txt)
RUN wget -O - https://apt.kitware.com/kitware-archive.sh | bash -s -- && \
    apt-get update && apt-get install -y cmake && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 3) Скопировать проект (предполагается, что signer-amd64.so лежит в корне)
COPY . /app

# 4) Сборка обычным CMake
RUN cmake -B build -S . && \
    cmake --build build -j

# 5) Рантайм: путь к .so рядом с бинарником
ENV LD_LIBRARY_PATH=/app

CMD ["./build/MM-BID-ASK"]


