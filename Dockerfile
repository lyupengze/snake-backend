# 第一阶段：编译阶段
FROM ubuntu:22.04 AS builder
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libasio-dev \
    git \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
# 开启多核编译加速
RUN mkdir build && cd build && cmake .. && make -j$(nproc)

# 第二阶段：运行阶段
FROM ubuntu:22.04
WORKDIR /app

ENV TZ=Asia/Shanghai
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y tzdata libsqlite3-0 && \
    ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone && \
    rm -rf /var/lib/apt/lists/*

# 只拷贝编译好的二进制文件
COPY --from=builder /app/build/server /app/server

# 声明数据卷，防止手滑丢数据
VOLUME ["/app/data"]

EXPOSE 8080
CMD ["./server"]