#ifndef NETSERVER_H
#define NETSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QHash>
#include <QStringList>

// 主机网络服务：监听端口、管理客户端、路由消息、广播状态
class NetServer : public QObject
{
    Q_OBJECT

public:
    explicit NetServer(quint16 port, QObject *parent = nullptr);

    bool start();                 // 开始监听
    QString ipAddress() const;    // 本机局域网 IP（优先热点/WiFi）
    QStringList ipCandidates() const; // 所有可用 IP（按优先级）
    quint16 port() const { return m_port; }
    int connectedCount() const { return m_clients.size(); }
    void broadcast(const QByteArray &payload);   // 发给所有客户端（帧封装）
    void sendTo(int playerId, const QByteArray &payload);
    QString name(int playerId) const;            // 客户端上报的名字

signals:
    void joined(int playerId);            // 新玩家加入（id 从 1 开始）
    void inputFrom(int playerId, int dir);// 收到客户端方向输入
    void clientLeft(int playerId);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnect();

private:
    void handlePayload(QTcpSocket *socket, const QByteArray &payload);

    QTcpServer *m_server;
    QList<QTcpSocket *> m_clients;
    QHash<QTcpSocket *, int> m_ids;
    QHash<int, QTcpSocket *> m_socketsById;
    QHash<int, QString> m_names;      // 玩家编号 -> 名字
    QByteArray m_buffer;
    quint16 m_port;
    int m_nextId;
};

#endif // NETSERVER_H
