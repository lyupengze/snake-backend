#!/bin/bash
# ==========================================
# 项目一键自动化部署脚本 (Server-side)
# 适用环境: Ubuntu / 生产环境纯部署节点
# ==========================================
set -e # 遇到任何错误立即退出

echo "Starting Snake deployment..."

# ==========================================
# 1. 更新前端 (Web)
# ==========================================
echo "Updating Frontend..."
cd /var/www/snake-frontend

# [关键操作] 强制同步远端代码，杜绝因换行符或权限变动导致的 git pull 冲突
git fetch origin
git reset --hard origin/main

# 执行 Vite 构建，生成最新的 dist 静态资源
npm run build
echo "Frontend build completed."


# ==========================================
# 2. 更新后端 (C++)
# ==========================================
echo "Updating Backend..."
cd /root/snake-backend

# 强制同步远端代码
git fetch origin
git reset --hard origin/main

# [核心防呆设计] 计算 C++ 源码和构建配置的 MD5 指纹
# 排除 .git 等无关文件，只算我们需要编译的文件
CURRENT_HASH=$(find . -maxdepth 1 -type f \( -name "*.cpp" -o -name "*.h" -o -name "CMakeLists.txt" -o -name "Dockerfile" \) -exec md5sum {} + | sort | md5sum | awk '{print $1}')

# 从本地缓存文件读取上一次的指纹（如果不存在则为空）
if [ -f .last_build_hash ]; then
    LAST_HASH=$(cat .last_build_hash)
else
    LAST_HASH=""
fi

# 比对指纹，决定是否需要构建
if [ "$CURRENT_HASH" != "$LAST_HASH" ]; then
  echo "Backend source code changed, rebuilding Docker image..."
  docker build -t snake-backend .

  echo "Restarting container..."
  docker stop my-snake || true
  docker rm my-snake || true

  docker run -d \
    --name my-snake \
    --restart always \
    -p 127.0.0.1:8080:8080 \
    -e TZ=Asia/Shanghai \
    -e INTERNAL_SECRET="${INTERNAL_SECRET}" \
    -v snake_data_volume:/app/data \
    snake-backend

  # 构建成功后，把当前指纹写入缓存文件，供下次比对
  echo "$CURRENT_HASH" > .last_build_hash
  echo "Backend deployment completed."
else
  echo "Backend source code unchanged, skipping Docker build."
fi


# ==========================================
# 3. 部署后健康检查
# ==========================================
echo "Performing health check..."
sleep 2
curl -s http://127.0.0.1/api/leaderboard > /dev/null && echo "API is healthy." || echo "API check failed, please inspect logs."

echo "Deployment finished!"