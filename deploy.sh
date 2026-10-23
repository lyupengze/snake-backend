#!/bin/bash
# ==========================================
# 项目一键自动化部署脚本 (Server-side)
# 适用环境: Ubuntu / 生产环境纯部署节点
# ==========================================
set -e # 遇到任何错误立即退出，防止带病往下执行

echo "Starting Snake deployment..."

# ==========================================
# 1. 更新前端 (Web)
# ==========================================
echo "Updating Frontend..."
cd /var/www/snake-frontend

# [关键操作] 强制同步远端代码。
# 因为服务器是纯部署环境，不应该有本地修改。使用 fetch + reset --hard
# 可以彻底杜绝因换行符(CRLF/LF)或权限(chmod)变动导致的 git pull 冲突。
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

# [关键操作] 记录同步前的 Commit Hash，用于后续比对
BEFORE_PULL=$(git rev-parse HEAD)

# 同样使用强制同步，保证代码绝对一致性
git fetch origin
git reset --hard origin/main

# 记录同步后的 Commit Hash
AFTER_PULL=$(git rev-parse HEAD)

# [智能判断] 检查这段期间的变更文件中，是否包含后端源码或构建配置。
# 正则解释：匹配以 .cpp/.h 结尾，或者是 CMakeLists.txt / Dockerfile 的文件
CHANGED_FILES=$(git diff --name-only "$BEFORE_PULL" "$AFTER_PULL")

if echo "$CHANGED_FILES" | grep -E '\.(cpp|h)$|^CMakeLists\.txt$|^Dockerfile$' > /dev/null; then
  # 如果匹配到了 C++ 文件或构建配置，则重新打 Docker 镜像
  echo "Backend code changed (C++ source or build config modified), rebuilding Docker image..."
  docker build -t snake-backend .

  echo "Restarting container..."
  docker stop my-snake || true
  docker rm my-snake || true

  # [注意] 启动新容器
  # 必须严格挂载 named volume (snake_data_volume)，否则线上数据会丢失
  docker run -d \
    --name my-snake \
    --restart always \
    -p 127.0.0.1:8080:8080 \
    -e TZ=Asia/Shanghai \
    -v snake_data_volume:/app/data \
    snake-backend

  echo "Backend deployment completed."
else
  # 如果只是改了 README、脚本或其他无关文件，直接跳过 Docker 构建，秒级完成部署
  echo "Backend code unchanged (only non-code files modified), skipping Docker build."
fi


# ==========================================
# 3. 部署后健康检查
# ==========================================
echo "Performing health check..."
sleep 2 # 稍等 2 秒，给容器启动的时间

# 尝试请求 API，如果成功则静默，否则输出错误提示
curl -s http://127.0.0.1/api/leaderboard > /dev/null && echo "API is healthy." || echo "API check failed, please inspect logs."

echo "Deployment finished!"