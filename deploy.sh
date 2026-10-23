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
sudo git pull --ff-only

# Check if backend code changed, rebuild if necessary
if sudo git diff --quiet HEAD@{1} HEAD; then
  echo "Backend code unchanged, skipping Docker build."
else
  echo "Backend code changed, rebuilding Docker image..."
  sudo docker build -t snake-backend .
  
  echo "Restarting container..."
  sudo docker stop my-snake || true
  sudo docker rm my-snake || true
  # Using named volume
  sudo docker run -d \
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