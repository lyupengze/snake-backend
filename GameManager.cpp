#include "GameManager.h"

#include <algorithm>
#include <iostream>

GameManager::GameManager() {
    init_db();
}

GameManager::~GameManager() {
    if (db_)
        sqlite3_close(db_);
}

void GameManager::init_db() {
    // 数据库文件将存放在 /app/data/snake.db（后面通过 Docker 挂载出来）
    int rc = sqlite3_open("/app/data/snake.db", &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "无法打开数据库: " << sqlite3_errmsg(db_) << std::endl;
        return;
    }

    // 建表：名字 (name) 作为主键，保证去重
    const char* sql =
        "CREATE TABLE IF NOT EXISTS scores ("
        "name TEXT PRIMARY KEY, "
        "score INTEGER, "
        "timestamp INTEGER);";
    sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
}

std::string GameManager::generate_game_id() {
    thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dis(0, 61);
    const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string id;
    id.reserve(16);
    for (int i = 0; i < 16; ++i)
        id += alphanum[dis(gen)];
    return id;
}

std::string GameManager::start_game() {
    std::string game_id = generate_game_id();
    std::lock_guard<std::mutex> lock(mtx_);
    active_sessions_[game_id] = std::time(nullptr);
    return game_id;
}

bool GameManager::submit_score(const std::string& game_id, const std::string& name, int score) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = active_sessions_.find(game_id);
    if (it == active_sessions_.end())
        return false;

    std::time_t now = std::time(nullptr);
    int actual_duration = static_cast<int>(std::difftime(now, it->second));
    active_sessions_.erase(it);

    if (score < 0 || score > 500 || actual_duration < 1)
        return false;
    if (static_cast<double>(score) / actual_duration > 50.0)
        return false;

    // 【核心魔法】如果名字已存在，只有当新分数 > 老分数时，才更新数据
    std::string sql =
        "INSERT INTO scores (name, score, timestamp) VALUES ('" + name + "', " +
        std::to_string(score) + ", " + std::to_string(now) +
        ") "
        "ON CONFLICT(name) DO UPDATE SET score = excluded.score, timestamp = excluded.timestamp "
        "WHERE excluded.score > scores.score;";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL 错误: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

std::vector<ScoreRecord> GameManager::get_top_scores(int limit) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<ScoreRecord> top;

    std::string sql = "SELECT name, score, timestamp FROM scores ORDER BY score DESC LIMIT " +
                      std::to_string(limit) + ";";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ScoreRecord record;
            record.player_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            record.score = sqlite3_column_int(stmt, 1);
            record.timestamp = sqlite3_column_int64(stmt, 2);
            top.push_back(record);
        }
        sqlite3_finalize(stmt);
    }

    return top;
}