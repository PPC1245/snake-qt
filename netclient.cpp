#include "netclient.h"
#include "netprotocol.h"

#include <QDataStream>
#include <QNetworkProxy>

NetClient::NetClient(const QString &host, quint16 port,
                     const QString &playerName, QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_host(host)
    , m_port(port)
    , m_playerName(playerName)
{
    // 局域网直连不走系统代理（修复：代理软件导致连接报 proxy type is invalid）
    m_socket->setProxy(QNetworkProxy::NoProxy);
    // 关闭 Nagle 算法：小包即时发出，避免状态帧被缓存造成额外延迟
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_socket, &QTcpSocket::readyRead, this, &NetClient::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &NetClient::disconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &NetClient::onError);
    connect(&m_pingTimer, &QTimer::timeout, this, &NetClient::sendPing);
    // 连接超时：网络不可达/防火墙拦截时 SYN 无回应，不能永远干等
    m_connectTimer.setSingleShot(true);
    m_connectTimer.setInterval(8000);
    connect(&m_connectTimer, &QTimer::timeout, this, [this]() {
        m_socket->abort();
        emit connectionTimeout();
    });
}

void NetClient::connectToHost()
{
    m_socket->connectToHost(m_host, m_port);
    m_connectTimer.start();
    // 连接建立后发送加入请求（携带名字）
    connect(m_socket, &QTcpSocket::connected, this, [this]() {
        m_connectTimer.stop();
        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        out << quint8(NetMsg::Join) << m_playerName;
        m_socket->write(frameMessage(payload));
    });
}

void NetClient::sendInput(int dir)
{
    if (!isConnected())
        return;
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out << quint8(NetMsg::Input) << quint8(quint8(dir));
    m_socket->write(frameMessage(payload));
}

void NetClient::startPing(int intervalMs)
{
    m_pingTimer.start(intervalMs);
}

void NetClient::disconnectFromHost()
{
    m_connectTimer.stop();
    m_socket->disconnectFromHost();
}

bool NetClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void NetClient::sendPing()
{
    if (!isConnected())
        return;
    if (!m_elapsed.isValid())
        m_elapsed.start();
    m_lastTs = quint32(m_elapsed.elapsed());
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out << quint8(NetMsg::Ping) << m_lastTs;
    m_socket->write(frameMessage(payload));
}

void NetClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    while (m_buffer.size() >= 4) {
        QDataStream peek(m_buffer);
        quint32 len = 0;
        peek >> len;
        if (m_buffer.size() < int(4 + len))
            break;
        QByteArray payload = m_buffer.mid(4, len);
        m_buffer.remove(0, 4 + len);
        handlePayload(payload);
    }
}

void NetClient::handlePayload(const QByteArray &payload)
{
    QDataStream in(payload);
    quint8 msg;
    in >> msg;
    switch (msg) {
    case NetMsg::Welcome: {
        quint8 playerId;
        in >> playerId;
        m_playerId = playerId;
        emit welcomeReceived(playerId);
        break;
    }
    case NetMsg::Start: {
        quint8 count;
        quint16 gw, gh;
        in >> count >> gw >> gh;
        emit startReceived(count, gw, gh);
        break;
    }
    case NetMsg::State:
        emit stateReceived(payload);
        break;
    case NetMsg::Pong: {
        quint32 ts = 0;
        in >> ts;
        if (!m_elapsed.isValid())
            m_elapsed.start();
        const quint32 rtt = quint32(m_elapsed.elapsed()) - ts;
        emit pongReceived(rtt);
        break;
    }
    default:
        break;
    }
}

void NetClient::onError(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::RemoteHostClosedError)
        return;
    emit networkError(m_socket->errorString());
}
