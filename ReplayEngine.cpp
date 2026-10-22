#include "ReplayEngine.h"

// 内部使用的坐标结构
struct Point {
    int x, y;
    bool operator==(const Point& o) const {
        return x == o.x && y == o.y;
    }
};

int ReplayEngine::run(uint32_t seed, const std::vector<Action>& actions) {
    std::vector<Point> snake = {{200, 200}};
    Point food;
    std::string dir = "RIGHT";
    int calc_score = 0;
    bool is_dead = false;

    // 与前端 1:1 完全一致的伪随机数生成器
    uint32_t a = seed;
    auto nextRand = [&a]() {
        a += 0x6D2B79F5u;
        uint32_t t = a;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return t ^ (t >> 14);
    };

    // 生成初始食物
    auto spawn_food = [&]() {
        std::vector<Point> free_cells;
        for (int x = 0; x < 20; x++) {
            for (int y = 0; y < 20; y++) {
                int px = x * 20, py = y * 20;
                bool occ = false;
                for (auto& s : snake)
                    if (s.x == px && s.y == py) {
                        occ = true;
                        break;
                    }
                if (!occ)
                    free_cells.push_back({px, py});
            }
        }
        if (free_cells.empty()) {
            is_dead = true;
            return;
        }
        food = free_cells[nextRand() % free_cells.size()];
    };

    spawn_food();

    int current_tick = 0;
    size_t action_idx = 0;

    // 【新增】限制最大帧数，防止恶意构造 tick 导致死循环
    const int MAX_TICKS = 10000;

    while (!is_dead && current_tick < MAX_TICKS) {
        // 应用玩家操作
        if (action_idx < actions.size() && actions[action_idx].tick == current_tick) {
            std::string new_dir = actions[action_idx].dir;
            bool is_opposite =
                (dir == "RIGHT" && new_dir == "LEFT") || (dir == "LEFT" && new_dir == "RIGHT") ||
                (dir == "UP" && new_dir == "DOWN") || (dir == "DOWN" && new_dir == "UP");
            if (!is_opposite)
                dir = new_dir;
            action_idx++;
        }

        // 移动
        Point head = snake[0];
        Point new_head = head;
        if (dir == "RIGHT")
            new_head.x += 20;
        if (dir == "LEFT")
            new_head.x -= 20;
        if (dir == "UP")
            new_head.y -= 20;
        if (dir == "DOWN")
            new_head.y += 20;

        // 死亡判定
        if (new_head.x < 0 || new_head.x >= 400 || new_head.y < 0 || new_head.y >= 400) {
            is_dead = true;
            break;
        }
        bool ate_food = (new_head.x == food.x && new_head.y == food.y);
        size_t check_len = ate_food ? snake.size() : snake.size() - 1;

        for (size_t i = 0; i < check_len; ++i) {
            if (snake[i].x == new_head.x && snake[i].y == new_head.y) {
                is_dead = true;
                break;
            }
        }
        if (is_dead)
            break;

        // 吃食物
        snake.insert(snake.begin(), new_head);
        if (new_head == food) {
            calc_score++;
            spawn_food();
        } else {
            snake.pop_back();
        }
        current_tick++;
    }

    // 【新增】如果达到最大帧数还未死，说明是恶意重放或极限卡BUG，返回 -1 判为作弊
    if (current_tick >= MAX_TICKS) {
        return -1;
    }

    return calc_score;
}