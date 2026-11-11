# ============================================================
#  istar_rpc.cmake —— RPC 代码生成
#
#  与旧做法的区别：
#    1) 生成物只落 build 树（GEN_DIR），**不回写源码树**
#       —— 源码树永远干净，也不再需要"删源码树里的生成头来驱动重生成"
#    2) 增量判定只靠 CMake 的 OUTPUT/DEPENDS，没有手写 mtime 比较
#       （旧 mctrl/build.bat 用 `%~tf GTR %~tg` 比字符串，换区域设置就会错乱）
#    3) 生成后修补集中在这里的 PATCH_SCRIPT 钩子，各工程自己提供脚本，
#       不再散落在 build.bat 的 PowerShell 里
#
#  istar_gen_rpc(<proto>
#                API_DIR <dir>             手写的 <proto>_controller.h / <proto>c_data.h 所在
#                GEN_DIR <dir>             生成物落点，必须在 build 树内，末级必须等于协议名
#                [PATCH_SCRIPT <path.cmake>]  生成后修补脚本（路径由调用方给出）
#                [IFACE_TARGET <name>])    额外导出一个 INTERFACE target
#
#  结果变量：ISTAR_RPC_<proto>_SOURCES（在调用者作用域）
# ============================================================
function(istar_gen_rpc PROTO)
    cmake_parse_arguments(R "" "API_DIR;GEN_DIR;PATCH_SCRIPT;IFACE_TARGET" "" ${ARGN})
    if(NOT R_API_DIR OR NOT R_GEN_DIR)
        message(FATAL_ERROR "istar_gen_rpc(${PROTO}): API_DIR and GEN_DIR are required")
    endif()
    if(NOT EXISTS "${ISTAR_GENERATOR}")
        message(FATAL_ERROR
            "istar_gen_rpc(${PROTO}): RPC generator not found\n  ${ISTAR_GENERATOR}\n"
            "Initialize the platform submodule first: git submodule update --init basiclibrary_output")
    endif()

    # 生成器会在 -o 指定的目录下再建一级 <proto>/，所以 -o 要传 GEN_DIR 的父目录
    get_filename_component(_GEN_ROOT "${R_GEN_DIR}" DIRECTORY)
    get_filename_component(_GEN_NAME "${R_GEN_DIR}" NAME)
    if(NOT _GEN_NAME STREQUAL "${PROTO}")
        message(FATAL_ERROR
            "istar_gen_rpc(${PROTO}): GEN_DIR must end with the protocol name (got '${_GEN_NAME}')\n"
            "  GEN_DIR = ${R_GEN_DIR}")
    endif()

    set(_SRC "${R_GEN_DIR}/source")
    set(_INC "${R_GEN_DIR}/include")
    set(ISTAR_RPC_SOURCES
        "${_SRC}/${PROTO}_client_original.cpp"
        "${_SRC}/${PROTO}_client.cpp"
        "${_SRC}/${PROTO}_serialize.cpp"
        "${_SRC}/${PROTO}_server.cpp"
        "${_SRC}/${PROTO}_server_impl.cpp")
    set(ISTAR_RPC_HEADERS
        "${_INC}/${PROTO}_client.h"
        "${_INC}/${PROTO}_server.h"
        "${_INC}/${PROTO}_define.h")

    set(_CMDS
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${R_GEN_DIR}"
        COMMAND "${ISTAR_GENERATOR}" -d "${R_API_DIR}" -o "${_GEN_ROOT}")

    # 生成后修补：把脚本交给 cmake -P 跑，脚本里自己决定怎么改
    if(R_PATCH_SCRIPT)
        if(NOT EXISTS "${R_PATCH_SCRIPT}")
            message(FATAL_ERROR "istar_gen_rpc(${PROTO}): patch script not found: ${R_PATCH_SCRIPT}")
        endif()
        list(APPEND _CMDS
            COMMAND ${CMAKE_COMMAND}
                -DPROTO=${PROTO} -DGEN_DIR=${R_GEN_DIR} -DAPI_DIR=${R_API_DIR}
                -P "${R_PATCH_SCRIPT}")
    endif()

    add_custom_command(
        OUTPUT ${ISTAR_RPC_SOURCES} ${ISTAR_RPC_HEADERS}
        ${_CMDS}
        DEPENDS
            "${R_API_DIR}/${PROTO}_controller.h"
            "${R_API_DIR}/${PROTO}c_data.h"
            "${ISTAR_GENERATOR}"
        COMMENT "[istar] generate ${PROTO} RPC code -> ${R_GEN_DIR}"
        VERBATIM)

    set(ISTAR_RPC_${PROTO}_SOURCES ${ISTAR_RPC_SOURCES} ${ISTAR_RPC_HEADERS} PARENT_SCOPE)

    if(R_IFACE_TARGET)
        add_library(${R_IFACE_TARGET} INTERFACE)
        target_include_directories(${R_IFACE_TARGET} INTERFACE "${_INC}")
    endif()
endfunction()
