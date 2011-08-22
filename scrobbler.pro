CONFIG += qt
SOURCES += m6_main.cpp helper.c scrobble.c
HEADERS += m6_main.h helper.h scrobble.h

CONFIG += link_pkgconfig
PKGCONFIG += qmafw qmafw-shared glib-2.0 gio-2.0 libsoup-2.4 conic

TARGET = scrobbler

target.path = /usr/bin
INSTALLS += target

QMAKE_CFLAGS += -std=c99 -Wno-unused-parameter
QMAKE_CXXFLAGS += -Wno-unused-parameter
