# BBMusic's UI, a plain executable as the bar's Qnx/Elf entry point. Built out of tree by
# scripts/build-app.sh (build/app-ui).
TEMPLATE = app
TARGET = bbmusic
CONFIG += qt warn_on release
QT += declarative network
LIBS += -lbbcascades -lbbdata -lbbsystem -lbb -lbps
QMAKE_CXXFLAGS += -Wno-psabi

SOURCES += src/main.cpp src/EngineLink.cpp src/PlayerState.cpp src/Thumbs.cpp src/TubeSignIn.cpp src/SignInCard.cpp
HEADERS += src/EngineLink.h src/PlayerState.h src/Thumbs.h src/TubeSignIn.h src/SignInCard.h src/UiLog.h

DESTDIR = app
OBJECTS_DIR = obj
MOC_DIR = moc

# The .ts files: scripts/build-app.sh updates them from the QML and compiles them to build/qm.
TRANSLATIONS += translations/bbmusic_en.ts translations/bbmusic_it.ts translations/bbmusic_fr.ts translations/bbmusic_de.ts \
    translations/bbmusic_pl.ts translations/bbmusic_ru.ts translations/bbmusic_uk.ts \
    translations/bbmusic_hi.ts translations/bbmusic_vi.ts
