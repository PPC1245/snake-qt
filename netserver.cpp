#include "netserver.h"
#include "netprotocol.h"
#include "gamecore.h"

#include <QDataStream>
#include <QNetworkInterface>
#include <QNetworkAddressEntry>
#include <algorithm>

NetServer::NetServer(quint16 port, QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_port(port)
    , m_nextId(1)
{
    connect(m_server, &QTcpServer::newConnection,
            this, &NetServer::onNewConnection);
}

bool NetServer::start()
{
    return m_server->listen(QHostAddress::Any, m_port);
}

QStringList NetServer::ipCandidates() const
{
    struct Cand { QString ip; QString name; int pri; };
    QList<Cand> cands;
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : addresses) {
        if (addr.protocol() != QAbstractSocket::IPv4Protocol || addr.isLoopback())
            continue;
        const QString ip = addr.toString();
        if (ip.startsWith(QLatin1String("169.254.")))   // APIPA：无网络的网卡
            continue;
        QNetworkInterface iface;
        const QList<QNetworkInterface> allIfaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface &i : allIfaces) {
            const QList<QNetworkAddressEntry> entries = i.addressEntries();
            bool hit = false;
            for (const QNetworkAddressEntry &e : entries) {
                if (e.ip() == addr) { hit = true; break; }
            }
            if (hit) { iface = i; break; }
        }
        const QString name = iface.name().toLower() + QLatin1Char('|')
                           + iface.humanReadableName().toLower();
        // 排除虚拟网卡、蓝牙、代理网卡
        if (name.contains("vmware") || name.contains("virtualbox")
            || name.contains("vethernet") || name.contains("hyper-v")
            || name.contains("bluetooth") || name.contains("蓝牙")
            || name.contains("loopback") || name.contains("docker")
            || name.contains("wsl") || name.contains("tap-")
            || name.contains("tailscale") || name.contains("wireguard")
            || name.contains("clash") || name.contains("tun"))
            continue;
        int pri = 3;   // 其他未知网卡
        if (name.contains(QLatin1Char('*')) || name.contains("hotspot")
            || name.contains("热点"))
            pri = 1;   // 移动热点（朋友最常连）
        else if (name.contains("wlan") || name.contains("wi-fi")
                 || name.contains("wireless") || name.contains("无线")
                 || name.contains("ethernet") || name.contains("以太网"))
            pri = 2;   // WiFi / 有线
        cands.append({ip, iface.humanReadableName(), pri});
    }
    std::stable_sort(cands.begin(), cands.end(),
        [](const Cand &a, const Cand &b) { return a.pri < b.pri; });
    QStringList result;
    for (const Cand &c : cands)
        result << c.ip;
    if (result.isEmpty())
        result << QStringLiteral("127.0.0.1");
    return result;
}

QString NetServer::ipAddress() const
{
    const QStringList list = ipCandidates();
    return list.isEmpty() ? QStringLiteral("127.0.0.1") : list.first();
}

void NetServer::broadcast(const QByteArray &payload)
{
    const QByteArray framed = frameMessage(payload);
    for (QTcpSocket *socket : m_clients) {
        if (socket->state() == QAbstractSocket::ConnectedState)
            socket->write(framed);
    }
}

void NetServer::sendTo(int playerId, const QByteArray &payload)
{
    QTcpSocket *socket = m_socketsById.value(playerId);
    if (socket && socket->state() == QAbstractSocket::ConnectedState)
        socket->write(frameMessage(payload));
}

void NetServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        // 最多 3 个客户端（+ 主机 = 4 人）
        if (m_clients.size() >= 3) {
            socket->abort();
            continue;
        }
        const int playerId = m_nextId++;
        m_clients.append(socket);
        m_ids.insert(socket, playerId);
        m_socketsById.insert(playerId, socket);
        // 关闭 Nagle 算法：状态广播小包即时发出，降低感知延迟
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        connect(socket, &QTcpSocket::readyRead, this, &NetServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &NetServer::onDisconnect);
        // 分配玩家编号（Welcome 消息携带 playerId）
        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        out << quint8(NetMsg::Welcome) << quint8(playerId);
        socket->write(frameMessage(payload));
        emit joined(playerId);
    }
}

void NetServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;
    m_buffer.append(socket->readAll());
    // 按长度前缀拆包
    while (m_buffer.size() >= 4) {
        QDataStream peek(m_buffer);
        quint32 len = 0;
        peek >> len;
        if (m_buffer.size() < int(4 + len))
            break;
        QByteArray payload = m_buffer.mid(4, len);
        m_buffer.remove(0, 4 + len);
        handlePayload(socket, payload);
    }
}

void NetServer::handlePayload(QTcpSocket *socket, const QByteArray &payload)
{
    QDataStream in(payload);
    quint8 msg;
    in >> msg;
    const int playerId = m_ids.value(socket, -1);

    if (msg == NetMsg::Input) {
        quint8 dir;
        in >> dir;
        if (playerId >= 0)
            emit inputFrom(playerId, dir);
    } else if (msg == NetMsg::Join) {
        // 记录客户端名字
        QString name;
        in >> name;
        if (playerId >= 0)
            m_names[playerId] = name;
    } else if (msg == NetMsg::Ping) {
        // 延迟探测：原样回显时间戳
        quint32 ts = 0;
        in >> ts;
        QByteArray pong;
        QDataStream out(&pong, QIODevice::WriteOnly);
        out << quint8(NetMsg::Pong) << ts;
        socket->write(frameMessage(pong));
    }
}

QString NetServer::name(int playerId) const
{
    return m_names.value(playerId);
}

void NetServer::onDisconnect()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;
    const int playerId = m_ids.value(socket, -1);
    m_clients.removeAll(socket);
    m_ids.remove(socket);
    m_socketsById.remove(playerId);
    socket->deleteLater();
    if (playerId >= 0)
        emit clientLeft(playerId);
}
