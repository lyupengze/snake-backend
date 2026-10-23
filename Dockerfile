# ==========================================
# 第一阶段：编译环境 (Builder)
# 负责安装完整的编译工具链，并构建出二进制文件
# ==========================================
FROM ubuntu:22.04 AS builder

# 安装编译所需的依赖库
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libasio-dev \
    git \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
# 将宿主机的源码复制进镜像
COPY . .

# 执行 CMake 构建，并使用 $(nproc) 开启多核并行编译加速
RUN mkdir build && cd build && cmake .. && make -j$(nproc)


# ==========================================
# 第二阶段：运行环境 (Runtime)
# 只保留运行所需的依赖，极大地减小镜像体积并提升安全性
# ==========================================
FROM ubuntu:22.04
WORKDIR /app

# 设置时区环境变量（后端使用 localtime_r 处理每日挑战的日期结算，强依赖正确时区）
ENV TZ=Asia/Shanghai
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y tzdata libsqlite3-0 && \
    ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone && \
    rm -rf /var/lib/apt/lists/*

# 仅从 builder 阶段拷贝编译好的二进制文件，丢弃所有源码和编译工具
COPY --from=builder /app/build/server /app/server

# [安全最佳实践] 创建非 root 用户运行服务
# 防止容器内的应用被攻破后，攻击者获得容器内的 root 权限
RUN groupadd -r appuser && useradd -r -g appuser -u 1000 appuser && \
    chown -R appuser:appuser /app

# 声明数据卷挂载点。配合 docker run 的 -v 参数，确保 SQLite 数据持久化在宿主机上
VOLUME ["/app/data"]

EXPOSE 8080

# 切换为非 root 用户运行
USER appuser

CMD ["./server"]