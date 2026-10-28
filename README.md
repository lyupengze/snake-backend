# SnakeBattle Backend

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Crow](https://img.shields.io/badge/Crow-1.3.5-green.svg)](https://github.com/CrowCpp/Crow)
[![SQLite](https://img.shields.io/badge/SQLite-3-blue.svg)](https://www.sqlite.org/)
[![Docker](https://img.shields.io/badge/Docker-✓-2496ED.svg)](https://www.docker.com/)
[![License](https://img.shields.io/badge/license-MIT-lightgrey.svg)]()

C++17 贪吃蛇游戏后端服务，基于 Crow 框架，内置服务器权威重放防作弊引擎。

## 技术栈

| 层面 | 技术 |
|---|---|
| 语言 | C++17 |
| 框架 | Crow |
| 数据库 | SQLite3 (WAL 模式) |
| 部署 | Docker 多阶段构建 |

## API

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/start` | 创建游戏会话，返回 `gameId` + 权威 `seed` |
| POST | `/api/score` | 提交分数，携带操作序列进行重放验证 |
| GET | `/api/leaderboard` | 查询排行榜（`?mode=classic/daily/wrap&limit=N`） |

所有接口需通过 `X-Internal-Secret` 头校验，拦截绕过 Nginx 的直连请求。

## 项目结构

```
snake/
├── main.cpp           # 服务入口：路由注册、限流、输入校验
├── GameManager.h/cpp  # 业务层：Session 管理、防作弊校验、数据库读写
├── ReplayEngine.h/cpp # 重放引擎：用种子 + 操作序列模拟完整游戏
├── CMakeLists.txt     # 构建配置
├── Dockerfile         # 多阶段构建（builder → runtime，非 root 运行）
└── deploy.sh          # 一键部署脚本
```

## 防作弊体系

服务器在收到分数提交时执行 7 层校验：

1. **密钥校验** — `X-Internal-Secret` 头必须匹配环境变量
2. **IP 限流** — 读写分离：写接口（`/api/start`、`/api/score`）30 次/分，读接口（`/api/leaderboard`）120 次/分，429 响应为 JSON 格式并携带 `Retry-After` 头
3. **Session 验证** — `gameId` 必须存在且未被提交过
4. **tick 时序** — 操作序列必须严格递增且非负
5. **帧数上限** — `max_tick ≤ 10000`，防止构造超长 payload
6. **时间悖论** — 物理耗时不能远小于理论耗时（`tick × 0.15s`）
7. **权威重放** — 用相同 seed 在 `ReplayEngine` 中重跑游戏，计算分数必须与声称值一致

### PRNG 同步

`ReplayEngine.cpp` 中的 Mulberry32 实现必须与前端 `PRNG.ts` 比特级一致。任何偏差都会导致食物序列错位，使合法玩家也被判定为作弊。

## 数据库

SQLite3，WAL 模式。`scores` 表结构：

```sql
CREATE TABLE scores (
    name TEXT, mode TEXT, date TEXT,
    score INTEGER, timestamp INTEGER,
    PRIMARY KEY(name, mode, date)
);
```

使用 `ON CONFLICT DO UPDATE` 实现 UPSERT，仅当新分数高于历史记录时才更新。

## 部署

```bash
# 构建镜像
docker build -t snake-backend .

# 启动容器
docker run -d \
  --name my-snake \
  -p 127.0.0.1:8080:8080 \
  --restart always \
  -e TZ=Asia/Shanghai \
  -e INTERNAL_SECRET="your-secret" \
  -v snake_data_volume:/app/data \
  snake-backend
```

推荐使用 `deploy.sh` 一键部署。脚本通过源码 MD5 指纹比对实现增量构建——源码未变时跳过 Docker rebuild。

### 密钥配置

部署前需在服务器上创建密钥文件（权限 600，仅 root 可读）：

```bash
openssl rand -hex 32 > /etc/nginx/snake_secret.txt
chmod 600 /etc/nginx/snake_secret.txt
```

`deploy.sh` 启动容器时自动从该文件读取密钥注入环境变量。Nginx 配置中通过 `include` 引用对应的 `/etc/nginx/snake_secret.conf`，在反代时注入 `X-Internal-Secret` 头。

### 环境变量

| 变量 | 来源 | 说明 |
|---|---|---|
| `INTERNAL_SECRET` | `/etc/nginx/snake_secret.txt` | 由 deploy.sh 注入，Nginx 反代时校验 |
| `TZ` | Docker 启动参数 | 时区，影响每日挑战的日期结算 |
