FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y \
    g++ \
    cmake \
    make \
    libcurl4-openssl-dev \
    pkg-config \
    python3 \
    python3-pip \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install --break-system-packages yt-dlp

WORKDIR /app

COPY . .

RUN cmake -S . -B build && \
    cmake --build build

CMD ["./build/FacebookDownloadBot"]
