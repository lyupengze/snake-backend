# 第一阶段：编译阶段
FROM ubuntu:22.04 AS builder

# 必须安装 git 和 sqlite3 开发库
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

# 【核心修复】安装 SQLite 的运行时库（不需要 -dev 后缀）
RUN apt-get update && apt-get install -y libsqlite3-0 && rm -rf /var/lib/apt/lists/*

# 仅拷贝编译好的可执行文件
COPY --from=builder /app/build/server /app/server

EXPOSE 8080
CMD ["./server"]