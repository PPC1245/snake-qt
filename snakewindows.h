#ifndef SNAKEWINDOWS_H
#define SNAKEWINDOWS_H

#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QPoint>
#include <QSettings>
#include <QLabel>
#include <QPixmap>
#include <QElapsedTimer>

#include "gamecore.h"

namespace Ui {
class SnakeWindows;
}

class NetServer;
class NetClient;

// 在 exe 同级 / 上级 / 当前目录查找素材文件（Music.wav、bmp.png 等）
QString findAssetFile(const QString &fileName);

class SnakeWindows : public QWidget
{
    Q_OBJECT

public:
    enum class Mode { Single, Local, Host, Client };

    explicit SnakeWindows(Mode mode, const QString &playerName,
                          const QString &host = QString(),
                          quint16 port = 12345,
                          int playerCount = 1, QWidget *parent = nullptr);
    ~SnakeWindows();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void tick();       // 逻辑步进（单人/主机）
    void blink();      // 食物闪烁
    void animate();    // 60fps 平滑动画
    void pollLocalInput(); // 单机多人：轮询键盘状态
    void onHostInput(int playerId, int dir);      // 服务器收到客户端输入
    void onClientState(const QByteArray &payload); // 客户端收到状态
    void onWelcome(int playerId);                  // 客户端获得编号
    void onStartReceived(int playerCount, int gridW, int gridH);
    void onNetworkError(const QString &message);
    void onClientLeft(int playerId);
    void onPong(quint32 rttMs);                    // 网络延迟
    void onConnectTimeout();                      // 连接超时提示
    void onFoodEaten(int player, quint8 type);    // 吃到食物播放音效

private:
    static constexpr int CELL_SIZE = 20;
    static constexpr int GRID_W = 60;
    static constexpr int GRID_H = 40;
    static constexpr int HUD_HEIGHT = 56;
    static constexpr int MAX_PLAYERS = 4;

    void setupSingle();
    void setupLocal(int players);
    void setupHost(quint16 port);
    void setupClient(const QString &host, quint16 port);
    void startHostGame();
    void updateHud();
    void updateStatus(const QString &text);
    void updateFpsLabel();
    void setupHudLabels(int placeholder, bool showPing);
    void createPlayerLabels(int count, bool colored, bool beforeHint = false); // 统一创建玩家分数标签
    void setupMusic();
    void applyMusicVolume();
    QPointF segmentPos(int player, int index, double t) const;
    void drawSnake(QPainter &painter, int player, bool dead);
    void drawEyes(QPainter &painter, const GameCore::Snake &s,
                  const QPointF &headPos, bool dead);
    void drawName(QPainter &painter, int player, const QPointF &headPos, bool dead);

    Ui::SnakeWindows *ui;
    Mode m_mode;
    GameCore *m_core;
    NetServer *m_server;
    NetClient *m_client;

    QTimer m_timer;       // 主循环（140ms 走一格）
    QTimer m_blinkTimer;  // 食物闪烁
    QTimer m_animTimer;   // 动画刷新（16ms）
    QElapsedTimer m_animClock;   // 实时时钟（插值进度的时间基准）
    qint64 m_animLastMs = 0;     // 上一帧时刻
    QTimer m_inputTimer;  // 单机多人键盘轮询（16ms）
    double m_animProgress;

    int m_localId;        // 本机玩家编号（主机=0，客户端由服务器分配）
    int m_localPlayers = 1; // 单机多人玩家人数
    int m_highScore;
    bool m_paused;
    bool m_foodVisible;

    QString m_playerName; // 本机玩家名字
    int m_musicVolume;    // 0-100（持久化）

    bool m_showFps;       // 是否显示 FPS（持久化）
    int m_frameCount = 0;
    int m_frameElapsedMs = 0;
    int m_fps = 0;
    QLabel *m_fpsLabel = nullptr;
    QLabel *m_pingLabel = nullptr;
    quint32 m_lastRtt = 0;

    QVector<QLabel *> m_playerLabels;
    QPixmap m_bgPixmap;        // 游戏背景图（编入 exe）
};

#endif // SNAKEWINDOWS_H
