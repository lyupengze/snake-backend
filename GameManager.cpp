#include "GameManager.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

GameManager::GameManager() {
    init_db();
}

GameManager::~GameManager() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void GameManager::init_db() {
#ifdef _WIN32
    const char* db_path = "snake.db";
#else
    const char* db_path = "/app/data/snake.db";
#endif

    int rc = sqlite3_open(db_path, &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "无法打开数据库: " << sqlite3_errmsg(db_) << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        exit(1);
    }

    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char* sql =
        "CREATE TABLE IF NOT EXISTS scores ("
        "name TEXT, mode TEXT, date TEXT, score INTEGER, timestamp INTEGER, "
        "PRIMARY KEY(name, mode, date));";

    int rc_exec = sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
    if (rc_exec != SQLITE_OK) {
        std::cerr << "建表失败: " << sqlite3_errmsg(db_) << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        exit(1);
    }
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

void GameManager::cleanup_expired_sessions() {
    std::time_t now = std::time(nullptr);
    for (auto it = active_sessions_.begin(); it != active_sessions_.end();) {
        if (now - it->second > GameConfig::kSessionTimeoutSeconds) {
            it = active_sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string GameManager::start_game() {
    std::lock_guard<std::mutex> lock(session_mtx_);
    cleanup_expired_sessions();

    std::string game_id = generate_game_id();
    active_sessions_[game_id] = std::time(nullptr);
    return game_id;
}

bool GameManager::submit_score(const std::string& game_id, const std::string& name, int score,
                               const std::string& mode) {
    // 【修复 1】校验 name 和 mode 长度/合法性
    if (name.empty() || name.length() > GameConfig::kMaxNameLength)
        return false;
    if (mode != "classic" && mode != "daily")
        return false;

    std::time_t now = std::time(nullptr);
    int actual_duration = 0;

    {
        std::lock_guard<std::mutex> lock(session_mtx_);
        auto it = active_sessions_.find(game_id);
        if (it == active_sessions_.end())
            return false;

        actual_duration = static_cast<int>(std::difftime(now, it->second));
        active_sessions_.erase(it);
    }

    if (score < 0 || score > GameConfig::kMaxScore || actual_duration < 1)
        return false;
    if (static_cast<double>(score) / actual_duration > GameConfig::kMaxScorePerSecond)
        return false;

    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    char date_buf[11];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);
    std::string today_str = date_buf;
    std::string date_str = (mode == "daily") ? today_str : "all";

    std::lock_guard<std::mutex> db_lock(db_mtx_);

    const char* sql =
        "INSERT INTO scores (name, mode, date, score, timestamp) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(name, mode, date) DO UPDATE SET score = excluded.score, timestamp = "
        "excluded.timestamp "
        "WHERE excluded.score > scores.score;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, date_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, score);
    sqlite3_bind_int64(stmt, 5, now);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::vector<ScoreRecord> GameManager::get_top_scores(const std::string& mode, int limit) {
    limit = std::clamp(limit, 1, 100);
    std::lock_guard<std::mutex> db_lock(db_mtx_);
    std::vector<ScoreRecord> top;

    std::time_t t = std::time(nullptr);
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &t);
#else
    localtime_r(&t, &tm_now);
#endif

    char date_buf[11];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);
    std::string date_filter = (mode == "daily") ? date_buf : "all";

    const char* sql =
        "SELECT name, score, timestamp FROM scores WHERE mode = ? AND date = ? ORDER BY score DESC "
        "LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, mode.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, date_filter.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ScoreRecord record;
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            record.player_name = text ? reinterpret_cast<const char*>(text) : "Unknown";
            record.score = sqlite3_column_int(stmt, 1);
            record.timestamp = sqlite3_column_int64(stmt, 2);
            record.mode = mode;
            record.date = date_filter;
            top.push_back(record);
        }
    }
    sqlite3_finalize(stmt);

    return top;
}