#!/bin/bash
set -e

echo "Starting Snake deployment..."

# 1. Update Frontend
echo "Updating Frontend..."
cd /var/www/snake-frontend
sudo git pull --ff-only
sudo npm run build
echo "Frontend build completed."

# 2. Update Backend
echo "Updating Backend..."
cd /root/snake-backend

# [修复] 记录拉取前的 commit hash
BEFORE_PULL=$(git rev-parse HEAD)

git pull --ff-only

# [修复] 记录拉取后的 commit hash
AFTER_PULL=$(git rev-parse HEAD)

# 检查 commit hash 是否发生变化，如果有则重新构建镜像
if [ "$BEFORE_PULL" = "$AFTER_PULL" ]; then
  echo "Backend code unchanged, skipping Docker build."
else
  echo "Backend code changed, rebuilding Docker image..."
  docker build -t snake-backend .

  echo "Restarting container..."
  docker stop my-snake || true
  docker rm my-snake || true
  # 注意：这里使用了正确的 named volume 挂载
  docker run -d \
    --name my-snake \
    --restart always \
    -p 127.0.0.1:8080:8080 \
    -e TZ=Asia/Shanghai \
    -v snake_data_volume:/app/data \
    snake-backend

  echo "Backend deployment completed."
fi

# 3. Health Check
echo "Performing health check..."
sleep 2
curl -s http://127.0.0.1/api/leaderboard > /dev/null && echo "API is healthy." || echo "API check failed, please inspect logs."

echo "Deployment finished!"