#ifndef GAMECORE_H
#define GAMECORE_H

#include <QObject>
#include <QVector>
#include <QPoint>
#include <QByteArray>

#include "netprotocol.h"

// 纯游戏逻辑（不依赖 UI），单人/主机/客户端共用
class GameCore : public QObject
{
    Q_OBJECT

signals:
    void foodEaten(int player, quint8 type);   // 吃到食物/道具（联机客户端由状态帧驱动，不触发）

public:
    // 食物/道具类型
    enum FoodType : quint8 {
        FoodNormal  = 0,   // 红点 +1
        FoodGold    = 1,   // 金色大食物 +3（限时）
        FoodBad     = 2,   // 蓝色 -1（惩罚）
        FoodSpeed   = 3,   // 道具：加速（5s）
        FoodSlow    = 4,   // 道具：减速（5s）
        FoodReverse = 5,   // 道具：方向反向（4s）
        FoodShrink  = 6,   // 道具：身体减半
    };

    struct Food {
        QPoint pos;
        quint8 type = FoodNormal;
        int lifeTicks = -1;   // 剩余存活 tick，-1 = 永久
    };

    struct Snake {
        QVector<QPoint> body;   // first 是蛇头
        QVector<QPoint> prev;   // 上一帧蛇身（插值用）
        int dir = Dir::Right;   // 当前方向
        int nextDir = Dir::Right;
        int score = 0;
        bool alive = true;
        QString name;           // 玩家自定义名字
        int speedTicks = 0;     // 加速剩余 tick（每 tick 走 2 步）
        int slowTicks = 0;      // 减速剩余 tick（隔 tick 走 1 步）
        int reverseTicks = 0;   // 方向反向剩余 tick（按键取反）
    };

    explicit GameCore(int gridW, int gridH, QObject *parent = nullptr);

    void reset(int playerCount);          // 初始化多蛇
    void setInput(int player, int dir);   // 设置玩家方向输入
    void setName(int player, const QString &name);
    QString name(int player) const;
    void tick();                          // 逻辑一步（单人/主机调用）

    // 网络状态同步
    QByteArray stateBytes() const;
    void applyState(const QByteArray &data);   // 客户端接收服务器状态

    // 查询
    int gridW() const { return m_gridW; }
    int gridH() const { return m_gridH; }
    int playerCount() const { return m_snakes.size(); }
    const Snake &snake(int p) const { return m_snakes.at(p); }
    int foodCount() const { return m_foods.size(); }
    const Food &foodAt(int i) const { return m_foods.at(i); }
    // 调试/测试用：直接设定某个食物的位置与类型
    void setFood(int i, const QPoint &pos, quint8 type, int lifeTicks = -1) {
        if (i >= 0 && i < m_foods.size()) {
            m_foods[i].pos = pos; m_foods[i].type = type; m_foods[i].lifeTicks = lifeTicks;
        }
    }
    bool gameOver() const { return m_gameOver; }
    bool started() const { return m_started; }
    void setStarted(bool s) { m_started = s; }
    bool paused() const { return m_paused; }
    void setPaused(bool p) { m_paused = p; }
    void killPlayer(int p);   // 断线等场景标记死亡
    QVector<int> ranking() const;   // 排名：存活优先，其次分数

private:
    void spawnFoodAt(int index);
    bool isBlank(const QPoint &p) const;
    void stepSnake(int p);   // 单蛇：移动+碰撞+吃食物（加速道具连走 2 步）
    bool stepOnce(int p);    // 单蛇走一步，返回是否存活
    void applyFood(int p, quint8 type);   // 应用食物/道具效果

    int m_gridW;
    int m_gridH;
    QVector<Snake> m_snakes;
    QVector<Food> m_foods;
    QVector<bool> m_eaten;  // 本 tick 已被吃掉的食物标记（防加速双步重复吃）
    int m_tickCount = 0;    // 全局步数（减速蛇隔步判定用）
    bool m_started = false;
    bool m_gameOver = false;
    bool m_paused = false;
};

#endif // GAMECORE_H
