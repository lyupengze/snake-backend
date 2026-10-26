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
    int score = 0;
    long long timestamp = 0;
    std::string mode;
    std::string date;
};

namespace GameConfig {
constexpr int kSessionTimeoutSeconds = 3600;
constexpr int kMaxNameLength = 20;
}

struct Session {
    std::time_t created_at;
    std::uint32_t seed;
    std::string mode;
    std::string date;
    bool submitted = false;
};

enum class SubmitResult {
    Success,
    InvalidData,
    Cheating,
    PayloadTooLarge,
    InternalError
};

// 管理游戏会话生命周期、反作弊验证、分数持久化。
// 线程安全：会话表与数据库访问分别使用独立的互斥锁。
class GameManager {
public:
    GameManager();
    ~GameManager();

    GameManager(const GameManager&) = delete;
    GameManager& operator=(const GameManager&) = delete;

    std::pair<std::string, std::uint32_t> start_game(const std::string& mode);

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
