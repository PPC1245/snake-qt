#include "gamecore.h"
#include "netprotocol.h"

#include <QRandomGenerator>
#include <QDataStream>
#include <algorithm>

namespace {
// 方向位移表：Up / Right / Down / Left（与 netprotocol.h 的 Dir 枚举一致）
const QPoint kDelta[4] = {
    QPoint(0, -1), QPoint(1, 0), QPoint(0, 1), QPoint(-1, 0) };
} // namespace

GameCore::GameCore(int gridW, int gridH, QObject *parent)
    : QObject(parent)
    , m_gridW(gridW)
    , m_gridH(gridH)
{
}

void GameCore::reset(int playerCount)
{
    m_snakes.clear();
    m_foods.clear();
    m_tickCount = 0;
    m_started = true;
    m_gameOver = false;

    const int cx = m_gridW / 2;
    const int cy = m_gridH / 2;
    const int n = qMax(1, playerCount);

    // 出生点表：头位置 + 朝向（单人中央朝右；多人四边朝中心、错开防开局相撞）
    static const struct { QPoint head; int dir; } kStarts[4] = {
        { QPoint(6, cy - 2), Dir::Right },       { QPoint(m_gridW - 7, cy + 2), Dir::Left },
        { QPoint(cx - 2, 6), Dir::Down },        { QPoint(cx + 2, m_gridH - 7), Dir::Up },
    };
    for (int p = 0; p < n; ++p) {
        const QPoint head = (n == 1) ? QPoint(cx, cy) : kStarts[p].head;
        const int dir = (n == 1) ? Dir::Right : kStarts[p].dir;
        Snake s;
        s.body.append(head);                  // 头
        s.body.append(head - kDelta[dir]);    // 身体第 2 节（朝反方向延伸）
        s.body.append(head - kDelta[dir] * 2); // 身体第 3 节
        s.dir = dir;
        s.nextDir = dir;
        s.name = QStringLiteral("P%1").arg(p + 1);
        m_snakes.append(s);
    }

    // 食物数量 = 玩家数 × 2（大地图保持竞争）
    const int foodCount = qMax(2, playerCount * 2);
    for (int i = 0; i < foodCount; ++i)
        spawnFoodAt(i);
}

void GameCore::setInput(int player, int dir)
{
    if (player < 0 || player >= m_snakes.size())
        return;
    Snake &s = m_snakes[player];
    if (!s.alive)
        return;
    // 反向道具：左右/上下互换
    if (s.reverseTicks > 0)
        dir = (dir + 2) % 4;
    // 防反向
    if ((s.dir == Dir::Up    && dir == Dir::Down)
        || (s.dir == Dir::Down  && dir == Dir::Up)
        || (s.dir == Dir::Left  && dir == Dir::Right)
        || (s.dir == Dir::Right && dir == Dir::Left))
        return;
    s.nextDir = dir;
}

void GameCore::setName(int player, const QString &name)
{
    if (player >= 0 && player < m_snakes.size() && !name.isEmpty())
        m_snakes[player].name = name;
}

QString GameCore::name(int player) const
{
    if (player < 0 || player >= m_snakes.size())
        return QString();
    return m_snakes.at(player).name;
}

void GameCore::tick()
{
    if (!m_started || m_gameOver || m_paused)
        return;
    ++m_tickCount;

    // 0. 重置本 tick 吃掉标记（防越界 + 防加速双步重复吃）
    m_eaten.assign(m_foods.size(), false);

    // 0. 道具效果倒计时
    for (int p = 0; p < m_snakes.size(); ++p) {
        Snake &s = m_snakes[p];
        if (s.speedTicks > 0)
            --s.speedTicks;
        if (s.slowTicks > 0)
            --s.slowTicks;
        if (s.reverseTicks > 0)
            --s.reverseTicks;
    }

    // 0.5 限时食物倒计时（过期重刷）
    for (int f = 0; f < m_foods.size(); ++f) {
        if (m_foods[f].lifeTicks > 0 && --m_foods[f].lifeTicks == 0)
            spawnFoodAt(f);
    }

    // 1. 每条蛇走一步；减速蛇隔 tick 走（全局奇偶对齐）；加速蛇内部双步
    for (int p = 0; p < m_snakes.size(); ++p) {
        if (!m_snakes[p].alive)
            continue;
        if (m_snakes[p].slowTicks > 0 && (m_tickCount & 1))
            continue;
        stepSnake(p);
    }

    // 1.5 重刷本 tick 被吃掉的食物（在原位替换成新食物，地图总数保持不变）
    for (int f = 0; f < m_foods.size(); ++f) {
        if (m_eaten[f])
            spawnFoodAt(f);
    }

    // 2. 结束判定：单人蛇死即结束；多人剩 1 条存活即结束
    int aliveCount = 0;
    for (const Snake &s : m_snakes)
        if (s.alive)
            ++aliveCount;
    if (aliveCount == 0
        || (m_snakes.size() > 1 && aliveCount == 1))
        m_gameOver = true;
}

// 单蛇完整一步：移动 + 碰撞 + 吃食物；加速道具在一步结束后再走一步
void GameCore::stepSnake(int p)
{
    if (!m_snakes[p].alive)
        return;
    m_snakes[p].prev = m_snakes[p].body;   // 插值起点
    if (!stepOnce(p))
        return;
    if (m_snakes[p].speedTicks > 0 && m_snakes[p].alive) {
        m_snakes[p].prev = m_snakes[p].body;   // 双步时以上一步结果作为插值起点
        stepOnce(p);
    }
}

bool GameCore::stepOnce(int p)
{
    Snake &s = m_snakes[p];
    s.dir = s.nextDir;
    QPoint head = s.body.first() + kDelta[qBound<int>(0, s.dir, 3)];
    s.body.prepend(head);

    // 碰撞：撞墙 / 撞任何蛇（含自己的尾巴、别的蛇头）
    if (head.x() < 0 || head.x() >= m_gridW
        || head.y() < 0 || head.y() >= m_gridH) {
        s.alive = false;
        return false;
    }
    for (int q = 0; q < m_snakes.size(); ++q) {
        const Snake &o = m_snakes[q];
        if (!o.alive)
            continue;
        const int startIdx = (q == p) ? 1 : 0;   // 自己从头后开始，别的蛇含头
        for (int i = startIdx; i < o.body.size(); ++i) {
            if (o.body.at(i) == head) {
                s.alive = false;
                return false;
            }
        }
    }

    // 吃食物：吃到则加分/生效且不缩尾；否则移除尾巴
    bool ate = false;
    for (int f = 0; f < m_foods.size(); ++f) {
        if (!m_eaten[f] && m_foods[f].pos == head) {
            m_eaten[f] = true;
            applyFood(p, m_foods[f].type);
            emit foodEaten(p, m_foods[f].type);
            ate = true;
            break;
        }
    }
    if (!ate)
        s.body.removeLast();
    return s.alive;
}

void GameCore::applyFood(int p, quint8 type)
{
    Snake &s = m_snakes[p];
    switch (type) {
    case FoodNormal:
        ++s.score;
        break;
    case FoodGold:
        s.score += 3;
        break;
    case FoodBad:
        s.score = qMax(0, s.score - 1);
        break;
    case FoodSpeed:
        s.speedTicks = 35;      // ≈ 4.9 秒（140ms × 35）
        s.slowTicks = 0;
        break;
    case FoodSlow:
        s.slowTicks = 35;
        s.speedTicks = 0;
        break;
    case FoodReverse:
        s.reverseTicks = 28;    // ≈ 3.9 秒
        break;
    case FoodShrink: {
        const int nl = qMax(2, s.body.size() / 2);
        if (s.body.size() > nl)
            s.body.resize(nl);
        break;
    }
    default:
        break;
    }
}

void GameCore::killPlayer(int p)
{
    if (p >= 0 && p < m_snakes.size())
        m_snakes[p].alive = false;
}

bool GameCore::isBlank(const QPoint &p) const
{
    for (const Snake &s : m_snakes)
        if (s.body.contains(p))
            return false;
    for (const Food &f : m_foods)
        if (f.pos == p)
            return false;
    return true;
}

void GameCore::spawnFoodAt(int index)
{
    // 类型权重表：普通45 金15 蓝10 加速12 减速6 反向6 缩短6；非普通/蓝限时 25 tick
    static const struct { int limit; quint8 type; } kWeights[] = {
        { 45, FoodNormal }, { 60, FoodGold }, { 70, FoodBad },
        { 82, FoodSpeed }, { 88, FoodSlow }, { 94, FoodReverse }, { 100, FoodShrink },
    };
    const int r = QRandomGenerator::global()->bounded(100);
    quint8 type = FoodNormal;
    for (const auto &w : kWeights)
        if (r < w.limit) { type = w.type; break; }
    const int life = (type == FoodNormal || type == FoodBad) ? -1 : 25;

    for (;;) {
        const QPoint p(QRandomGenerator::global()->bounded(m_gridW),
                       QRandomGenerator::global()->bounded(m_gridH));
        if (isBlank(p)) {
            const Food f{ p, type, life };
            if (index < m_foods.size())
                m_foods[index] = f;
            else
                m_foods.append(f);
            return;
        }
    }
}

QVector<int> GameCore::ranking() const
{
    QVector<int> order;
    for (int i = 0; i < m_snakes.size(); ++i)
        order.append(i);
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        const Snake &sa = m_snakes.at(a);
        const Snake &sb = m_snakes.at(b);
        if (sa.alive != sb.alive)
            return sa.alive;
        return sa.score > sb.score;
    });
    return order;
}

// 序列化当前状态（含 MsgState 消息头 + 食物类型/道具效果）
QByteArray GameCore::stateBytes() const
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << quint8(NetMsg::State)
        << quint16(m_gridW) << quint16(m_gridH)
        << quint8(m_snakes.size())
        << quint8(m_started ? 1 : 0)
        << quint8(m_gameOver ? 1 : 0)
        << quint8(m_paused ? 1 : 0)
        << quint8(m_foods.size());
    for (const Food &f : m_foods)
        out << qint16(f.pos.x()) << qint16(f.pos.y())
            << quint8(f.type) << qint16(f.lifeTicks);
    for (const Snake &s : m_snakes) {
        out << quint8(s.alive ? 1 : 0)
            << quint8(s.dir)
            << qint16(s.score)
            << quint16(s.body.size())
            << qint16(s.speedTicks) << qint16(s.slowTicks)
            << qint16(s.reverseTicks);
        for (const QPoint &p : s.body)
            out << qint16(p.x()) << qint16(p.y());
        out << s.name;   // 玩家名字
    }
    return data;
}

// 客户端应用服务器状态（prev 保留上一帧用于插值）
void GameCore::applyState(const QByteArray &data)
{
    QDataStream in(data);
    quint8 msg;
    in >> msg;
    if (msg != NetMsg::State)
        return;

    quint16 gw, gh;
    quint8 count, started, over, paused, fcount;
    in >> gw >> gh >> count >> started >> over >> paused >> fcount;
    m_gridW = gw;
    m_gridH = gh;
    m_started = (started != 0);
    m_gameOver = (over != 0);
    m_paused = (paused != 0);

    m_foods.clear();
    for (int i = 0; i < fcount; ++i) {
        qint16 x, y, life;
        quint8 type;
        in >> x >> y >> type >> life;
        Food f;
        f.pos = QPoint(x, y);
        f.type = type;
        f.lifeTicks = life;
        m_foods.append(f);
    }

    QVector<Snake> next;
    next.reserve(count);
    for (int p = 0; p < count; ++p) {
        quint8 alive, dir;
        qint16 score;
        quint16 blen;
        qint16 st, sl, rv;
        in >> alive >> dir >> score >> blen >> st >> sl >> rv;
        Snake s;
        s.alive = (alive != 0);
        s.dir = dir;
        s.nextDir = dir;
        s.score = score;
        s.speedTicks = st;
        s.slowTicks = sl;
        s.reverseTicks = rv;
        if (p < m_snakes.size())
            s.prev = m_snakes.at(p).body;   // 上一帧作为插值起点
        for (int i = 0; i < blen; ++i) {
            qint16 bx, by;
            in >> bx >> by;
            s.body.append(QPoint(bx, by));
        }
        in >> s.name;
        next.append(s);
    }
    m_snakes = next;
    m_eaten.assign(m_foods.size(), false);
}
