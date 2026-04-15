cmake_minimum_required(VERSION 3.21)
project(MyWhisperQt VERSION 1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(X11_XTEST REQUIRED IMPORTED_TARGET x11 xtst)
endif()
qt_standard_project_setup()

set(MYWHISPER_QT_SOURCES
    src/main.cpp
    src/app_controller.cpp
    src/audio_recorder.cpp
    src/config.cpp
    src/global_hotkey_manager.cpp
    src/history_dialog.cpp
    src/hotkey_formatter.cpp
    src/llm_refiner.cpp
    src/platform_integration.cpp
    src/settings_dialog.cpp
    src/status_overlay.cpp
    src/transcriber.cpp
    src/transcription_history_store.cpp
)

if(APPLE)
    list(REMOVE_ITEM MYWHISPER_QT_SOURCES
        src/global_hotkey_manager.cpp
        src/platform_integration.cpp
    )
    list(APPEND MYWHISPER_QT_SOURCES
        src/global_hotkey_manager_mac.mm
        src/platform_integration_mac.mm
    )
endif()

qt_add_executable(MyWhisperQt
    MACOSX_BUNDLE
    ${MYWHISPER_QT_SOURCES}
    resources/resources.qrc
    src/app_controller.h
    src/audio_recorder.h
    src/config.h
    src/global_hotkey_manager.h
    src/history_dialog.h
    src/hotkey_formatter.h
    src/llm_refiner.h
    src/platform_integration.h
    src/settings_dialog.h
    src/status_overlay.h
    src/transcriber.h
    src/transcription_history_store.h
    src/types.h
)

target_link_libraries(MyWhisperQt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
)

if(UNIX AND NOT APPLE)
    target_link_libraries(MyWhisperQt PRIVATE PkgConfig::X11_XTEST)
endif()

if(APPLE)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(APPLICATION_SERVICES_FRAMEWORK ApplicationServices REQUIRED)
    find_library(CARBON_FRAMEWORK Carbon REQUIRED)

    target_link_libraries(MyWhisperQt PRIVATE
        ${APPKIT_FRAMEWORK}
        ${APPLICATION_SERVICES_FRAMEWORK}
        ${CARBON_FRAMEWORK}
    )

    set_target_properties(MyWhisperQt PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME MyWhisperQt
        MACOSX_BUNDLE_GUI_IDENTIFIER com.mywhisper.qt
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
endif()

add_executable(SoundProbe
    tests/sound_probe.cpp
    src/platform_integration.cpp
    src/platform_integration.h
    resources/resources.qrc
)

target_include_directories(SoundProbe PRIVATE
    src
)

target_link_libraries(SoundProbe PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
)

install(TARGETS MyWhisperQt SoundProbe
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
