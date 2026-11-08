QT += core gui qml quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = labelmaker_tests
TEMPLATE = app

CONFIG += link_pkgconfig
PKGCONFIG += poppler-qt6

INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/filepicker.h \
    ../src/labelimageprovider.h \
    ../src/labelrenderer.h \
    ../src/labelsize.h \
    ../src/theme.h \
    ../src/translator.h \
    ../src/tspljob.h \
    ../src/printerlink.h

SOURCES += \
    labelmaker_tests.cpp \
    ../src/backend.cpp \
    ../src/labelrenderer.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp \
    ../src/tspljob.cpp \
    ../src/printerlink.cpp

RESOURCES += ../src/resources.qrc
