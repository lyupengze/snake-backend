#!/bin/bash
# 一键部署脚本：同时更新前端（静态构建）和后端（Docker 镜像）。
# 用法：./deploy.sh
set -e

echo "Starting Snake deployment..."

# --- Frontend ---
echo "Updating Frontend..."
cd /var/www/snake-frontend

git fetch origin
git reset --hard origin/main

echo "Syncing frontend dependencies..."
npm ci
npm run build
echo "Frontend build completed."


# --- Backend ---
echo "Updating Backend..."
cd /root/snake-backend

git fetch origin
git reset --hard origin/main

# 计算影响二进制的文件指纹。若与上次部署相比无变化，则跳过耗时的 Docker 重建。
CURRENT_HASH=$(find . -maxdepth 1 -type f \( -name "*.cpp" -o -name "*.h" -o -name "CMakeLists.txt" -o -name "Dockerfile" \) -exec md5sum {} + | sort | md5sum | awk '{print $1}')

if [ -f .last_build_hash ]; then
  LAST_HASH=$(cat .last_build_hash)
else
  LAST_HASH=""
fi

# 从权限受限的密钥文件读取，不再依赖环境变量
SECRET_FILE="/etc/nginx/snake_secret.txt"
if [ ! -f "$SECRET_FILE" ]; then
    echo "ERROR: Secret file not found: $SECRET_FILE" >&2
    echo "Create it with: openssl rand -hex 32 > $SECRET_FILE && chmod 600 $SECRET_FILE" >&2
    exit 1
fi
INTERNAL_SECRET=$(cat "$SECRET_FILE")

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

  echo "$CURRENT_HASH" > .last_build_hash
  echo "Backend deployment completed."
else
  echo "Backend source code unchanged, skipping Docker build."
fi


# --- Health check ---
echo "Performing health check..."
sleep 2

# 健康检查经 Nginx 代理链路，同时验证 Nginx 配置和后端服务是否正常。
# 部署时 Nginx 必须处于运行状态，否则检查会失败。
#
# 站点已启用 Let's Encrypt HTTPS，certbot 会插入 80 -> 443 的 301 跳转，
# 因此这里改走 https。用 --resolve 把域名钉到 127.0.0.1：
#   1) 不依赖本机 DNS 解析（解析到公网 IP 时可能回环不通或绕一圈）；
#   2) SNI 仍是 snakebattle.cn，能命中正确的 server 块并校验证书链。
# --max-time 防止 Nginx 挂死时脚本无限等待。
HTTP_CODE=$(curl -s -o /dev/null -w '%{http_code}' --max-time 10 \
  --resolve snakebattle.cn:443:127.0.0.1 \
  'https://snakebattle.cn/api/leaderboard?mode=classic' || echo "000")
if [ "$HTTP_CODE" = "200" ]; then
    echo "API is healthy."
else
    echo "API health check failed (HTTP $HTTP_CODE), please inspect logs." >&2
    exit 1
fi

echo "Deployment finished!"
