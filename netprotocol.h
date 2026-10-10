#ifndef NETPROTOCOL_H
#define NETPROTOCOL_H

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>

// 网络消息类型
namespace NetMsg {
enum Msg : quint8 {
    Join    = 1,   // Client -> Server：请求加入（携带名字）
    Welcome = 2,   // Server -> Client：分配玩家编号
    Input   = 3,   // Client -> Server：方向输入
    Start   = 4,   // Server -> Client：开始游戏
    State   = 5,   // Server -> Client：游戏状态帧
    Ping    = 6,   // Client -> Server：延迟探测
    Pong    = 7    // Server -> Client：延迟应答
};
}

// 方向编码
namespace Dir {
enum DirCode : quint8 {
    Up = 0, Right = 1, Down = 2, Left = 3
};
}

// 帧封装：在每条消息前加 4 字节长度前缀（TCP 流式传输防粘包）
static inline QByteArray frameMessage(const QByteArray &payload)
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << quint32(payload.size());
    data.append(payload);
    return data;
}

#endif // NETPROTOCOL_H
