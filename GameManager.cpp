#include "GameManager.h"

#include <algorithm>

std::string GameManager::generate_game_id() {
    // 现代 C++ 随机数，解决 rand() 的多线程竞争和分布不均
    thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dis(0, 61);
    const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    std::string id;
    id.reserve(16);
    for (int i = 0; i < 16; ++i) {
        id += alphanum[dis(gen)];
    }
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

    // 防重复提交，立刻作废该 Session
    active_sessions_.erase(it);

    // 严格反作弊校验：防负数、防超高分、防 0 秒速通
    if (score < 0 || score > 500 || actual_duration < 1)
        return false;

    // 修复整数除法坑，强制转 double
    if (static_cast<double>(score) / actual_duration > 50.0)
        return false;

    leaderboard_.push_back({name, score, now});
    std::sort(leaderboard_.begin(), leaderboard_.end(),
              [](const ScoreRecord& a, const ScoreRecord& b) { return a.score > b.score; });
    return true;
}

std::vector<ScoreRecord> GameManager::get_top_scores(int limit) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<ScoreRecord> top;
    int count = std::min(static_cast<int>(leaderboard_.size()), limit);
    top.reserve(count);
    for (int i = 0; i < count; ++i) {
        top.push_back(leaderboard_[i]);
    }
    return top;
}