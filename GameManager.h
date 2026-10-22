#pragma once
#include <sqlite3.h>  // 【新增】

#include <ctime>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

struct ScoreRecord {
    std::string player_name;
    int score;
    long long timestamp;
    std::string mode;
    std::string date;
};

class GameManager {
public:
    GameManager();   // 【新增】构造函数，初始化数据库
    ~GameManager();  // 【新增】析构函数，关闭数据库

    std::string start_game();
    bool submit_score(const std::string& game_id, const std::string& name, int score,
                      const std::string& mode);
    std::vector<ScoreRecord> get_top_scores(const std::string& mode, int limit = 10);

private:
    std::string generate_game_id();
    void init_db();  // 【新增】初始化建表

    std::mutex mtx_;
    std::unordered_map<std::string, std::time_t> active_sessions_;
    sqlite3* db_;  // 【新增】数据库指针
};