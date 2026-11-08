QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = label-maker
TEMPLATE = app

CONFIG += link_pkgconfig
PKGCONFIG += poppler-qt6

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/filepicker.h \
    src/labelimageprovider.h \
    src/labelrenderer.h \
    src/labelsize.h \
    src/portalfilepicker.h \
    src/printerlink.h \
    src/theme.h \
    src/translator.h \
    src/tspljob.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/labelrenderer.cpp \
    src/portalfilepicker.cpp \
    src/printerlink.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/tspljob.cpp

RESOURCES += src/resources.qrc
