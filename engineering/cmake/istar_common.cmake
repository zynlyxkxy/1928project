# ============================================================
#  istar_common.cmake —— 各子工程共用的构建设置
#
#  用法（子工程顶层 CMakeLists.txt）：
#      list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
#      include(istar_common)
#
#  本文件随子工程一起入库（<子工程>/cmake/），所以子工程可独立构建：
#  所有路径都从 CMAKE_SOURCE_DIR（子工程根）出发，不依赖任何上层工程。
#  规范源在 1927project/cmake/，由 scripts 的同步边 A9 镜像到各子工程。
# ============================================================

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# debug 产物加 d 后缀（motion.dll -> motiond.dll），与各工程原有行为一致。
# 注意：实测对 add_executable 不生效（exe 名不变），只影响库。
set(CMAKE_DEBUG_POSTFIX "d")

# 下列选项对 5 个工程都有益、且不改变生成代码的语义：
#   /MP      并行编译
#   /utf-8   源码为 UTF-8
#   /bigobj  gui / mctrl 的巨型 TU（AUTOUIC 生成的 ui_*.h）需要，缺了必报 C1128
if(MSVC)
    add_compile_options(/MP /utf-8 /bigobj)
    # Qt5 + C++17 下 std::byte 与 Qt 头冲突，统一关掉
    add_compile_definitions(_HAS_STD_BYTE=0)
endif()

# ---- 平台基础库：ISTAR*.dll/.lib，并自带一份 Qt5*.dll + platforms/ ----
#
# 允许子工程在 include(istar_common) 之前覆盖这三项：module/re 的平台基础库
# 不在自己目录下，而在 platform 子仓里（platform/basiclibrary_output/...）。
if(NOT DEFINED BASIC_LIB)
    set(BASIC_LIB    "${CMAKE_SOURCE_DIR}/basiclibrary_output/lib")
endif()
if(NOT DEFINED BASIC_INC)
    set(BASIC_INC    "${CMAKE_SOURCE_DIR}/basiclibrary_output/include")
endif()
if(NOT DEFINED BASIC_CONFIG)
    set(BASIC_CONFIG "${CMAKE_SOURCE_DIR}/basiclibrary_output/config")
endif()

# ---- RPC 代码生成器（平台子模块自带的可执行文件） ----
if(NOT DEFINED ISTAR_GENERATOR)
    set(ISTAR_GENERATOR "${CMAKE_SOURCE_DIR}/basiclibrary_output/script/ISTARMWGenerate.exe")
endif()

# ---- 运行目录自包含的来源目录 ----
#  默认就是平台基础库（各工程的 basiclibrary_output/lib）。
#  module/re 覆盖成"平台拍平后的部署集" platform/deploy/<Config>
#  （ISTAR/Qt5/rm + platforms/styles + config）。
if(NOT DEFINED ISTAR_RUNTIME_SRC)
    set(ISTAR_RUNTIME_SRC "${BASIC_LIB}")
endif()

# ============================================================
#  外层工程（1927project）的 config 覆盖 —— 可选的便利，独立构建自动跳过
#
#  向上探测：找到同时含 .gitmodules 和 exe_template 的目录即为整机工程根，
#  它的 config/ 覆盖进本工程 build/<Config>/（现场配置调试用）。
#  找不到就留空 —— 独立 clone 的子工程走这条，用自己仓库里的 config/。
#  这里不引入任何硬依赖：本文件随子工程入库，缺了外层照样能构建。
# ============================================================
set(OUTER_CONFIG_DIR "")
set(_istar_up "${CMAKE_SOURCE_DIR}")
foreach(_istar_i RANGE 1 5)
    get_filename_component(_istar_up "${_istar_up}" DIRECTORY)
    if(EXISTS "${_istar_up}/.gitmodules" AND EXISTS "${_istar_up}/exe_template")
        set(OUTER_CONFIG_DIR "${_istar_up}/config")
        break()
    endif()
endforeach()

if(OUTER_CONFIG_DIR)
    message(STATUS "[istar] outer config override: ${OUTER_CONFIG_DIR}")
else()
    message(STATUS "[istar] no outer project above ${CMAKE_SOURCE_DIR}"
                   " - standalone build, outer config override disabled")
endif()

# ---- 部署形状目录 = <子工程根>/output ----
#
#  必须"自愈"，不能只在首次生效：CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT
#  只在第一次 configure 时为真。已有的构建树（例如 Qt Creator 早先建的）cache 里
#  会永久留着 CMake 默认前缀 C:/Program Files (x86)/<project>，重新 configure 也
#  不会变 —— 于是挂进默认构建的 install 每次都去写 Program Files，直接权限报错。
set(ISTAR_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT
   OR CMAKE_INSTALL_PREFIX MATCHES "^[A-Za-z]:/Program Files")
    set(CMAKE_INSTALL_PREFIX "${ISTAR_OUTPUT_DIR}"
        CACHE PATH "本工程的部署形状目录" FORCE)
endif()

# ============================================================
#  Qt 定位
#  优先环境变量 QTDIR（机器级一次性设置），否则探测常见安装位置。
#  这样换一台电脑只要有 Qt（装在常见位置）就能直接构建、直接 F5，
#  .vscode 和 CMakePresets.json 里都不需要写任何绝对路径。
# ============================================================
if(NOT CMAKE_PREFIX_PATH)
    foreach(_istar_q IN ITEMS
            "$ENV{QTDIR}"
            "D:/Qt/Qt5.12.12/5.12.12/msvc2017_64"
            "C:/Qt/Qt5.12.12/5.12.12/msvc2017_64"
            "D:/Qt/5.15.2/msvc2019_64"
            "C:/Qt/5.15.2/msvc2019_64")
        if(_istar_q AND EXISTS "${_istar_q}/lib/cmake/Qt5")
            set(CMAKE_PREFIX_PATH "${_istar_q}")
            message(STATUS "[istar] Qt prefix: ${_istar_q}")
            break()
        endif()
    endforeach()
    if(NOT CMAKE_PREFIX_PATH)
        message(WARNING
            "[istar] Qt was not found in the usual locations.\n"
            "  Install Qt there, or set QTDIR, or pass -DCMAKE_PREFIX_PATH=<qt-prefix>")
    endif()
endif()

# ============================================================
#  共享运行时：把平台基础库（含 Qt）拷进本配置的输出目录
#
#  在顶层建立，一个工程只拷一次。所有 exe 都 add_dependencies 到它
#  （见 istar_runtime.cmake），避免并行构建时多个 target 抢着拷同一批文件
#  —— 那样 copy_directory_if_different 会偶发失败（"Error copying directory"）。
#  目标目录和 istar_add_app() 设的 RUNTIME_OUTPUT_DIRECTORY_<CONFIG> 一致。
# ============================================================
add_custom_target(istar_runtime_shared
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${ISTAR_RUNTIME_SRC}" "${CMAKE_BINARY_DIR}/$<CONFIG>"
    COMMENT "[istar] platform base lib -> ${CMAKE_BINARY_DIR}/$<CONFIG>"
    VERBATIM)

# ============================================================
#  istar_add_app(<名字> SOURCES <源文件...> [WIN32])
#
#  统一 exe 落点：build/<Config>/（平铺），让所有工程的 IDE launch 路径一致。
#  只负责建 target 和定输出目录；拷贝见 istar_runtime / istar_install。
# ============================================================
function(istar_add_app NAME)
    cmake_parse_arguments(APP "WIN32" "" "SOURCES" ${ARGN})
    if(NOT APP_SOURCES)
        message(FATAL_ERROR "istar_add_app(${NAME}): SOURCES is required")
    endif()

    if(APP_WIN32)
        add_executable(${NAME} WIN32 ${APP_SOURCES})
    else()
        add_executable(${NAME} ${APP_SOURCES})
    endif()

    set_target_properties(${NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_BINARY_DIR}/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_BINARY_DIR}/MinSizeRel")
endfunction()

# ============================================================
#  让 IDE（VSCode + CMake Tools）总有 IntelliSense 数据可用
#
#  CMake 只在 <build>/.cmake/api/v1/query/ 下存在"客户端查询"时才写 File API
#  的 reply（<build>/.cmake/api/v1/reply/*.json）；CMake Tools 的 IntelliSense
#  provider 读的就是那份 reply。它会自己放查询，但 clean.bat 会把整个 build\
#  删掉、纯脚本构建也不放查询 —— 于是 reply 缺失，provider 拿不到任何 include
#  路径（表现为满屏"找不到头文件"）。
#  这里在 configure 时补一个中立的客户端查询（client-istar），从下一次
#  configure 起就会始终生成 reply。纯文本写入，不影响任何编译行为。
# ============================================================
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/.cmake/api/v1/query/client-istar")
if(NOT EXISTS "${CMAKE_BINARY_DIR}/.cmake/api/v1/query/client-istar/query.json")
    file(WRITE "${CMAKE_BINARY_DIR}/.cmake/api/v1/query/client-istar/query.json"
        "{\"requests\":[{\"kind\":\"codemodel\",\"version\":2},"
        "{\"kind\":\"cache\",\"version\":2},"
        "{\"kind\":\"cmakeFiles\",\"version\":1},"
        "{\"kind\":\"toolchains\",\"version\":1}]}")
endif()
