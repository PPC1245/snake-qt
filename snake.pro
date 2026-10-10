QT       += core gui widgets network

TARGET   = snake
TEMPLATE = app
CONFIG   += c++17

LIBS     += -lwinmm -lole32

SOURCES += \
    main.cpp \
    snakewindows.cpp \
    gamecore.cpp \
    netserver.cpp \
    netclient.cpp

HEADERS += \
    snakewindows.h \
    gamecore.h \
    netserver.h \
    netclient.h \
    netprotocol.h

FORMS   += \
    snakewindows.ui

RESOURCES += \
    src.qrc
