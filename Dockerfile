# 第一阶段：使用完整工具链编译二进制
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

RUN mkdir build && cd build && cmake .. && make -j$(nproc)


# 第二阶段：最小运行时镜像 — 不包含源码和构建工具
FROM ubuntu:22.04
WORKDIR /app

# 后端使用 localtime_r 解析每日挑战日期；时区必须与服务器目标地区一致。
ENV TZ=Asia/Shanghai
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y tzdata libsqlite3-0 && \
    ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/server /app/server

# 以非 root 用户运行，降低容器被攻破时的影响范围。
RUN groupadd -r appuser && useradd -r -g appuser -u 1000 appuser && \
    chown -R appuser:appuser /app

VOLUME ["/app/data"]

EXPOSE 8080

USER appuser

CMD ["./server"]
