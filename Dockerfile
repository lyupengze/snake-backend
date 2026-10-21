# 第一阶段：编译阶段
FROM ubuntu:22.04 AS builder

# 必须安装 git，因为 CMake 要自动去 GitHub 下载 Crow
# 找到 apt-get install 那一行，加上 libsqlite3-dev
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libasio-dev \
    git \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# 编译
RUN mkdir build && cd build && cmake .. && make

# 第二阶段：运行阶段
FROM ubuntu:22.04
WORKDIR /app

COPY --from=builder /app/build/server /app/server

EXPOSE 8080
CMD ["./server"]