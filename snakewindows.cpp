#include "snakewindows.h"
#include "ui_snakewindows.h"
#include "netserver.h"
#include "netclient.h"
#include "netprotocol.h"
#include <QPainter>
#include <QKeyEvent>
#include <QPair>
#include <QApplication>
#include <QDataStream>
#include <QHBoxLayout>
#include <QCoreApplication>
#include <QDir>
#include <QUrl>
#include <QFile>
#include <QMessageBox>
#include <windows.h>
#include <mmsystem.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
namespace {
    // 设置本进程音频会话音量（0-100），不影响系统音量
    bool setProcessAudioVolume(int percent)
    {
        const float vol = qBound(0, percent, 100) / 100.0f;
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
            return false;
        IMMDeviceEnumerator* enumerator = nullptr;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (FAILED(hr))
            return false;
        IMMDevice* device = nullptr;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        enumerator->Release();
        if (FAILED(hr))
            return false;
        IAudioSessionManager2* manager = nullptr;
        hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&manager));
        device->Release();
        if (FAILED(hr))
            return false;
        IAudioSessionControl* session = nullptr;
        hr = manager->GetAudioSessionControl(nullptr, 0, &session);
        manager->Release();
        if (FAILED(hr))
            return false;
        ISimpleAudioVolume* volume = nullptr;
        hr = session->QueryInterface(IID_PPV_ARGS(&volume));
        session->Release();
        if (FAILED(hr))
            return false;
        hr = volume->SetMasterVolume(vol, nullptr);
        volume->Release();
        CoUninitialize();
        return SUCCEEDED(hr);
    }
}
namespace {
    // 玩家配色（P1~P4）
    const QColor kPlayerColors[4] = {
        QColor(0x3e, 0xd4, 0x8e),
        QColor(0x4e, 0xa1, 0xff),
        QColor(0xff, 0x9f, 0x4a),
        QColor(0xc7, 0x7b, 0xff),
    };
    const QColor kDeadColor(0x4a, 0x4a, 0x58);
    constexpr quint16 kDefaultPort = 12345;
}
// 在 exe 同级 / 上级 / 当前目录查找素材文件
QString findAssetFile(const QString& fileName)
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exeDir + QLatin1Char('/') + fileName,
        exeDir + QStringLiteral("/../") + fileName,
        QDir::current().absoluteFilePath(fileName),
    };
    for (const QString& path : candidates) {
        if (QFile::exists(path))
            return QDir::cleanPath(path);
    }
    return QString();
}
// 窗口初始化：尺寸/图标/设置/三个定时器/按模式分发
SnakeWindows::SnakeWindows(Mode mode, const QString& playerName, const QString& host, quint16 port, int playerCount, QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::SnakeWindows)
    , m_mode(mode)
    , m_core(new GameCore(GRID_W, GRID_H, this))
    , m_server(nullptr)
    , m_client(nullptr)
    , m_animProgress(1.0)
    , m_localId(0)
    , m_localPlayers(playerCount)
    , m_highScore(0)
    , m_paused(false)
    , m_foodVisible(true)
    , m_playerName(playerName.isEmpty() ? QStringLiteral("玩家") : playerName)
{
    ui->setupUi(this);
    m_animClock.start();
    m_animLastMs = 0;
    setFixedSize(GRID_W * CELL_SIZE, HUD_HEIGHT + GRID_H * CELL_SIZE);
    setFocusPolicy(Qt::StrongFocus);
    setWindowIcon(QIcon(QStringLiteral(":/bmp.png")));
    m_bgPixmap.load(QStringLiteral(":/bg.png"));
    connect(m_core, &GameCore::foodEaten, this, &SnakeWindows::onFoodEaten);
    QSettings settings(QStringLiteral("SnakeGame"), QStringLiteral("SnakeGame"));
    m_highScore = settings.value(QStringLiteral("highScore"), 0).toInt();
    m_musicVolume = settings.value(QStringLiteral("musicVolume"), 70).toInt();
    m_showFps = settings.value(QStringLiteral("showFps"), false).toBool();
    const QString hudStyle = QStringLiteral("color:#e8ecf5; font-size:16px; font-weight:bold;");
    ui->scoreLabel->setStyleSheet(hudStyle); ui->highLabel->setStyleSheet(hudStyle); ui->lenLabel->setStyleSheet(hudStyle);
    ui->hintLabel->setStyleSheet(QStringLiteral("color:#8fa1c8; font-size:13px;"));
    setupMusic();
    m_timer.setInterval(140);
    connect(&m_timer, &QTimer::timeout, this, &SnakeWindows::tick);
    m_blinkTimer.setInterval(300);
    connect(&m_blinkTimer, &QTimer::timeout, this, &SnakeWindows::blink);
    m_animTimer.setInterval(16);
    connect(&m_animTimer, &QTimer::timeout, this, &SnakeWindows::animate);
    m_inputTimer.setInterval(16);
    connect(&m_inputTimer, &QTimer::timeout, this, &SnakeWindows::pollLocalInput);
    m_blinkTimer.start();
    m_animTimer.start();
    switch (m_mode) {
    case Mode::Single: setupSingle(); break;
    case Mode::Local: setupLocal(playerCount); break;
    case Mode::Host: setupHost(port); break;
    case Mode::Client: setupClient(host, port); break;
    }
}
// 析构：停止背景音乐
SnakeWindows::~SnakeWindows()
{
    PlaySoundW(nullptr, nullptr, 0);
    delete ui;
}
// 单人模式初始化
void SnakeWindows::setupSingle()
{
    setWindowTitle(QStringLiteral("贪吃蛇 · 单人"));
    m_core->reset(1);
    m_core->setName(0, m_playerName);
    m_timer.start();
    ui->hintLabel->setText(QStringLiteral("方向键 / WASD 移动 · 空格暂停 · 结束按空格重开"));
    setupHudLabels(0, false);
    updateHud();
}
// 单机多人：同屏 2-4 人，各用一组按键，P1 按空格开局
void SnakeWindows::setupLocal(int players)
{
    setWindowTitle(QStringLiteral("贪吃蛇 · 单机多人"));
    m_core->reset(players);
    for (int i = 0; i < players; ++i)
        m_core->setName(i, QStringLiteral("P%1").arg(i + 1));
    m_core->setStarted(false);
    m_inputTimer.start();
    ui->scoreLabel->hide(); ui->highLabel->hide(); ui->lenLabel->hide();
    createPlayerLabels(players, true, true);
    setupHudLabels(0, false);
    updateStatus(QStringLiteral("P1:WASD P2:YGHJ P3:数字8426 P4:方向键 · P1 按空格开始"));
    updateHud();
}
// 创建 FPS / 延迟标签（按设置项显示）
void SnakeWindows::setupHudLabels(int placeholder, bool showPing)
{
    Q_UNUSED(placeholder);
    QHBoxLayout* hud = qobject_cast<QHBoxLayout*>(ui->verticalLayout->itemAt(0)->layout());
    if (!hud)
        return;
    if (m_showFps && !m_fpsLabel) {
        m_fpsLabel = new QLabel(QStringLiteral("FPS 60"), this);
        m_fpsLabel->setStyleSheet(QStringLiteral("color:#8fa1c8; font-size:13px; font-weight:bold;"));
        hud->insertWidget(hud->count() - 2, m_fpsLabel);
    }
    if (showPing && !m_pingLabel) {
        m_pingLabel = new QLabel(QStringLiteral("延迟 0ms"), this);
        m_pingLabel->setStyleSheet(QStringLiteral("color:#8fa1c8; font-size:13px; font-weight:bold;"));
        hud->insertWidget(hud->count() - 2, m_pingLabel);
    }
    if (m_fpsLabel)
        updateFpsLabel();
}
// 创建各玩家分数标签（先清旧标签，重开不重复）
void SnakeWindows::createPlayerLabels(int count, bool colored, bool beforeHint)
{
    QHBoxLayout* hud = qobject_cast<QHBoxLayout*>(ui->verticalLayout->itemAt(0)->layout());
    if (!hud)
        return;
    for (QLabel* label : m_playerLabels) {
        hud->removeWidget(label);
        delete label;
    }
    m_playerLabels.clear();
    for (int i = 0; i < count; ++i) {
        QLabel* label = new QLabel(QStringLiteral("P%1 0").arg(i + 1), this);
        label->setStyleSheet(QStringLiteral("color:%1; font-size:15px; font-weight:bold;").arg(colored ? kPlayerColors[i % 4].name() : QStringLiteral("#8fa1c8")));
        hud->insertWidget(beforeHint ? hud->count() - 2 : i, label);
        m_playerLabels.append(label);
    }
}
// 创建房间：显示可连 IP，等待玩家加入
void SnakeWindows::setupHost(quint16 port)
{
    m_localId = 0;
    m_server = new NetServer(port, this);
    if (!m_server->start()) {
        setWindowTitle(QStringLiteral("贪吃蛇 · 房主"));
        ui->hintLabel->setText(QStringLiteral("房间创建失败：端口被占用"));
        return;
    }
    const QStringList ips = m_server->ipCandidates();
    setWindowTitle(QStringLiteral("贪吃蛇 · 房主 [%1:%2]").arg(ips.value(0, QStringLiteral("127.0.0.1"))).arg(m_server->port()));
    QString ipHint = QStringLiteral("房主 IP（让朋友输入其中一个）：");
    for (int i = 0; i < ips.size(); ++i)
        ipHint += (i == 0 ? QStringLiteral(" %1（推荐）") : QStringLiteral(" %1")).arg(ips.at(i));
    ipHint += QStringLiteral(" · 请确保连了热点或同一 WiFi · 客户端输入 IP");
    connect(m_server, &NetServer::inputFrom, this, &SnakeWindows::onHostInput);
    connect(m_server, &NetServer::clientLeft, this, &SnakeWindows::onClientLeft);
    ui->scoreLabel->hide(); ui->highLabel->hide(); ui->lenLabel->hide();
    createPlayerLabels(MAX_PLAYERS, false);
    updateStatus(ipHint + QStringLiteral(" · 等待玩家加入…（至少 1 人后按空格开始）"));
    setupHudLabels(0, true);
    if (m_pingLabel)
        m_pingLabel->setText(QStringLiteral("延迟 0ms（主机）"));
    updateHud();
}
// 加入房间：连接主机并开始延迟检测
void SnakeWindows::setupClient(const QString& host, quint16 port)
{
    setWindowTitle(QStringLiteral("贪吃蛇 · 联机玩家"));
    m_client = new NetClient(host, port, m_playerName, this);
    connect(m_client, &NetClient::welcomeReceived, this, &SnakeWindows::onWelcome);
    connect(m_client, &NetClient::startReceived, this, &SnakeWindows::onStartReceived);
    connect(m_client, &NetClient::stateReceived, this, &SnakeWindows::onClientState);
    connect(m_client, &NetClient::pongReceived, this, &SnakeWindows::onPong);
    connect(m_client, &NetClient::networkError, this, &SnakeWindows::onNetworkError);
    connect(m_client, &NetClient::connectionTimeout, this, &SnakeWindows::onConnectTimeout);
    ui->scoreLabel->hide(); ui->highLabel->hide(); ui->lenLabel->hide();
    setupHudLabels(0, true);
    m_client->startPing(1000);
    updateStatus(QStringLiteral("正在连接 %1:%2…").arg(host).arg(port));
    m_client->connectToHost();
}
// 主机开始对局：广播人数 + 地图尺寸
void SnakeWindows::startHostGame()
{
    if (m_server->connectedCount() < 1) {
        updateStatus(QStringLiteral("等待玩家加入…（按空格开始）"));
        return;
    }
    const int count = qMin(MAX_PLAYERS, m_server->connectedCount() + 1);
    m_core->reset(count);
    m_core->setName(0, m_playerName);
    for (int id = 1; id < count; ++id) {
        const QString name = m_server->name(id);
        if (!name.isEmpty())
            m_core->setName(id, name);
    }
    m_animProgress = 0.0;
    m_animLastMs = m_animClock.elapsed();
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out << quint8(NetMsg::Start) << quint8(count) << quint16(GRID_W) << quint16(GRID_H);
    m_server->broadcast(payload);
    m_timer.start();
    updateHud();
    updateStatus(QStringLiteral("对局开始！你控制 P1（方向键） · 空格暂停"));
}
// 逻辑步：走一帧（主机同时广播状态）
void SnakeWindows::tick()
{
    if (!m_core->started() || m_core->gameOver())
        return;
    m_core->tick();
    m_animProgress = 0.0;
    m_animLastMs = m_animClock.elapsed();
    updateHud();
    if (m_mode == Mode::Host)
        m_server->broadcast(m_core->stateBytes());
    update();
}
// 食物闪烁
void SnakeWindows::blink()
{
    if (m_core->started() && !m_core->paused() && !m_core->gameOver())
        m_foodVisible = !m_foodVisible;
    update();
}
// 平滑动画 16ms 插值+ FPS 统计
void SnakeWindows::animate()
{
    if (!m_core->started() || m_core->paused() || m_core->gameOver())
        return;
    const qint64 now = m_animClock.elapsed();
    m_animProgress += double(now - m_animLastMs) / double(m_timer.interval());
    m_animLastMs = now;
    if (m_animProgress > 1.0)
        m_animProgress = 1.0;
    if (m_showFps) {
        ++m_frameCount;
        m_frameElapsedMs += 16;
        if (m_frameElapsedMs >= 1000) {
            m_fps = m_frameCount;
            m_frameCount = 0;
            m_frameElapsedMs = 0;
            updateFpsLabel();
        }
    }
    update();
}
// 刷新 FPS 标签
void SnakeWindows::updateFpsLabel()
{
    if (m_fpsLabel)
        m_fpsLabel->setText(QStringLiteral("FPS %1").arg(m_fps));
}
// 客户端收到延迟应答（滑动平均）
void SnakeWindows::onPong(quint32 rttMs)
{
    const quint32 r = qMin<quint32>(rttMs, 999);
    m_lastRtt = m_lastRtt ? (m_lastRtt * 3 + r) / 4 : r;
    if (m_pingLabel)
        m_pingLabel->setText(QStringLiteral("延迟 %1ms").arg(m_lastRtt));
}
namespace {
    struct EatSnd {
        HWAVEOUT hout = nullptr;
        WAVEHDR* hdr = nullptr;
        QByteArray* data = nullptr;
    };
    // 释放吃豆音效资源
    void closeEatSnd(EatSnd* s)
    {
        if (!s)
            return;
        if (s->hout) {
            waveOutReset(s->hout);
            if (s->hdr)
                waveOutUnprepareHeader(s->hout, s->hdr, sizeof(WAVEHDR));
            waveOutClose(s->hout);
        }
        delete s->hdr;
        delete s->data;
        delete s;
    }
}
// 吃豆音效：waveOut 独立输出，与背景音乐共存、可调音量
void SnakeWindows::onFoodEaten(int, quint8)
{
    static QByteArray wavBytes;
    if (wavBytes.isEmpty()) {
        QFile f(QStringLiteral(":/eat.wav"));
        if (!f.open(QIODevice::ReadOnly))
            return;
        wavBytes = f.readAll();
        if (wavBytes.size() < 64) {
            wavBytes.clear();
            return;
        }
    }
    quint16 channels = 1, bits = 16;
    quint32 rate = 44100;
    qint64 dataOff = -1;
    const uchar* p = (const uchar*)wavBytes.constData();
    int i = 12;
    while (i + 8 <= wavBytes.size()) {
        quint32 sz = quint32(p[i + 4]) | (quint32(p[i + 5]) << 8) | (quint32(p[i + 6]) << 16) | (quint32(p[i + 7]) << 24);
        if (memcmp(p + i, "fmt ", 4) == 0 && sz >= 16) {
            channels = quint16(p[i + 10] | (p[i + 11] << 8));
            rate = quint32(p[i + 12]) | (quint32(p[i + 13]) << 8) | (quint32(p[i + 14]) << 16) | (quint32(p[i + 15]) << 24);
            bits = quint16(p[i + 22] | (p[i + 23] << 8));
        }
        else if (memcmp(p + i, "data", 4) == 0) {
            dataOff = i + 8;
            break;
        }
        i += 8 + int(sz) + (sz & 1);
    }
    if (dataOff < 0 || dataOff >= wavBytes.size())
        return;
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = channels;
    fmt.nSamplesPerSec = rate;
    fmt.wBitsPerSample = bits;
    fmt.nBlockAlign = quint16(channels * bits / 8);
    fmt.nAvgBytesPerSec = rate * fmt.nBlockAlign;
    EatSnd* s = new EatSnd;
    s->data = new QByteArray(wavBytes.constData() + dataOff,
        int(wavBytes.size() - dataOff));
    if (waveOutOpen(&s->hout, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL)
        != MMSYSERR_NOERROR) {
        closeEatSnd(s);
        return;
    }
    const quint32 v = quint32(quint64(m_musicVolume) * 0xFFFF / 100);
    waveOutSetVolume(s->hout, MAKELONG(v, v));
    s->hdr = new WAVEHDR{};
    s->hdr->lpData = s->data->data();
    s->hdr->dwBufferLength = DWORD(s->data->size());
    if (waveOutPrepareHeader(s->hout, s->hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR
        || waveOutWrite(s->hout, s->hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        closeEatSnd(s);
        return;
    }
    QTimer::singleShot(700, [s]() { closeEatSnd(s); });
}
// 加载并循环播放背景音乐（PlaySound 原生循环）
void SnakeWindows::setupMusic()
{
    const QString path = findAssetFile(QStringLiteral("Music.wav"));
    if (path.isEmpty())
        return;
    if (!PlaySoundW(path.toStdWString().c_str(), nullptr,
        SND_ASYNC | SND_LOOP | SND_FILENAME))
        return;
    applyMusicVolume();
}
// 应用音量设置（音频流未就绪则稍后重试一次）
void SnakeWindows::applyMusicVolume()
{
    if (setProcessAudioVolume(m_musicVolume))
        return;
    QTimer::singleShot(500, this, [this]() {
        setProcessAudioVolume(m_musicVolume);
        });
}
// 服务器收到客户端方向输入 -> 写入核心
void SnakeWindows::onHostInput(int playerId, int dir)
{
    if (m_core->started() && !m_core->gameOver())
        m_core->setInput(playerId, dir);
}
// 客户端应用服务器状态帧
void SnakeWindows::onClientState(const QByteArray& payload)
{
    m_core->applyState(payload);
    m_animProgress = 0.0;
    m_animLastMs = m_animClock.elapsed();
    updateHud();
    update();
}
// 客户端收到分配编号
void SnakeWindows::onWelcome(int playerId)
{
    m_localId = playerId;
    updateStatus(QStringLiteral("已连接，你是 P%1 · 等待房主开始…").arg(m_localId + 1));
}
// 客户端开局：重置核心并显示所有玩家分数
void SnakeWindows::onStartReceived(int playerCount, int gridW, int gridH)
{
    Q_UNUSED(gridW);
    Q_UNUSED(gridH);
    m_core->reset(playerCount);
    m_core->setName(m_localId, m_playerName);
    m_animProgress = 0.0;
    ui->scoreLabel->hide(); ui->highLabel->hide(); ui->lenLabel->hide();
    createPlayerLabels(playerCount, false);
    updateStatus(QStringLiteral("对局开始！你是 P%1（方向键移动）")
        .arg(m_localId + 1));
    updateHud();
    update();
}
// 连接超时提示（含排查方法）
void SnakeWindows::onConnectTimeout()
{
    updateStatus(QStringLiteral("连接超时，请检查：① 主机是否点了“创建房间”（标题显示 IP） ② 是否与主机同一网络（同一 WiFi 或主机热点） ③ IP 是否正确 ④ 主机防火墙是否放行 12345 端口 · 按 ESC 返回菜单"));
    QMessageBox::warning(this, QStringLiteral("连接超时"),
        QStringLiteral("无法连接主机，请检查：\n"
            "1. 主机是否已点击“创建房间”（窗口标题会显示 IP）\n"
            "2. 本机与主机是否在同一网络（连同一 WiFi 或主机热点）\n"
            "3. 输入的 IP 是否与房主标题一致\n"
            "4. 主机防火墙是否放行端口 12345\n"
            "测试方法：手机连上同一网络后，用浏览器访问 http://IP:12345 看能否打开。"));
}
// 连接失败提示
void SnakeWindows::onNetworkError(const QString& message)
{
    updateStatus(QStringLiteral("连接失败：%1").arg(message));
}
// 玩家断线 -> 其蛇死亡，尸体保留
void SnakeWindows::onClientLeft(int playerId)
{
    m_core->killPlayer(playerId);
    if (m_core->gameOver())
        updateHud();
}
// 蛇节插值位置：t=0 旧格，t=1 新格
QPointF SnakeWindows::segmentPos(int player, int index, double t) const
{
    const GameCore::Snake& s = m_core->snake(player);
    if (index < s.body.size()) {
        const QPointF curr(s.body.at(index));
        if (index < s.prev.size())
            return QPointF(s.prev.at(index)) + (curr - QPointF(s.prev.at(index))) * t;
        return curr;
    }
    return QPointF();
}
// 更新底部提示文字
void SnakeWindows::updateStatus(const QString& text)
{
    ui->hintLabel->setText(text);
    ui->hintLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
// 刷新得分 / 长度 / 排名标签
void SnakeWindows::updateHud()
{
    if (m_mode == Mode::Single) {
        const GameCore::Snake& s = m_core->snake(0);
        ui->scoreLabel->setText(QStringLiteral("得分 %1").arg(s.score));
        ui->highLabel->setText(QStringLiteral("最高 %1").arg(m_highScore));
        ui->lenLabel->setText(QStringLiteral("长度 %1").arg(s.body.size()));
        return;
    }
    for (int i = 0; i < m_playerLabels.size() && i < m_core->playerCount(); ++i) {
        const GameCore::Snake& s = m_core->snake(i);
        m_playerLabels[i]->setText(QStringLiteral("P%1 %2%3").arg(i + 1).arg(s.score).arg(s.alive ? QString() : QStringLiteral(" ✝")));
        m_playerLabels[i]->setStyleSheet(QStringLiteral("color:%1; font-size:15px; font-weight:bold;").arg(s.alive ? kPlayerColors[i % 4].name() : QStringLiteral("#8a8a96")));
    }
}
// 单机多人：每 16ms 轮询物理键状态，多人同按不丢键
void SnakeWindows::pollLocalInput()
{
    if (m_mode != Mode::Local)
        return;
    if (!m_core->started() || m_core->paused() || m_core->gameOver())
        return;
    static const int keyCodes[4][4] = { { 'W', 'S', 'A', 'D' }, { 'Y', 'H', 'G', 'J' }, { '8', '2', '4', '6' }, { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT } };
    static const int dirs[4] = { Dir::Up, Dir::Down, Dir::Left, Dir::Right };
    const int n = qMin(4, m_core->playerCount());
    for (int p = 0; p < n; ++p) {
        if (!m_core->snake(p).alive) continue;
        for (int k = 0; k < 4; ++k)
            if (GetAsyncKeyState(keyCodes[p][k]) & 0x8000) { m_core->setInput(p, dirs[k]); break; }
    }
}
// 键盘事件：移动 / 空格开始暂停重开 / ESC 返回菜单
void SnakeWindows::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    const bool inGame = m_core->started() && !m_core->paused() && !m_core->gameOver();
    if (m_mode == Mode::Local && inGame) {
        const QVector<QPair<int, int>> keyMap[4] = {
            {{Qt::Key_W, Dir::Up}, {Qt::Key_S, Dir::Down}, {Qt::Key_A, Dir::Left}, {Qt::Key_D, Dir::Right}},
            {{Qt::Key_Y, Dir::Up}, {Qt::Key_H, Dir::Down}, {Qt::Key_G, Dir::Left}, {Qt::Key_J, Dir::Right}},
            {{Qt::Key_8, Dir::Up}, {Qt::Key_2, Dir::Down}, {Qt::Key_4, Dir::Left}, {Qt::Key_6, Dir::Right}},
            {{Qt::Key_Up, Dir::Up}, {Qt::Key_Down, Dir::Down}, {Qt::Key_Left, Dir::Left}, {Qt::Key_Right, Dir::Right}},
        };
        const int n = qMin(4, m_core->playerCount());
        for (int p = 0; p < n; ++p) {
            if (!m_core->snake(p).alive) continue;
            for (const QPair<int, int>& m : keyMap[p])
                if (key == m.first) { m_core->setInput(p, m.second); return; }
        }
    }
    else if (m_mode != Mode::Local && inGame) {
        int dir = -1;
        static const QPair<int, int> kDirKeys[] = {
            {Qt::Key_Up, Dir::Up}, {Qt::Key_W, Dir::Up}, {Qt::Key_Down, Dir::Down}, {Qt::Key_S, Dir::Down},
            {Qt::Key_Left, Dir::Left}, {Qt::Key_A, Dir::Left}, {Qt::Key_Right, Dir::Right}, {Qt::Key_D, Dir::Right},
        };
        for (const auto& k : kDirKeys)
            if (key == k.first) { dir = k.second; break; }
        if (dir >= 0) {
            if (m_mode == Mode::Client) m_client->sendInput(dir); else m_core->setInput(m_localId, dir);
            return;
        }
    }
    switch (key) {
    case Qt::Key_Space:
    case Qt::Key_P:
        if (m_mode == Mode::Single) {
            if (m_core->gameOver()) {
                m_core->reset(1);
                m_core->setName(0, m_playerName);
                updateStatus(QStringLiteral("方向键 / WASD 移动 · 空格暂停"));
                updateHud();
                update();
            }
            else if (m_core->started()) {
                m_paused = !m_paused;
                m_core->setPaused(m_paused);
                updateStatus(m_paused ? QStringLiteral("已暂停 · 按空格继续") : QStringLiteral("方向键 / WASD 移动 · 空格暂停"));
                update();
            }
        }
        else if (m_mode == Mode::Local) {
            if (!m_core->started()) {
                m_core->setStarted(true);
                m_timer.start();
                updateStatus(QStringLiteral("P1:WASD P2:YGHJ P3:数字8426 P4:方向键 · P1 空格暂停"));
                update();
            }
            else if (m_core->gameOver()) {
                m_core->reset(m_localPlayers);
                for (int i = 0; i < m_localPlayers; ++i)
                    m_core->setName(i, QStringLiteral("P%1").arg(i + 1));
                updateStatus(QStringLiteral("P1:WASD P2:YGHJ P3:数字8426 P4:方向键 · 空格开始"));
                updateHud();
                update();
            }
            else if (m_core->started()) {
                m_paused = !m_paused;
                m_core->setPaused(m_paused);
                updateStatus(m_paused ? QStringLiteral("已暂停 · P1 按空格继续") : QStringLiteral("P1:WASD P2:YGHJ P3:数字8426 P4:方向键 · 空格暂停"));
                update();
            }
        }
        else if (m_mode == Mode::Host) {
            if (!m_core->started() || m_core->gameOver()) {
                if (m_core->gameOver()) {
                    m_core->setPaused(false);
                    updateStatus(QStringLiteral("重新开局…按空格开始"));
                }
                startHostGame();
            }
            else {
                m_paused = !m_paused;
                m_core->setPaused(m_paused);
                updateStatus(m_paused ? QStringLiteral("已暂停 · 按空格继续") : QStringLiteral("对局进行中 · 空格暂停"));
                m_server->broadcast(m_core->stateBytes());
                update();
            }
        }
        break;
    case Qt::Key_Escape:
        close();
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}
// 画蛇：身体渐变分段线 + 圆角头 + 道具光圈 + 眼睛名字
void SnakeWindows::drawSnake(QPainter& painter, int player, bool dead)
{
    const GameCore::Snake& s = m_core->snake(player);
    if (s.body.isEmpty())
        return;
    const QColor headColor = dead ? QColor(0x6a, 0x6a, 0x7a) : kPlayerColors[player % 4];
    const int n = s.body.size();
    QVector<QPointF> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        const QPointF pos = segmentPos(player, i, m_animProgress);
        if (pos.isNull() && i >= s.prev.size())
            break;
        pts.append(QPointF(pos.x() * CELL_SIZE + CELL_SIZE / 2.0, HUD_HEIGHT + pos.y() * CELL_SIZE + CELL_SIZE / 2.0));
    }
    if (pts.size() < 2) {
        painter.setBrush(headColor);
        painter.setPen(Qt::NoPen);
        if (!pts.isEmpty())
            painter.drawRoundedRect(QRectF(pts.first().x() - CELL_SIZE / 2.0 + 2, pts.first().y() - CELL_SIZE / 2.0 + 2, CELL_SIZE - 4, CELL_SIZE - 4), 6, 6);
        if (!dead)
            drawEyes(painter, s, segmentPos(player, 0, m_animProgress), false);
        drawName(painter, player, segmentPos(player, 0, m_animProgress), dead);
        return;
    }
    const qreal penW = CELL_SIZE - 5.0;
    for (int i = int(pts.size()) - 2; i >= 0; --i) {
        const int seg = i + 1;
        QColor color;
        if (dead) {
            color = kDeadColor;
        }
        else if (seg == 0) {
            color = headColor;
        }
        else {
            const double t = 1.0 - 0.5 * double(seg) / qMax(1, n - 1);
            const QColor& base = kPlayerColors[player % 4];
            color = QColor(int(base.red() * t), int(base.green() * t), int(base.blue() * t));
        }
        painter.setPen(QPen(color, penW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(pts.at(i), pts.at(i + 1));
    }
    const QPointF hp0 = pts.first();
    painter.setPen(Qt::NoPen);
    painter.setBrush(headColor);
    painter.drawRoundedRect(QRectF(hp0.x() - CELL_SIZE / 2.0 + 2, hp0.y() - CELL_SIZE / 2.0 + 2, CELL_SIZE - 4, CELL_SIZE - 4), 6, 6);
    if (!dead) {
        const QPointF hp = segmentPos(player, 0, m_animProgress);
        const qreal hcx = hp.x() * CELL_SIZE + CELL_SIZE / 2.0;
        const qreal hcy = HUD_HEIGHT + hp.y() * CELL_SIZE + CELL_SIZE / 2.0;
        if (s.speedTicks > 0 || s.slowTicks > 0) {
            QColor c(s.speedTicks > 0 ? 0xffd73a : 0x3ed4d4); c.setAlpha(200);
            painter.setPen(QPen(c, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPointF(hcx, hcy), CELL_SIZE * 0.72, CELL_SIZE * 0.72);
        }
        if (s.reverseTicks > 0) { QColor rc(0xc77bff); rc.setAlpha(220); painter.setPen(QPen(rc, 2)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(QPointF(hcx, hcy), CELL_SIZE * 0.84, CELL_SIZE * 0.84); }
    }
    if (!dead)
        drawEyes(painter, s, segmentPos(player, 0, m_animProgress), false);
    drawName(painter, player, segmentPos(player, 0, m_animProgress), dead);
}
// 蛇头正上方绘制玩家名字（跟随蛇移动）
void SnakeWindows::drawName(QPainter& painter, int player, const QPointF& headPos, bool dead)
{
    const QString name = m_core->name(player);
    if (name.isEmpty())
        return;
    QFont f = painter.font();
    f.setPointSize(10);
    f.setBold(true);
    painter.setFont(f);
    const QPointF center(headPos.x() * CELL_SIZE + CELL_SIZE / 2.0, HUD_HEIGHT + headPos.y() * CELL_SIZE - 2);
    const QRectF textRect(center.x() - CELL_SIZE * 2, center.y() - 20, CELL_SIZE * 4, 15);
    painter.setPen(dead ? QColor(0x9a, 0x9a, 0xa6) : kPlayerColors[player % 4]);
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignBottom, name);
}
// 蛇头眼睛（方向查表）
void SnakeWindows::drawEyes(QPainter& painter, const GameCore::Snake& s, const QPointF& headPos, bool dead)
{
    Q_UNUSED(dead);
    static const QPoint kDirs[4] = { QPoint(0, -1), QPoint(1, 0), QPoint(0, 1), QPoint(-1, 0) };
    const QPoint d = kDirs[qBound<int>(0, s.dir, 3)];
    const QPointF center(headPos.x() * CELL_SIZE + CELL_SIZE / 2.0, HUD_HEIGHT + headPos.y() * CELL_SIZE + CELL_SIZE / 2.0);
    const QPointF perp(-d.y(), d.x());
    const QPointF e1 = center + perp * 3.5 + QPointF(d) * 4.0;
    const QPointF e2 = center - perp * 3.5 + QPointF(d) * 4.0;
    painter.setBrush(Qt::white);
    painter.drawEllipse(e1, 3.0, 4.0);
    painter.drawEllipse(e2, 3.0, 4.0);
    painter.setBrush(QColor(0x14, 0x18, 0x28));
    painter.drawEllipse(e1 + QPointF(d) * 1.2, 1.6, 2.0);
    painter.drawEllipse(e2 + QPointF(d) * 1.2, 1.6, 2.0);
}
namespace {
    // 7 类食物外观表（颜色 / 半径系数 / 道具符号）
    struct FoodStyle { QRgb rgb; qreal radius; const char* sym; };
    const FoodStyle kFoodStyles[] = {
        { 0xff4747, 0.42f, "" }, { 0xffd73a, 0.50f, "" }, { 0x4ea1ff, 0.30f, "" },
        { 0xff9f4a, 0.46f, "⚡" }, { 0x3ed4d4, 0.46f, "≋" }, { 0xc77bff, 0.46f, "↺" }, { 0xff6fb5, 0.46f, "✂" },
    };
}
// 绘制：背景 / 网格 / 食物 / 蛇 / 暂停与结束遮罩
void SnakeWindows::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(0x0d, 0x10, 0x20));
    const QRect boardRect(0, HUD_HEIGHT, GRID_W * CELL_SIZE, GRID_H * CELL_SIZE);
    if (!m_bgPixmap.isNull())
        painter.drawPixmap(boardRect, m_bgPixmap);
    else
        painter.fillRect(boardRect, QColor(0x18, 0x1c, 0x2e));
    painter.setPen(QPen(QColor(0x25, 0x2a, 0x40, 120), 1));
    for (int x = 0; x <= GRID_W; ++x)
        painter.drawLine(x * CELL_SIZE, HUD_HEIGHT, x * CELL_SIZE, HUD_HEIGHT + GRID_H * CELL_SIZE);
    for (int y = 0; y <= GRID_H; ++y)
        painter.drawLine(0, HUD_HEIGHT + y * CELL_SIZE, GRID_W * CELL_SIZE, HUD_HEIGHT + y * CELL_SIZE);
    if (m_foodVisible && m_core->playerCount() > 0) {
        for (int f = 0; f < m_core->foodCount(); ++f) {
            const GameCore::Food& fd = m_core->foodAt(f);
            const qreal fx = fd.pos.x() * CELL_SIZE, fy = HUD_HEIGHT + fd.pos.y() * CELL_SIZE;
            const qreal cx = fx + CELL_SIZE / 2.0, cy = fy + CELL_SIZE / 2.0;
            const quint8 type = fd.type;
            const FoodStyle& st = kFoodStyles[qBound<int>(0, type, 6)];
            const QColor base(st.rgb);
            const qreal radius = CELL_SIZE * st.radius;
            if (fd.lifeTicks >= 0) { painter.setPen(QPen(QColor(base.red(), base.green(), base.blue(), 110), 2)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(QPointF(cx, cy), radius + 4, radius + 4); }
            painter.setBrush(base);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(QPointF(cx, cy), radius, radius);
            painter.setBrush(QColor(255, 255, 255, 90));
            painter.drawEllipse(QPointF(cx - radius * 0.35f, cy - radius * 0.35f), radius * 0.30f, radius * 0.22f);
            if (type >= GameCore::FoodSpeed) {
                QFont sf = painter.font();
                sf.setPointSize(type == GameCore::FoodShrink ? 9 : 10); sf.setBold(true);
                painter.setFont(sf); painter.setPen(Qt::white);
                painter.drawText(QRectF(fx, fy, CELL_SIZE, CELL_SIZE), Qt::AlignCenter, QString::fromUtf8(st.sym));
            }
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (int p = 0; p < m_core->playerCount(); ++p) {
            const GameCore::Snake& s = m_core->snake(p);
            if (s.body.isEmpty())
                continue;
            const bool dead = !s.alive || m_core->gameOver();
            if (dead != (pass == 0))
                continue;
            drawSnake(painter, p, dead);
        }
    }
    if (m_core->paused()) {
        painter.fillRect(boardRect, QColor(0, 0, 0, 140));
        QFont f = painter.font();
        f.setPointSize(26); f.setBold(true); painter.setFont(f);
        painter.setPen(QColor(0xe8, 0xec, 0xf5));
        painter.drawText(boardRect.adjusted(0, -20, 0, 0), Qt::AlignCenter, QStringLiteral("已暂停 · ESC 返回菜单"));
        QFont sub = painter.font();
        sub.setPointSize(13); sub.setBold(false); painter.setFont(sub);
        painter.setPen(QColor(0x9a, 0xa6, 0xc8));
        painter.drawText(boardRect.adjusted(0, 40, 0, 0), Qt::AlignCenter,
            m_mode == Mode::Client ? QStringLiteral("等待房主继续") : QStringLiteral("按空格继续 · ESC 返回菜单"));
    }
    if (m_core->gameOver()) {
        painter.fillRect(boardRect, QColor(0, 0, 0, 130));
        const QVector<int> order = m_core->ranking();
        QFont f = painter.font();
        f.setPointSize(26); f.setBold(true); painter.setFont(f);
        painter.setPen(QColor(0xff, 0x5c, 0x6c));
        painter.drawText(boardRect.adjusted(0, -86, 0, 0), Qt::AlignCenter,
            m_mode == Mode::Single ? QStringLiteral("游戏结束 · %1 分").arg(m_core->snake(0).score) : QStringLiteral("游戏结束"));
        QFont sub = painter.font();
        sub.setPointSize(15); sub.setBold(false); painter.setFont(sub);
        painter.setPen(QColor(0xe8, 0xec, 0xf5));
        if (m_mode == Mode::Single) {
            sub.setPointSize(14); painter.setFont(sub);
            painter.setPen(QColor(0x9a, 0xa6, 0xc8));
            painter.drawText(boardRect.adjusted(0, 36, 0, 0), Qt::AlignCenter, QStringLiteral("按空格重新开始 · ESC 返回菜单"));
        }
        else {
            QStringList lines;
            const int shown = qMin(MAX_PLAYERS, order.size());
            for (int r = 0; r < shown; ++r) {
                const GameCore::Snake& s = m_core->snake(order.at(r));
                lines << QStringLiteral("%1. P%2 · %3 分%4").arg(r + 1).arg(order.at(r) + 1).arg(s.score).arg(s.alive ? QStringLiteral("（存活）") : QStringLiteral("（出局）"));
            }
            painter.drawText(boardRect.adjusted(0, 10, 0, -10), Qt::AlignCenter, lines.join('\n'));
        }
    }
}
