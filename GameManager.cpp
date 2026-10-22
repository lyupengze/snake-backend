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
        // 【修复】改成访问结构体里的 created_at
        if (now - it->second.created_at > GameConfig::kSessionTimeoutSeconds) {
            it = active_sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

std::pair<std::string, std::uint32_t> GameManager::start_game(const std::string& mode) {
    std::lock_guard<std::mutex> lock(session_mtx_);
    cleanup_expired_sessions();

    std::string game_id = generate_game_id();
    std::uint32_t seed = 0;
    std::string date_str = "all";  // 默认经典模式

    std::time_t t = std::time(nullptr);
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &t);
#else
    localtime_r(&t, &tm_now);
#endif
    char date_buf[11];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);

    if (mode == "daily") {
        // 每日挑战，用日期做种子，并记录日期
        date_str = date_buf;
        seed = (tm_now.tm_year + 1900) * 10000 + (tm_now.tm_mon + 1) * 100 + tm_now.tm_mday;
    } else {
        // 经典模式，真随机
        std::random_device rd;
        seed = rd();
    }

    // 存入 Session 时把 date 也带上
    active_sessions_[game_id] = {t, seed, mode, date_str, false};
    return {game_id, seed};
}

bool GameManager::submit_score(const std::string& game_id, const std::string& name, int score,
                               const std::vector<Action>& actions) {
    if (name.empty() || name.length() > GameConfig::kMaxNameLength)
        return false;
    if (actions.size() > 10000)
        return false;  // 防DoS

    std::uint32_t real_seed = 0;
    std::string mode = "";
    std::string session_date = "all";
    std::time_t session_created_at = 0;
    {
        std::lock_guard<std::mutex> lock(session_mtx_);
        auto it = active_sessions_.find(game_id);
        if (it == active_sessions_.end() || it->second.submitted)
            return false;

        real_seed = it->second.seed;
        mode = it->second.mode;
        session_date = it->second.date;
        session_created_at = it->second.created_at;
        it->second.submitted = true;  // 标记已提交，防止重放
    }

    int max_tick = 0;
    if (!actions.empty()) {
        max_tick = actions.back().tick;
    }
    double expected_duration = max_tick * 0.15;

    std::time_t now = std::time(nullptr);
    int actual_duration = static_cast<int>(std::difftime(now, session_created_at));

    // 如果物理耗时远小于预期耗时（允许3秒网络延迟），说明是录播回放，直接拒绝
    if (actual_duration < expected_duration - 3) {
        std::cerr << "作弊拦截: 时间悖论! 声称操作了 " << max_tick << " 帧(约" << expected_duration
                  << "秒)，但物理时间只过了 " << actual_duration << " 秒" << std::endl;
        return false;
    }

    // 核心：必须用 real_seed 跑重放
    int calc_score = ReplayEngine::run(real_seed, actions);
    if (calc_score < 0 || calc_score != score) {
        std::cerr << "作弊拦截: 声称 " << score << " 分, 实际 " << calc_score << " 分" << std::endl;
        return false;
    }

    // ... 接下来的写数据库逻辑保持原样（用 session 里的 mode）...
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif

    std::string date_str = session_date;

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