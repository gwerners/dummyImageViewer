QT = core

CONFIG += c++20 cmdline

DEFINES += USING_QT

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

FMT += \
    fmt/src/format.cc \
    fmt/src/os.cc

IMGUI += \
    rlImGui/rlImGui.cpp \
    imgui/imgui.cpp \
    imgui/imgui_draw.cpp \
    imgui/imgui_tables.cpp \
    imgui/imgui_widgets.cpp \
    imgui/imgui_demo.cpp

SOURCES += \
        $${FMT} \
        $${IMGUI} \
        work/Task.cpp \
        work/Core.cpp \
        work/Utils.cpp \
        work/main.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    work/Const.h \
    work/Task.h \
    work/Core.h \
    work/Utils.h

INCLUDEPATH += \
    raylib/src \
    imgui \
    rlImGui \
    stb \
    fmt/include \
    json/include \
    work

LIBS += -L"$$_PRO_FILE_PWD_/raylib/build/raylib" -lraylib -lavif -lwebp -lpthread -lm
