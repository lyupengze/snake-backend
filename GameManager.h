#pragma once
#include <sqlite3.h>

#include <ctime>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

struct ScoreRecord {
    std::string player_name;
    int score = 0;            // 【修复】给默认值防未初始化
    long long timestamp = 0;  // 【修复】给默认值防未初始化
    std::string mode;
    std::string date;
};

namespace GameConfig {
constexpr int kMaxScore = 500;
constexpr double kMaxScorePerSecond = 50.0;
constexpr int kSessionTimeoutSeconds = 3600;
constexpr int kMaxNameLength = 20;
}  // namespace GameConfig

class GameManager {
public:
    GameManager();
    ~GameManager();

    GameManager(const GameManager&) = delete;
    GameManager& operator=(const GameManager&) = delete;

    std::string start_game();
    bool submit_score(const std::string& game_id, const std::string& name, int score,
                      const std::string& mode);
    std::vector<ScoreRecord> get_top_scores(const std::string& mode, int limit = 10);

private:
    std::string generate_game_id();
    void init_db();
    void cleanup_expired_sessions();

    std::mutex session_mtx_;
    std::mutex db_mtx_;

    std::unordered_map<std::string, std::time_t> active_sessions_;
    sqlite3* db_ = nullptr;
};