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
    int score = 0;
    long long timestamp = 0;
    std::string mode;
    std::string date;
};

namespace GameConfig {
constexpr int kSessionTimeoutSeconds = 3600;
constexpr int kMaxNameLength = 20;
}  // namespace GameConfig

/**
 * 单次游戏会话上下文
 */
struct Session {
    std::time_t created_at;
    std::uint32_t seed;
    std::string mode;
    std::string date;
    bool submitted = false;
};

/**
 * [新增] 定义分数提交的具体结果，用于向路由层传递精确的 HTTP 状态码
 */
enum class SubmitResult {
    Success,          // 提交成功 (200)
    InvalidData,      // 数据不合法（如名字为空或超长） (400)
    Cheating,         // 触发防作弊校验（如时间悖论、分数不符） (403)
    PayloadTooLarge,  // 超出最大帧数限制（超长局） (413)
    InternalError     // 数据库异常等内部错误 (500)
};

/**
 * 核心业务管理器：负责管理 Session、防作弊校验以及数据库读写
 */
class GameManager {
public:
    GameManager();
    ~GameManager();

    GameManager(const GameManager&) = delete;
    GameManager& operator=(const GameManager&) = delete;

    std::pair<std::string, std::uint32_t> start_game(const std::string& mode);

    /**
     * 提交分数：[修改] 返回具体的枚举类型，方便路由层判断
     */
    SubmitResult submit_score(const std::string& game_id, const std::string& name, int score,
                              const std::vector<Action>& actions);

    std::vector<ScoreRecord> get_top_scores(const std::string& mode, int limit = 10);

private:
    std::string generate_game_id();
    void init_db();
    void cleanup_expired_sessions();

    std::mutex session_mtx_;
    std::mutex db_mtx_;

    std::unordered_map<std::string, Session> active_sessions_;

    sqlite3* db_ = nullptr;
};