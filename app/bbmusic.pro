# BBMusic's UI, a plain executable as the bar's Qnx/Elf entry point. Built out of tree by
# scripts/build-app.sh (build/app-ui).
TEMPLATE = app
TARGET = bbmusic
CONFIG += qt warn_on release
QT += declarative network
LIBS += -lbbcascades -lbbdata -lbbsystem -lbb -lbps
QMAKE_CXXFLAGS += -Wno-psabi

SOURCES += src/main.cpp src/EngineLink.cpp src/PlayerState.cpp
HEADERS += src/EngineLink.h src/PlayerState.h src/UiLog.h

DESTDIR = app
OBJECTS_DIR = obj
MOC_DIR = moc
