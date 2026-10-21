#pragma once
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
};

class GameManager {
public:
    std::string start_game();
    bool submit_score(const std::string& game_id, const std::string& name, int score);
    std::vector<ScoreRecord> get_top_scores(int limit = 10);

private:
    std::string generate_game_id();

    std::mutex mtx_;
    std::unordered_map<std::string, std::time_t> active_sessions_;
    std::vector<ScoreRecord> leaderboard_;
};