# ============================================================
#  istar_runtime.cmake —— 运行目录（build/<Config>/）自包含
#
#  目的：build/<Config>/ 里的 exe 能直接双击，也能被 Qt Creator / VSCode
#        直接启动，不需要配任何环境变量。
#
#  做法：平台基础库 basiclibrary_output/lib 本身就带一份
#        Qt5*.dll + platforms/，整目录由 istar_runtime_shared 统一拷一次
#        （见 istar_common.cmake），不需要 windeployqt。
#        gui 额外需要 WebEngine，平台那份 Qt 里没有 → istar_deploy_webengine()。
# ============================================================

# ============================================================
#  istar_deploy_runtime(<target>
#                        [TARGETS <本工程的 target 名...>]
#                        [DLLS <普通文件路径...>]
#                        [DIRS <目录...>])
#
#  TARGETS : 本工程构建出来的 dll，用 $<TARGET_FILE:> 解析
#  DLLS    : 已知路径的 dll（存在才拷，不存在给 WARNING）
#  DIRS    : 目录内容并入 exe 旁
#  平台基础库由 istar_runtime_shared 统一拷（这里只建立依赖）。
# ============================================================
function(istar_deploy_runtime TARGET)
    cmake_parse_arguments(R "" "" "TARGETS;DLLS;DIRS" ${ARGN})
    set(_DST "$<TARGET_FILE_DIR:${TARGET}>")

    add_dependencies(${TARGET} istar_runtime_shared)

    foreach(_t IN LISTS R_TARGETS)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DSRC="$<TARGET_FILE:${_t}>" -DDST="${_DST}" -DMODE=FILE
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/istar_copy.cmake"
            COMMENT "[istar] ${_t} -> ${_DST}")
    endforeach()

    foreach(_d IN LISTS R_DIRS)
        if(EXISTS "${_d}")
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -DSRC="${_d}" -DDST="${_DST}" -DMODE=DIR
                        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/istar_copy.cmake"
                COMMENT "[istar] dir ${_d} -> ${_DST}")
        else()
            message(WARNING "[istar] runtime dir not found, skipped: ${_d}")
        endif()
    endforeach()

    foreach(_f IN LISTS R_DLLS)
        if(EXISTS "${_f}")
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -DSRC="${_f}" -DDST="${_DST}" -DMODE=FILE
                        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/istar_copy.cmake"
                COMMENT "[istar] ${_f} -> ${_DST}")
        else()
            message(WARNING "[istar] runtime dep not found, skipped: ${_f}")
        endif()
    endforeach()
endfunction()

# ============================================================
#  istar_deploy_first(<target> CANDIDATES <候选文件...>)
#
#  按顺序取**第一个存在**的候选拷到 exe 旁；全都找不到时打印一行说明
#  （不是错误 —— 例如 gui 在 re 还没构建时就是这样）。
#  用途：gui 的 libUIRE_*.dll —— re 的产物优先，exe/lib/module 兜底。
# ============================================================
function(istar_deploy_first TARGET)
    cmake_parse_arguments(R "" "" "CANDIDATES" ${ARGN})
    set(_DST "$<TARGET_FILE_DIR:${TARGET}>")

    set(_pick "")
    foreach(_c IN LISTS R_CANDIDATES)
        if(_pick STREQUAL "" AND EXISTS "${_c}")
            set(_pick "${_c}")
        endif()
    endforeach()

    if(_pick STREQUAL "")
        message(STATUS "[istar] none of these exist, skipped: ${R_CANDIDATES}")
    else()
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DSRC="${_pick}" -DDST="${_DST}" -DMODE=FILE
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/istar_copy.cmake"
            COMMENT "[istar] ${_pick} -> ${_DST}")
    endif()
endfunction()

# ============================================================
#  istar_deploy_webengine(<target>)
#
#  只给 gui 用：平台基础库那份 Qt 没有 WebEngine，必须从 Qt 安装目录补。
#  Qt 安装缺 WebEngine 时直接 FATAL_ERROR（而不是等 POST_BUILD 神秘失败）。
# ============================================================
function(istar_deploy_webengine TARGET)
    set(_DST "$<TARGET_FILE_DIR:${TARGET}>")
    set(_D "$<$<CONFIG:Debug>:d>")

    # Qt5_DIR = <prefix>/lib/cmake/Qt5  ->  <prefix>
    get_filename_component(_QT_ROOT "${Qt5_DIR}/../../.." ABSOLUTE)
    if(NOT EXISTS "${_QT_ROOT}/bin/QtWebEngineProcess.exe")
        message(FATAL_ERROR
            "istar_deploy_webengine: QtWebEngine not found under ${_QT_ROOT}\n"
            "  gui 需要 WebEngine，请检查 Qt 安装是否包含该模块")
    endif()

    # 9 个 Qt 模块 dll（带配置后缀）
    foreach(_m Qt5WebEngineWidgets Qt5WebEngineCore Qt5WebEngine Qt5WebChannel
              Qt5Quick Qt5QuickWidgets Qt5Qml Qt5Positioning Qt5Sensors)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_QT_ROOT}/bin/${_m}${_D}.dll" "${_DST}"
            COMMENT "[istar] webengine ${_m}${_D}.dll -> ${_DST}")
    endforeach()

    # 图形后端（带配置后缀）
    foreach(_m libEGL libGLESv2)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_QT_ROOT}/bin/${_m}${_D}.dll" "${_DST}"
            COMMENT "[istar] webengine ${_m}${_D}.dll -> ${_DST}")
    endforeach()

    # 这两个不带配置后缀
    foreach(_m d3dcompiler_47.dll QtWebEngineProcess${_D}.exe)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_QT_ROOT}/bin/${_m}" "${_DST}"
            COMMENT "[istar] webengine ${_m} -> ${_DST}")
    endforeach()

    # Chromium 资源：必须和 exe 同级
    foreach(_m icudtl.dat qtwebengine_resources.pak qtwebengine_resources_100p.pak
              qtwebengine_resources_200p.pak qtwebengine_devtools_resources.pak)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_QT_ROOT}/resources/${_m}" "${_DST}"
            COMMENT "[istar] webengine ${_m} -> ${_DST}")
    endforeach()

    if(EXISTS "${_QT_ROOT}/translations/qtwebengine_locales")
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${_QT_ROOT}/translations/qtwebengine_locales" "${_DST}/qtwebengine_locales"
            COMMENT "[istar] webengine locales -> ${_DST}")
    endif()
endfunction()

# ============================================================
#  istar_stage(<target> SRC <文件或目录> [TO <子目录>] [OPTIONAL])
#
#  通用的"放到 exe 旁"一步：源可以是文件也可以是目录。
#  源不存在时：OPTIONAL -> STATUS 说明（例如外层 config 不存在，独立构建正常）；
#              否则 -> WARNING（不再像旧脚本那样静默跳过）。
#  各工程用它逐条描述自己的 config 形状，比强行套一个统一函数清楚。
# ============================================================
function(istar_stage TARGET)
    cmake_parse_arguments(S "OPTIONAL" "SRC;TO" "" ${ARGN})
    if(NOT S_SRC)
        if(S_OPTIONAL)
            return()
        endif()
        message(FATAL_ERROR "istar_stage(${TARGET}): SRC is required")
    endif()

    if(IS_DIRECTORY "${S_SRC}")
        set(_copy_mode DIR)
    else()
        set(_copy_mode FILE)
    endif()

    set(_dst "$<TARGET_FILE_DIR:${TARGET}>")
    if(S_TO)
        set(_dst "${_dst}/${S_TO}")
    endif()

    if(NOT EXISTS "${S_SRC}")
        if(S_OPTIONAL)
            message(STATUS "[istar] not present, skipped: ${S_SRC}")
        else()
            message(WARNING "[istar] source not found, skipped: ${S_SRC}")
        endif()
        return()
    endif()

    if(S_TO)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}")
    endif()

    if(_copy_mode STREQUAL DIR)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DSRC="${S_SRC}" -DDST="${_dst}" -DMODE=DIR
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/istar_copy.cmake"
            COMMENT "[istar] ${S_SRC} -> ${_dst}")
    else()
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DSRC="${S_SRC}" -DDST="${_dst}" -DMODE=FILE
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/istar_copy.cmake"
            COMMENT "[istar] ${S_SRC} -> ${_dst}")
    endif()
endfunction()
