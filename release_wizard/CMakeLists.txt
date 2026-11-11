cmake_minimum_required(VERSION 3.16)
project(release_wizard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ============================================================
#  Qt 定位：QTDIR 优先，否则探测常见安装位置。
#  和子仓的 engineering/cmake/istar_common.cmake 保持同一套探测列表，
#  换一台电脑只要 Qt 装在常见位置就能直接构建，不需要改任何文件。
# ============================================================
if(NOT CMAKE_PREFIX_PATH)
    foreach(_rw_q IN ITEMS
            "$ENV{QTDIR}"
            "D:/Qt/Qt5.12.12/5.12.12/msvc2017_64"
            "C:/Qt/Qt5.12.12/5.12.12/msvc2017_64"
            "D:/Qt/5.15.2/msvc2019_64"
            "C:/Qt/5.15.2/msvc2019_64")
        if(_rw_q AND EXISTS "${_rw_q}/lib/cmake/Qt5")
            set(CMAKE_PREFIX_PATH "${_rw_q}")
            message(STATUS "[release_wizard] Qt prefix: ${_rw_q}")
            break()
        endif()
    endforeach()
    if(NOT CMAKE_PREFIX_PATH)
        message(WARNING
            "[release_wizard] Qt was not found in the usual locations.\n"
            "  Install Qt there, set QTDIR, or pass -DCMAKE_PREFIX_PATH=<qt-prefix>")
    endif()
endif()

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets)

add_executable(release_wizard WIN32
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/refreshworker.cpp
    src/refreshworker.h
    src/pages_overview_update.cpp
    src/pages_sync_release.cpp
    src/pages_logs.cpp
)

target_link_libraries(release_wizard PRIVATE Qt5::Widgets)

if (MSVC)
    target_compile_options(release_wizard PRIVATE /utf-8)
endif()

# ============================================================
#  Qt 运行时部署：链接成功后跑一次 windeployqt，
#  build_ui.bat 因此不再需要知道 Qt 装在哪里。
# ============================================================
if(WIN32)
    get_filename_component(_rw_qt_prefix "${Qt5Core_DIR}/../../.." ABSOLUTE)
    find_program(RW_WINDEPLOYQT windeployqt HINTS "${_rw_qt_prefix}/bin" "$ENV{QTDIR}/bin")
    if(RW_WINDEPLOYQT)
        add_custom_command(TARGET release_wizard POST_BUILD
            COMMAND "${RW_WINDEPLOYQT}"
                    "--$<IF:$<CONFIG:Debug>,debug,release>"
                    --no-translations
                    "$<TARGET_FILE:release_wizard>"
            COMMENT "[release_wizard] windeployqt")
    else()
        message(WARNING
            "[release_wizard] windeployqt not found - run the exe with Qt bin on PATH")
    endif()
endif()
