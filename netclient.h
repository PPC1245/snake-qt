#ifndef NETCLIENT_H
#define NETCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>

// 客户端网络：连接主机房间、发送输入、接收状态、探测延迟
class NetClient : public QObject
{
    Q_OBJECT

public:
    explicit NetClient(const QString &host, quint16 port,
                       const QString &playerName, QObject *parent = nullptr);

    void connectToHost();
    void sendInput(int dir);
    void startPing(int intervalMs = 1000);
    void disconnectFromHost();

    bool isConnected() const;
    int playerId() const { return m_playerId; }

signals:
    void welcomeReceived(int playerId);          // 服务器分配编号
    void startReceived(int playerCount, int gridW, int gridH);
    void stateReceived(const QByteArray &payload);
    void pongReceived(quint32 rttMs);            // 网络延迟
    void networkError(const QString &message);
    void connectionTimeout();          // 连接超时（无法建立连接）
    void disconnected();

private slots:
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    void sendPing();

private:
    void handlePayload(const QByteArray &payload);

    QTcpSocket *m_socket;
    QString m_host;
    quint16 m_port;
    QString m_playerName;
    QByteArray m_buffer;
    int m_playerId = -1;
    QTimer m_pingTimer;
    QTimer m_connectTimer;   // 连接超时计时（8 秒）
    QElapsedTimer m_elapsed;
    quint32 m_lastTs = 0;
};

#endif // NETCLIENT_H
