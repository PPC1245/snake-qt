// 道具/食物逻辑的代码级测试（不进发布包，仅开发验证）
#include "gamecore.h"
#include "netprotocol.h"
#include <QCoreApplication>
#include <QPoint>
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS %s\n", msg); \
    else { printf("FAIL %s\n", msg); ++g_fail; } \
} while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // 1. 食物数量 = 玩家数 x2
    {
        GameCore c(60, 40);
        c.reset(1);
        CHECK(c.foodCount() == 2, "single foodCount=2");
    }
    {
        GameCore c(60, 40);
        c.reset(2);
        CHECK(c.foodCount() == 4, "multi foodCount=4");
    }

    // 2. 普通食物 +1 分
    {
        GameCore c(60, 40);
        c.reset(1);
        c.setFood(0, QPoint(31, 20), GameCore::FoodNormal);   // 蛇头(30,20)朝右前方
        c.tick();
        CHECK(c.snake(0).score == 1, "normal +1");
        CHECK(c.snake(0).body.size() == 4, "normal grow body");
        // 被吃的食物必须重刷到新位置（不在原格）
        CHECK(c.foodAt(0).pos != QPoint(31, 20), "eaten food respawns");
    }

    // 2.5 吃到食物触发 foodEaten 信号（音效链路）
    {
        GameCore c(60, 40);
        c.reset(1);
        int eatenCount = 0;
        QObject::connect(&c, &GameCore::foodEaten,
                         [&eatenCount](int, quint8) { ++eatenCount; });
        c.setFood(0, QPoint(31, 20), GameCore::FoodNormal);
        c.tick();
        CHECK(eatenCount == 1, "foodEaten signal emitted on eat");
        c.setFood(0, QPoint(32, 20), GameCore::FoodNormal);
        c.tick();
        CHECK(eatenCount == 2, "foodEaten emits again");
    }

    // 3. 金色 +3
    {
        GameCore c(60, 40);
        c.reset(1);
        c.setFood(0, QPoint(31, 20), GameCore::FoodGold);
        c.tick();
        CHECK(c.snake(0).score == 3, "gold +3");
    }

    // 4. 坏食物 -1（不低于 0）
    {
        GameCore c(60, 40);
        c.reset(1);
        c.setFood(0, QPoint(31, 20), GameCore::FoodBad);
        c.tick();
        CHECK(c.snake(0).score == 0, "bad -> 0 (floor)");
        c.setFood(0, QPoint(32, 20), GameCore::FoodBad);   // 吃第二个坏
        c.tick();
        CHECK(c.snake(0).score == 0, "bad stays 0");
    }

    // 5. 加速道具：speedTicks=35 + 双步
    {
        GameCore c(60, 40);
        c.reset(1);
        c.setFood(0, QPoint(31, 20), GameCore::FoodSpeed);
        c.tick();
        CHECK(c.snake(0).speedTicks == 35, "speed set 35");
        // 吃到的当 tick 已双步：头 30->31(吃)->32；下一步再双步 -> 34
        CHECK(c.snake(0).body.first() == QPoint(32, 20), "speed double step on eat tick");
        c.tick();
        CHECK(c.snake(0).body.first() == QPoint(34, 20), "speed double step");
    }

    // 6. 减速道具：slowTicks=35 且隔 tick 走
    {
        GameCore c(60, 40);
        c.reset(1);
        c.setFood(0, QPoint(31, 20), GameCore::FoodSlow);
        c.tick();
        CHECK(c.snake(0).slowTicks == 35, "slow set 35");
        QPoint before = c.snake(0).body.first();
        c.tick();   // slow 时奇数 tick 跳过
        // 该 tick 可能跳步，验证后续非跳步 tick 前进
        bool moved = false;
        for (int i = 0; i < 4 && !moved; ++i) {
            c.tick();
            moved = c.snake(0).body.first() != before;
        }
        CHECK(moved, "slow eventually moves");
    }

    // 7. 反向道具：方向取反 (dir+2)%4；Dir: Up=0 Right=1 Down=2 Left=3
    {
        GameCore c(60, 40);
        c.reset(1);
        c.setFood(0, QPoint(31, 20), GameCore::FoodReverse);
        c.tick();
        CHECK(c.snake(0).reverseTicks == 28, "reverse set 28");
        // 蛇朝右(1)，反向时按上(0) -> (0+2)%4 = 2 = Down（上下互换）
        c.setInput(0, Dir::Up);
        CHECK(c.snake(0).nextDir == Dir::Down, "reverse flips up->down");
        // 反向时按左(3) -> (3+2)%4 = 1 = Right（左右互换，与当前朝右相同向）
        c.setInput(0, Dir::Left);
        CHECK(c.snake(0).nextDir == Dir::Right, "reverse flips left->right");
    }

    // 8. 缩短道具：身体减半（最小 2）
    {
        GameCore c(60, 40);
        c.reset(1);
        for (int i = 0; i < 5; ++i) {   // 吃 5 个普通食物长到 8 节
            c.setFood(0, QPoint(31 + i, 20), GameCore::FoodNormal);
            c.tick();
        }
        const int lenBefore = c.snake(0).body.size();
        CHECK(lenBefore == 8, "body reached 8");
        c.setFood(0, QPoint(36, 20), GameCore::FoodShrink);
        c.tick();
        CHECK(c.snake(0).body.size() == 4, "shrink half to 4");
    }

    // 9. 撞墙死亡判定
    {
        GameCore c(60, 40);
        c.reset(1);
        // 蛇头(30,20)朝右，直行 35 步必撞右墙（60 格地图，x 到 60 即死）
        for (int i = 0; i < 35; ++i) {
            c.setFood(0, QPoint(1000, 1000), GameCore::FoodNormal); // 挪走食物不挡路
            c.tick();
        }
        CHECK(!c.snake(0).alive, "wall crash kills");
        CHECK(c.gameOver(), "single gameover on death");
    }

    printf("\n%d FAILURES\n", g_fail);
    return g_fail ? 1 : 0;
}
