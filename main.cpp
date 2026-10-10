#include <QApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QSlider>
#include <QCheckBox>
#include <QSettings>
#include <QIcon>
#include <QMessageBox>

#include "snakewindows.h"

namespace {

// 读取持久化名字
QString loadSavedName()
{
    QSettings s(QStringLiteral("SnakeGame"), QStringLiteral("SnakeGame"));
    return s.value(QStringLiteral("playerName"), QStringLiteral("P1")).toString();
}

void saveName(const QString &name)
{
    QSettings s(QStringLiteral("SnakeGame"), QStringLiteral("SnakeGame"));
    s.setValue(QStringLiteral("playerName"), name);
}

// 设置对话框：音乐音量 + 显示 FPS
void showSettingsDialog(QWidget *parent)
{
    QSettings s(QStringLiteral("SnakeGame"), QStringLiteral("SnakeGame"));
    const int volume = s.value(QStringLiteral("musicVolume"), 70).toInt();
    const bool showFps = s.value(QStringLiteral("showFps"), false).toBool();

    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("设置"));
    dlg.setFixedSize(340, 240);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    QLabel *title = new QLabel(QStringLiteral("设 置"), &dlg);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size:22px; font-weight:900; color:#3ed48e;"));
    layout->addWidget(title);

    // 音乐音量
    QLabel *volLabel = new QLabel(
        QStringLiteral("音量大小：%1").arg(volume), &dlg);
    volLabel->setStyleSheet(QStringLiteral("font-size:14px; color:#e8ecf5;"));
    layout->addWidget(volLabel);

    QSlider *volSlider = new QSlider(Qt::Horizontal, &dlg);
    volSlider->setRange(0, 100);
    volSlider->setValue(volume);
    volSlider->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal{height:6px;background:#2a3560;border-radius:3px;}"
        "QSlider::handle:horizontal{width:16px;margin:-5px 0;background:#3ed48e;border-radius:8px;}"));
    layout->addWidget(volSlider);
    QObject::connect(volSlider, &QSlider::valueChanged, &dlg, [volLabel](int v) {
        volLabel->setText(QStringLiteral("音量大小：%1").arg(v));
    });

    // 显示 FPS
    QCheckBox *fpsCheck = new QCheckBox(QStringLiteral("显示当前帧率"), &dlg);
    fpsCheck->setChecked(showFps);
    fpsCheck->setStyleSheet(QStringLiteral("font-size:14px; color:#e8ecf5;"));
    layout->addWidget(fpsCheck);

    // 延迟显示说明（多人模式自动显示）
    QLabel *pingHint = new QLabel(
        QStringLiteral("多人联机会自动显示网络延迟"), &dlg);
    pingHint->setStyleSheet(QStringLiteral("font-size:12px; color:#5f6f96;"));
    layout->addWidget(pingHint);

    // 按钮
    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *btnOk = new QPushButton(QStringLiteral("保存"), &dlg);
    QPushButton *btnCancel = new QPushButton(QStringLiteral("取消"), &dlg);
    const QString btnStyle = QStringLiteral(
        "QPushButton{ background:#1c2340; color:#e8ecf5; border:1px solid #2a3560;"
        " border-radius:8px; font-size:15px; font-weight:bold; padding:8px 0; }"
        "QPushButton:hover{ background:#243060; border-color:#3ed48e; }");
    btnOk->setStyleSheet(btnStyle);
    btnCancel->setStyleSheet(btnStyle);
    btnRow->addWidget(btnOk);
    btnRow->addWidget(btnCancel);
    layout->addLayout(btnRow);

    QObject::connect(btnOk, &QPushButton::clicked, &dlg, [&]() {
        s.setValue(QStringLiteral("musicVolume"), volSlider->value());
        s.setValue(QStringLiteral("showFps"), fpsCheck->isChecked());
        dlg.accept();
    });
    QObject::connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    dlg.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SnakeGame"));

    // 窗口图标（编译进 exe 的 bmp.png）
    app.setWindowIcon(QIcon(QStringLiteral(":/bmp.png")));

    // ===== 启动菜单（手动布局，避免布局管理器在 DPI 下的溢出） =====
    QDialog menu;
    menu.setWindowTitle(QStringLiteral("贪吃蛇"));
    menu.setFixedSize(460, 780);

    QLabel *title = new QLabel(QStringLiteral("贪 吃 蛇"), &menu);
    title->setGeometry(0, 30, 460, 60);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral(
        "font-size:34px; font-weight:900; color:#3ed48e;"));

    QLabel *subtitle = new QLabel(QStringLiteral("经典玩法 · 2~4 人联机 · 道具：⚡加速 ≋减速 ↺反向 ✂缩短"), &menu);
    subtitle->setGeometry(0, 100, 460, 28);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("font-size:13px; color:#8fa1c8;"));

    // 玩家名字输入（记住上次）
    QLabel *nameHint = new QLabel(QStringLiteral("Name："), &menu);
    nameHint->setGeometry(60, 160, 340, 24);
    nameHint->setStyleSheet(QStringLiteral("font-size:13px; color:#8fa1c8;"));

    QLineEdit *nameEdit = new QLineEdit(loadSavedName(), &menu);
    nameEdit->setGeometry(60, 190, 340, 44);
    nameEdit->setMaxLength(8);
    nameEdit->setStyleSheet(QStringLiteral(
        "QLineEdit{ background:#141a30; color:#e8ecf5; border:1px solid #2a3560;"
        " border-radius:8px; font-size:15px; padding:0 10px; }"
        "QLineEdit:focus{ border-color:#3ed48e; }"));

    const QString btnStyle = QStringLiteral(
        "QPushButton{ background:#1c2340; color:#e8ecf5; border:1px solid #2a3560;"
        " border-radius:10px; font-size:17px; font-weight:bold; }"
        "QPushButton:hover{ background:#243060; border-color:#3ed48e; }");
    const QString btnDimStyle = QStringLiteral(
        "QPushButton{ background:#141a30; color:#8fa1c8; border:1px solid #2a3560;"
        " border-radius:10px; font-size:15px; font-weight:bold; }"
        "QPushButton:hover{ background:#243060; color:#e8ecf5; border-color:#3ed48e; }");

    // 菜单按钮：文本 / 纵坐标 / 高度 / 是否弱化样式（数据驱动创建）
    struct MenuItem { const char *text; int y; int h; bool dim; };
    static const MenuItem kMenuItems[] = {
        { "单人游戏", 270, 54, false }, { "多人 · 单机同屏", 334, 54, false },
        { "多人 · 创建房间", 398, 54, false }, { "多人 · 加入房间", 462, 54, false },
        { "设 置", 526, 46, true },
    };
    QPushButton *menuBtns[5];
    for (int i = 0; i < 5; ++i) {
        menuBtns[i] = new QPushButton(QString::fromUtf8(kMenuItems[i].text), &menu);
        menuBtns[i]->setGeometry(60, kMenuItems[i].y, 340, kMenuItems[i].h);
        menuBtns[i]->setStyleSheet(kMenuItems[i].dim ? btnDimStyle : btnStyle);
    }
    QPushButton *btnSingle = menuBtns[0], *btnLocal = menuBtns[1],
                *btnHost = menuBtns[2], *btnJoin = menuBtns[3],
                *btnSettings = menuBtns[4];

    auto nameOf = [&]() {
        QString name = nameEdit->text().trimmed();
        if (name.isEmpty())
            name = QStringLiteral("玩家");
        saveName(name);
        return name;
    };

    QObject::connect(btnSettings, &QPushButton::clicked, &menu, [&]() {
        showSettingsDialog(&menu);
    });

    // ===== 选择后的行为 =====
    // 打开游戏窗口：隐藏菜单；游戏窗口关闭（含 ESC 返回）后重新显示菜单
    auto startGame = [&](SnakeWindows::Mode mode, const QString &host,
                         int players = 1) {
        menu.hide();
        SnakeWindows *window =
            new SnakeWindows(mode, nameOf(), host, 12345, players);
        window->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(window, &QObject::destroyed, &menu, [&menu]() {
            menu.show();
        });
        window->show();
    };

    QObject::connect(btnSingle, &QPushButton::clicked, &menu, [&]() {
        startGame(SnakeWindows::Mode::Single, QString());
    });

    // 单机多人：先选 2-4 人，同屏分键位操作
    QObject::connect(btnLocal, &QPushButton::clicked, &menu, [&]() {
        bool ok = false;
        const int players = QInputDialog::getInt(
            &menu, QStringLiteral("单机多人"),
            QStringLiteral("选择玩家人数（2~4 人）：\n"
                           "P1:WASD  P2:YGHJ  P3:数字8426  P4:方向键"),
            2, 2, 4, 1, &ok);
        if (!ok)
            return;
        startGame(SnakeWindows::Mode::Local, QString(), players);
    });

    QObject::connect(btnHost, &QPushButton::clicked, &menu, [&]() {
        startGame(SnakeWindows::Mode::Host, QString());
    });

    QObject::connect(btnJoin, &QPushButton::clicked, &menu, [&]() {
        bool ok = false;
        const QString ip = QInputDialog::getText(
            &menu, QStringLiteral("加入房间"),
            QStringLiteral("输入房主电脑的 IP 地址（如 192.168.1.100）："),
            QLineEdit::Normal, QStringLiteral("127.0.0.1"), &ok);
        if (!ok || ip.trimmed().isEmpty())
            return;
        startGame(SnakeWindows::Mode::Client, ip.trimmed());
    });

    // 关闭菜单窗口 = 退出程序
    QObject::connect(&menu, &QDialog::finished, &app, &QApplication::quit);

    menu.show();
    return app.exec();
}
