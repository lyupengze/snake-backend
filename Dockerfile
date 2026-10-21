# 第一阶段：编译阶段
FROM ubuntu:22.04 AS builder

# 必须安装 git，因为 CMake 要自动去 GitHub 下载 Crow
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libasio-dev \
    git \
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