#pragma once
#include <sqlite3.h>

#include <cstdint>
#include <ctime>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ReplayEngine.h"

/**
 * 排行榜单条记录结构体
 */
struct ScoreRecord {
    std::string player_name;
    int score = 0;            // 初始化默认值，防止未定义行为
    long long timestamp = 0;  // 初始化默认值，防止未定义行为
    std::string mode;
    std::string date;
};

/**
 * 游戏全局配置常量
 */
namespace GameConfig {
constexpr int kSessionTimeoutSeconds = 3600;  // 单局 Session 最大存活时间（秒）
constexpr int kMaxNameLength = 20;            // 玩家昵称最大长度
}  // namespace GameConfig

/**
 * 单次游戏会话上下文
 */
struct Session {
    std::time_t created_at;  // 创建时间（用于时间悖论校验）
    std::uint32_t seed;      // 服务器下发的权威随机种子
    std::string mode;        // 游戏模式 (classic/daily)
    std::string date;        // 开局时的日期字符串（用于跨日结算）
    bool submitted = false;  // 是否已提交分数（防重放）
};

/**
 * 核心业务管理器：负责管理 Session、防作弊校验以及数据库读写
 */
class GameManager {
public:
    GameManager();
    ~GameManager();

    // 禁用拷贝构造和赋值，防止互斥锁和数据库句柄被错误复制
    GameManager(const GameManager&) = delete;
    GameManager& operator=(const GameManager&) = delete;

    /**
     * 玩家请求开局：生成唯一 gameId 并分配服务器权威 seed
     * @return pair<gameId, seed>
     */
    std::pair<std::string, std::uint32_t> start_game(const std::string& mode);

    /**
     * 提交分数：触发服务器端权威重放，验证分数合法性并写入数据库
     * @return 校验通过并成功入库返回 true，否则返回 false
     */
    bool submit_score(const std::string& game_id, const std::string& name, int score,
                      const std::vector<Action>& actions);

    /**
     * 获取排行榜数据
     */
    std::vector<ScoreRecord> get_top_scores(const std::string& mode, int limit = 10);

private:
    std::string generate_game_id();
    void init_db();
    void cleanup_expired_sessions();

    // 并发保护锁
    std::mutex session_mtx_;  // 保护 active_sessions_ 的并发读写
    std::mutex db_mtx_;       // 保护 SQLite 连接的并发访问

    // 内存中的活跃游戏会话池：gameId -> Session
    std::unordered_map<std::string, Session> active_sessions_;

    // SQLite 数据库句柄
    sqlite3* db_ = nullptr;
};