# SnakeBattle Backend

[![CI Backend](https://github.com/lyupengze/snake-backend/actions/workflows/ci-backend.yml/badge.svg)](https://github.com/lyupengze/snake-backend/actions/workflows/ci-backend.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Crow](https://img.shields.io/badge/Crow-1.3.5-green.svg)](https://github.com/CrowCpp/Crow)
[![SQLite](https://img.shields.io/badge/SQLite-3-blue.svg)](https://www.sqlite.org/)
[![Docker](https://img.shields.io/badge/Docker-%E2%9C%93-2496ED.svg)](https://www.docker.com/)
[![License](https://img.shields.io/badge/license-MIT-lightgrey.svg)](LICENSE)

贪吃蛇游戏的服务端：发放权威随机种子、管理游戏会话、在服务端重放整局操作验证分数，并提供排行榜查询。后端不信任客户端上报的分数，只相信可复现的操作序列。

- 线上站点：<https://snakebattle.cn>
- 线上 API：<https://snakebattle.cn/api>
- 前端仓库：<https://github.com/lyupengze/snake-frontend>
- 后端仓库：<https://github.com/lyupengze/snake-backend>

## 职责

- **会话与种子**：每次开局生成 16 位 `gameId` 和服务器种子；每日挑战按日期派生全服同一种子。
- **回放校验**：收到分数后用同一 seed 重跑 `ReplayEngine`，计算结果必须与声称分数完全一致。
- **排行榜**：按模式查询；每日挑战只统计当天，经典/穿墙为历史总榜。
- **持久化**：SQLite（WAL 模式），同一玩家在同一模式、同一日期下只保留最高分；经典/穿墙按总榜记录。

## 技术栈

| 层面 | 技术 |
|---|---|
| 语言 | C++17 |
| HTTP 框架 | [Crow](https://github.com/CrowCpp/Crow) 1.3.5（CMake FetchContent 固定 commit） |
| 数据库 | SQLite3，WAL 模式 |
| 构建 | CMake ≥ 3.14 |
| 部署 | Docker 多阶段构建 + Nginx 反向代理 |
| CI | GitHub Actions（CMake 编译校验） |

## 快速开始

### Docker（推荐）

```bash
# 生成写接口密钥；容器和 Nginx 必须使用同一个值
export INTERNAL_SECRET="$(openssl rand -hex 32)"

docker build -t snake-backend .
docker run -d \
  --name my-snake \
  -p 127.0.0.1:8080:8080 \
  --restart always \
  -e TZ=Asia/Shanghai \
  -e INTERNAL_SECRET="$INTERNAL_SECRET" \
  -v snake_data_volume:/app/data \
  snake-backend
```

验证：

```bash
# 读接口公开，不需要密钥
curl -s "http://127.0.0.1:8080/api/leaderboard?mode=classic&limit=3"

# 写接口默认只接受带 X-Internal-Secret 的请求
curl -s -X POST http://127.0.0.1:8080/api/start \
  -H 'Content-Type: application/json' \
  -H "X-Internal-Secret: $INTERNAL_SECRET" \
  -d '{"mode":"classic"}'
```

服务默认监听 `0.0.0.0:8080`。生产环境只应把端口发布到 `127.0.0.1`，由 Nginx 对外提供服务；直接把 8080 暴露到公网会绕过 Nginx 的统一入口和密钥注入。

### 本地编译

Linux 依赖：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libasio-dev libsqlite3-dev git ca-certificates
```

Windows 建议使用 MSYS2 UCRT64，安装 GCC、CMake、Asio 和 SQLite：

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-asio mingw-w64-ucrt-x86_64-sqlite3
```

构建：

```bash
cmake -S . -B build
cmake --build build -j
```

启动（Linux/macOS）：

```bash
# 代码在非 Windows 平台把数据库固定为 /app/data/snake.db，需要该目录可写
sudo mkdir -p /app/data
sudo chown "$USER" /app/data

INTERNAL_SECRET=dev-secret ./build/server
```

Windows 下数据库写在启动目录的 `snake.db`：

```powershell
$env:INTERNAL_SECRET = "dev-secret"
.\build\server.exe
```

> 本地构建时，如果根目录存在 `Crow-master/` 会直接使用；否则 CMake 会从 GitHub 拉取固定版本的 Crow。`Crow-master/` 不在仓库中。Windows 的 Asio 头文件路径在 `CMakeLists.txt` 中默认按 `D:/msys64/ucrt64/include` 配置，路径不同时需要调整。

## API

所有写接口都要求 `Content-Type: application/json`，请求体大小和字段限制如下。

### 认证与限流

- 写接口（`/api/start`、`/api/score`）校验请求头 `X-Internal-Secret`。这个头由 Nginx 在反向代理时注入，浏览器端不接触密钥；绕过 Nginx 直连后端时，需要手动带上该头，否则返回 `403`。
- 读接口（`/api/leaderboard`）公开，无需密钥。
- 限流按客户端 IP 统计：写接口 30 次/分钟，读接口 120 次/分钟。超限返回 `429`，响应体为 JSON 并带 `Retry-After: 60`。
- IP 优先取 Nginx 注入的 `X-Real-IP`，没有时使用 TCP 对端地址。Nginx 必须用 `$remote_addr` 覆盖该头，不能透传客户端自带值。
- API 由 Nginx 与前端同源提供，后端本身不设置 CORS 响应头。

### POST `/api/start`

创建游戏会话。请求体最大 1 KiB。

```json
{ "mode": "classic" }
```

`mode` 可省略，默认 `classic`，可选值：`classic`（经典）、`daily`（每日挑战）、`wrap`（穿墙）。

响应：

```json
{
  "gameId": "5RFTaDNA49lJeQ2n",
  "seed": 1600658895,
  "mode": "classic"
}
```

- `gameId` 为 16 位 URL 安全字母数字，服务端内存保存；设计有效期为 1 小时（过期清理在下次 `/api/start` 时触发），且只能提交一次。
- `seed` 是权威种子，客户端必须用它初始化 PRNG，不能自行选择。
- 每日挑战的 `seed` 由服务器日期派生，同一天所有玩家相同；经典/穿墙模式每次开局随机。

### POST `/api/score`

提交分数并附带完整操作序列。请求体最大 500 KiB。

| 字段 | 类型 | 说明 |
|---|---|---|
| `gameId` | string | `/api/start` 返回的 16 位 ID |
| `name` | string | 排行榜昵称，去除首尾空格后 1–20 字节；仅允许 ASCII 字母数字、`_`、`-` 和 UTF-8 多字节字符 |
| `score` | number | 客户端声称的最终得分 |
| `actions` | array | 操作列表，元素为 `{ "tick": number, "dir": "UP｜DOWN｜LEFT｜RIGHT" }` |

`actions` 必须按 `tick` 严格递增且非负，最多 10000 条、最大 tick 不超过 10000。请求体结构示例：

```json
{
  "gameId": "5RFTaDNA49lJeQ2n",
  "name": "player",
  "score": 3,
  "actions": [
    { "tick": 0, "dir": "DOWN" },
    { "tick": 7, "dir": "RIGHT" }
  ]
}
```

上面只是字段示意；`actions` 必须是真实玩出的完整序列，且回放结果要等于 `score`，否则返回 `403`。

成功时返回纯文本：

```text
Score submitted successfully
```

会话在数据库写入成功后才会被删除；数据库写入失败时会恢复会话并返回 `500`，客户端可以重试。分数提交成功或判定作弊后，同一个 `gameId` 不能再提交。

### GET `/api/leaderboard`

查询排行榜。

| 参数 | 必填 | 说明 |
|---|---|---|
| `mode` | 是 | `classic`、`daily` 或 `wrap`；缺失或非法返回 `400` |
| `limit` | 否 | 返回条数，1–100，默认 10；非法值按 10 处理 |

```bash
curl -s "https://snakebattle.cn/api/leaderboard?mode=classic&limit=10"
```

响应：

```json
{
  "data": [
    { "playerName": "SpamtonNEO", "score": 71 }
  ]
}
```

`daily` 只返回服务器当天（`TZ=Asia/Shanghai`）的成绩；`classic`、`wrap` 返回历史总榜。

### 状态码

| 状态码 | 含义 |
|---|---|
| `200` | 成功；`/api/score` 的响应体是纯文本 |
| `400` | JSON 非法、字段缺失/类型错误、模式非法、昵称非法、`actions` 格式错误 |
| `403` | `X-Internal-Secret` 不匹配、`gameId` 不存在/已过期/已提交、回放得分不一致 |
| `413` | 请求体超过 1 KiB（start）或 500 KiB（score）、操作数超过 10000、`tick` 超过 10000 |
| `415` | `Content-Type` 不是 `application/json` |
| `429` | 触发限流；JSON 响应，带 `Retry-After: 60` |
| `500` | 数据库读写失败等内部错误 |

## 反作弊设计

提交分数时，服务端依次检查：

1. **密钥**：`X-Internal-Secret` 必须与环境变量一致，拦截绕过 Nginx 的直连写请求。
2. **限流**：写接口每 IP 30 次/分钟。
3. **会话**：`gameId` 必须存在、未过期、未提交过。
4. **tick 时序**：操作必须严格递增且非负，防止重排/重复操作。
5. **规模上限**：操作数 ≤ 10000，`max_tick` ≤ 10000，避免超长回放消耗 CPU。
6. **时间悖论**：从开局到提交的实际耗时不能明显短于 `max_tick × 0.15` 秒（允许 3 秒误差），否则视为加速外挂或伪造回放。
7. **权威重放**：用服务器保存的 seed，在 `ReplayEngine` 中重跑整局，计算出的得分必须与 `score` 完全相等。

此外，入口层还做了 JSON、字段类型、方向枚举、昵称字符和请求体大小校验，这些属于输入校验，不计入上面的反作弊层数。

### PRNG 与回放同步

`ReplayEngine.cpp` 中的 Mulberry32 必须与前端 `src/game/PRNG.ts` 的 `createSeededRandom()` 产生完全相同的位序列。以下任意一项不一致，都会让合法玩家的回放结果偏移，最终被判定为作弊：

- PRNG 的加法常数、位运算顺序和 32 位截断方式；
- 棋盘尺寸与初始状态：20×20 格、格子 20px、蛇初始位于 `(200, 200)`、长度 1、方向 `RIGHT`；
- 食物候选格的遍历顺序：外层 `x`、内层 `y`，且排除蛇身占用格；
- 每 tick 的移动、碰撞、吃食物和增长顺序；
- 穿墙模式的边界回绕规则；
- 单局最大 tick 数 10000。

每日挑战的种子按服务器本地日期计算：

```text
base = year * 10000 + month * 100 + day
seed = (base XOR 0x9E3779B9) * 2654435761   // 32 位无符号回绕
```

`0x9E3779B9` 是源码中的固定盐值，不是密钥；它让种子不能直接从日期读出，但不构成密码学保护。服务端时区必须稳定为 `Asia/Shanghai`，否则跨日边界会与玩家预期不一致。

### 设计边界

- 会话保存在进程内存中，**服务重启后未提交的会话会丢失**，玩家需要重新开局。
- 限流器同样是进程内状态；多实例部署无法共享会话和限流计数。当前按单容器设计。
- SQLite 适合当前规模；写操作串行化，没有分库分表或迁移框架。
- 时间悖论检查是启发式规则，依赖客户端 `tick` 与真实耗时的对应关系，不能替代重放校验。

## 数据库

数据库路径：

- Linux/macOS：`/app/data/snake.db`（Docker 卷 `snake_data_volume` 挂载到 `/app/data`）
- Windows：启动目录下的 `snake.db`

使用 WAL 模式，表结构：

```sql
CREATE TABLE scores (
    name TEXT,
    mode TEXT,
    date TEXT,
    score INTEGER,
    timestamp INTEGER,
    PRIMARY KEY (name, mode, date)
);
```

写入使用 `ON CONFLICT ... DO UPDATE ... WHERE excluded.score > scores.score`，只有新分数更高时才覆盖。`date` 对每日挑战是 `YYYY-MM-DD`，对经典/穿墙固定为 `all`。

### 清空排行榜

`clear_leaderboard.sh` 会先在线备份数据库，再停容器、删除记录、`VACUUM`、修正文件属主，最后重启并做健康检查。脚本需要 `docker` 和 `python3`，且通常要在服务器上以 root 运行：

```bash
bash clear_leaderboard.sh                    # 清空所有模式
bash clear_leaderboard.sh classic            # 只清空经典模式
bash clear_leaderboard.sh wrap               # 只清空穿墙模式
bash clear_leaderboard.sh daily              # 只清空“今天”的每日挑战
bash clear_leaderboard.sh daily 2026-11-19   # 只清空指定日期的每日挑战
```

可用环境变量：

| 变量 | 默认值 | 说明 |
|---|---|---|
| `CONTAINER` | `my-snake` | 容器名 |
| `VOLUME` | `snake_data_volume` | Docker 数据卷名 |
| `BACKUP_DIR` | `/root/snake-db-backups` | 备份目录 |
| `NO_RESTART` | `0` | 设为 `1` 时不重启容器，同时跳过 `VACUUM` 和健康检查 |

备份文件名为 `snake-YYYYmmdd-HHMMSS.db`。`deploy.sh` 会在服务器仓库执行 `git reset --hard origin/main`，所以脚本改动要先提交并推送，再通过部署流程拉到服务器。

## 部署

### 拓扑

```text
浏览器 / Tauri / Android
        │ HTTPS
        ▼
      Nginx
      ├── /            → /var/www/snake-frontend/dist（静态前端）
      └── /api/        → 127.0.0.1:8080（注入 X-Internal-Secret、X-Real-IP）
                              │
                              ▼
                        snake-backend 容器
                              │
                              ▼
                     snake_data_volume/snake.db
```

Nginx 需要做两件事：把 `/api/` 反向代理到后端，并在代理时注入写接口密钥和真实客户端 IP。下面只是示例，实际配置以服务器为准：

```nginx
location /api/ {
    proxy_pass http://127.0.0.1:8080;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    include /etc/nginx/snake_secret.conf;
}
```

`/etc/nginx/snake_secret.conf` 中放置：

```nginx
proxy_set_header X-Internal-Secret "与容器 INTERNAL_SECRET 相同的值";
```

密钥文件建议权限 `600`，只允许 root 读取：

```bash
openssl rand -hex 32 > /etc/nginx/snake_secret.txt
chmod 600 /etc/nginx/snake_secret.txt
```

`deploy.sh` 从 `snake_secret.txt` 读取密钥注入容器；Nginx 的 `snake_secret.conf` 需要单独保持同步。

### 一键部署

`deploy.sh` 同时更新前端和后端，默认假设服务器上的目录为：

- 前端：`/var/www/snake-frontend`
- 后端：`/root/snake-backend`

执行流程：

1. 前端 `git fetch` + `git reset --hard origin/main`，然后 `npm ci`、`npm run build`；
2. 后端同样重置到 `origin/main`，对 `*.cpp`、`*.h`、`CMakeLists.txt`、`Dockerfile` 计算 MD5 指纹；
3. 指纹变化时重建镜像、重启 `my-snake` 容器，并写入 `.last_build_hash`；未变化则跳过重建；
4. 通过 `https://snakebattle.cn/api/leaderboard?mode=classic` 做健康检查。

运行前需要：root 权限、Docker、Nginx、Node.js/npm，以及已配置好的 HTTPS 证书。健康检查使用 `--resolve snakebattle.cn:443:127.0.0.1`，因此本机 Nginx 必须处于运行状态；证书或 Nginx 配置有问题时部署会失败。

### 环境变量

| 变量 | 是否必需 | 说明 |
|---|---|---|
| `INTERNAL_SECRET` | 是 | 写接口密钥；为空时 `/api/start`、`/api/score` 都返回 `403`。由 `deploy.sh` 从密钥文件注入，不要提交到仓库 |
| `TZ` | 建议 | 每日挑战的日期边界；Dockerfile 默认 `Asia/Shanghai`，应与服务器/容器保持一致 |

## 项目结构

```text
snake/
├── main.cpp                  # Crow 路由、密钥校验、限流、请求校验
├── GameManager.h/cpp         # 会话管理、反作弊流程、SQLite 读写
├── ReplayEngine.h/cpp        # 服务端重放引擎（Mulberry32）
├── CMakeLists.txt            # C++17 构建，FetchContent 拉取 Crow 1.3.5
├── Dockerfile                # 多阶段构建，非 root 运行
├── deploy.sh                 # 前后端一键部署与健康检查
├── clear_leaderboard.sh      # 备份/清空排行榜
├── .github/workflows/ci-backend.yml  # CMake 编译 CI
└── LICENSE                   # MIT
```

运行时生成的 `snake.db`、`snake.db-wal`、`snake.db-shm` 已被 `.gitignore` 忽略。

## CI

`.github/workflows/ci-backend.yml` 在以下场景运行：

- 推送到 `main`，且改动涉及 `*.cpp`、`*.h`、`CMakeLists.txt`、`Dockerfile` 或 workflow 本身；
- 对 `main` 的 Pull Request。

流程为：安装 `libasio-dev`、`libsqlite3-dev` → `cmake` 配置 → `make -j$(nproc)`。当前 CI 只做编译校验，没有自动化测试；数据库和回放逻辑的回归需要手动验证。

## 相关仓库

- 前端：<https://github.com/lyupengze/snake-frontend>
- 后端：<https://github.com/lyupengze/snake-backend>
- 展示站：<https://snakebattle.cn>

## License

[MIT](LICENSE)
