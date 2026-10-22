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

struct ScoreRecord {
    std::string player_name;
    int score = 0;            // 【修复】给默认值防未初始化
    long long timestamp = 0;  // 【修复】给默认值防未初始化
    std::string mode;
    std::string date;
};

namespace GameConfig {
constexpr int kSessionTimeoutSeconds = 3600;
constexpr int kMaxNameLength = 20;
}  // namespace GameConfig

struct Session {
    std::time_t created_at;
    std::uint32_t seed;
    std::string mode;
    std::string date;  // 新增：记录开局时的日期字符串
    bool submitted = false;
};

class GameManager {
public:
    GameManager();
    ~GameManager();
    GameManager(const GameManager&) = delete;
    GameManager& operator=(const GameManager&) = delete;

    // 【修改】开局返回 gameId 和 seed
    std::pair<std::string, std::uint32_t> start_game(const std::string& mode);

    // 【修改】提交不再接收 seed
    bool submit_score(const std::string& game_id, const std::string& name, int score,
                      const std::vector<Action>& actions);

    std::vector<ScoreRecord> get_top_scores(const std::string& mode, int limit = 10);

private:
    std::string generate_game_id();
    void init_db();
    void cleanup_expired_sessions();

    std::mutex session_mtx_;
    std::mutex db_mtx_;

    std::unordered_map<std::string, Session> active_sessions_;  // 改成存 Session
    sqlite3* db_ = nullptr;
};