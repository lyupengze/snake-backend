# 第一阶段：使用完整工具链编译二进制
FROM ubuntu:22.04 AS builder

# 构建阶段同样需要非交互模式，否则个别维护脚本会停在提示上等输入。
ENV DEBIAN_FRONTEND=noninteractive

# 基础镜像默认源是 archive.ubuntu.com / security.ubuntu.com，其前端是 Cloudflare，
# 国内 ECS 上经常出现 TCP 半开（连接 ESTABLISHED 但对方不再回包），apt 不会报错，
# 只会一直挂着。这里换成阿里云内网源：ECS 内网直连，不计公网流量。
RUN set -eux; \
    for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources; do \
        [ -f "$f" ] || continue; \
        sed -i -E 's@https?://(archive|security)\.ubuntu\.com@http://mirrors.cloud.aliyuncs.com@g' "$f"; \
    done

# ForceIPv4：避免先尝试不可达的 IPv6 地址而空等。
# Timeout/Retries：连接卡住时 30 秒内失败重试，而不是无限等待。
RUN apt-get -o Acquire::ForceIPv4=true \
            -o Acquire::http::Timeout=30 \
            -o Acquire::https::Timeout=30 \
            -o Acquire::Retries=5 \
            update \
 && apt-get install -y --no-install-recommends \
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

RUN set -eux; \
    for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources; do \
        [ -f "$f" ] || continue; \
        sed -i -E 's@https?://(archive|security)\.ubuntu\.com@http://mirrors.cloud.aliyuncs.com@g' "$f"; \
    done

RUN apt-get -o Acquire::ForceIPv4=true \
            -o Acquire::http::Timeout=30 \
            -o Acquire::https::Timeout=30 \
            -o Acquire::Retries=5 \
            update \
 && apt-get install -y --no-install-recommends tzdata libsqlite3-0 \
 && ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone \
 && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/server /app/server

# 以非 root 用户运行，降低容器被攻破时的影响范围。
RUN groupadd -r appuser && useradd -r -g appuser -u 1000 appuser && \
    chown -R appuser:appuser /app

VOLUME ["/app/data"]

EXPOSE 8080

USER appuser

CMD ["./server"]
