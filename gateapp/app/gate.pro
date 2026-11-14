# The gate app's UI, a plain executable as the bar's Qnx/Elf entry point. Built out of tree by
# scripts/build-gate.sh (build/gate-ui).
TEMPLATE = app
TARGET = bbmusic-gate
CONFIG += qt warn_on release
QT += declarative network
LIBS += -lbbcascades -lbbdata -lbbsystem -lbbmultimedia -lbb -lbps
QMAKE_CXXFLAGS += -Wno-psabi

SOURCES += src/main.cpp src/EngineLink.cpp src/NowPlaying.cpp
HEADERS += src/EngineLink.h src/NowPlaying.h src/UiLog.h

DESTDIR = app
OBJECTS_DIR = obj
MOC_DIR = moc
