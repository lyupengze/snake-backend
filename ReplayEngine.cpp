#include "ReplayEngine.h"

/**
 * 内部使用的坐标结构
 */
struct Point {
    int x, y;
    bool operator==(const Point& o) const {
        return x == o.x && y == o.y;
    }
};

/**
 * 服务器权威重放引擎
 *
 * 根据玩家提交的操作序列 (actions) 和开局时下发的 seed，在服务器端完整跑一遍游戏过程。
 * 如果算出来的最终得分与玩家声称的得分不一致，则判定为作弊。
 *
 * @param seed 开局时下发的权威随机种子
 * @param actions 玩家提交的操作序列 (tick 递增，dir 为方向)
 * @return int 真实计算得出的分数；如果触发恶意构造的极限帧数，返回 -1
 */
int ReplayEngine::run(uint32_t seed, const std::vector<Action>& actions) {
    std::vector<Point> snake = {{200, 200}};
    Point food;
    std::string dir = "RIGHT";
    int calc_score = 0;
    bool is_dead = false;

    // ==========================================
    // 核心算法：伪随机数生成器 (PRNG)
    // ==========================================
    // [警告] 必须与前端 src/game/PRNG.js 中的 Mulberry32 算法保持 1:1 绝对一致！
    // 任何修改（包括运算顺序、溢出处理）都会导致前后端食物生成序列错位。
    // 一旦错位，正常玩家的分数也会因为重放不一致而被 403 拒绝。
    uint32_t a = seed;
    auto nextRand = [&a]() {
        a += 0x6D2B79F5u;  // 状态推进
        uint32_t t = a;
        // 32 位无符号整数乘法，模拟前端的 Math.imul 溢出行为
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return t ^ (t >> 14);
    };

    // ==========================================
    // 内部逻辑：生成食物
    // ==========================================
    auto spawn_food = [&]() {
        std::vector<Point> free_cells;
        // [警告] 遍历顺序（外层 x，内层 y）必须与前端 SnakeEngine.js 的 spawnFood 完全一致！
        // 否则同样长度的数组，前端随机索引取到的格子与后端完全不同。
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

        // 如果全屏被填满（触发通关），直接标记死亡
        if (free_cells.empty()) {
            is_dead = true;
            return;
        }
        // 消耗一次随机数，取模获得食物位置
        food = free_cells[nextRand() % free_cells.size()];
    };

    spawn_food();

    int current_tick = 0;
    size_t action_idx = 0;

    // [防御] 限制最大帧数，防止恶意构造超大 tick 导致服务器死循环 (DoS)
    const int MAX_TICKS = 10000;

    while (!is_dead && current_tick < MAX_TICKS) {
        // 1. 应用玩家操作 (每个 tick 最多消费一个动作)
        if (action_idx < actions.size() && actions[action_idx].tick == current_tick) {
            std::string new_dir = actions[action_idx].dir;

            // 判断是否反向移动（贪吃蛇不能直接掉头）
            bool is_opposite =
                (dir == "RIGHT" && new_dir == "LEFT") || (dir == "LEFT" && new_dir == "RIGHT") ||
                (dir == "UP" && new_dir == "DOWN") || (dir == "DOWN" && new_dir == "UP");

            // 如果不是反向，才更新方向
            if (!is_opposite) {
                dir = new_dir;
            }
            action_idx++;
        }

        // 2. 计算新蛇头坐标
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

        // 3. 死亡判定 (撞墙)
        if (new_head.x < 0 || new_head.x >= 400 || new_head.y < 0 || new_head.y >= 400) {
            is_dead = true;
            break;
        }

        // 4. 死亡判定 (撞自己)
        bool ate_food = (new_head.x == food.x && new_head.y == food.y);

        // [警告] 如果没吃到食物，尾巴会同时移开，因此新蛇头撞到当前蛇尾是合法的。
        // 这里的 check_len 逻辑必须与前端 slice(0, -1) 完全等价。
        size_t check_len = ate_food ? snake.size() : snake.size() - 1;

        for (size_t i = 0; i < check_len; ++i) {
            if (snake[i].x == new_head.x && snake[i].y == new_head.y) {
                is_dead = true;
                break;
            }
        }
        if (is_dead)
            break;

        // 5. 更新蛇身与分数
        snake.insert(snake.begin(), new_head);
        if (new_head == food) {
            calc_score++;
            spawn_food();
        } else {
            snake.pop_back();
        }

        current_tick++;
    }

    // [防御] 如果达到最大帧数还未死，说明是恶意重放或极限卡BUG，返回 -1 判为作弊
    if (current_tick >= MAX_TICKS) {
        return -1;
    }

    return calc_score;
}