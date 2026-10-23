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

# 记录拉取前的 commit hash
BEFORE_PULL=$(git rev-parse HEAD)

# 拉取最新代码
git pull --ff-only

# 获取这次拉取到底更新了哪些文件
CHANGED_FILES=$(git diff --name-only "$BEFORE_PULL" HEAD)

# 检查这些文件里是否包含后端代码或构建配置
if echo "$CHANGED_FILES" | grep -E '\.(cpp|h)$|^CMakeLists\.txt$|^Dockerfile$' > /dev/null; then
  echo "Backend code changed (C++ source or build config modified), rebuilding Docker image..."
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
else
  echo "Backend code unchanged (only non-code files modified), skipping Docker build."
fi

# 3. Health Check
echo "Performing health check..."
sleep 2
curl -s http://127.0.0.1/api/leaderboard > /dev/null && echo "API is healthy." || echo "API check failed, please inspect logs."

echo "Deployment finished!"