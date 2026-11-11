# ============================================================
#  istar_install.cmake —— output/ 的两类内容，两种配置策略
#
#  命令：
#    release: cmake --build build --config Release     （deploy 目标已挂进 ALL）
#    debug  : cmake --build build --config Debug
#
#  output/ 里按"出口"分层，两类内容的配置策略不同：
#    1) output/exe/         给顶层装配的那一份        —— **只出 Release**
#       = exe/apps/<工程>/ 的全部内容（exe + 运行时 dll + qt.conf）。
#       顶层 assemble 整目录拷进 exe/apps/<工程>/，目标机跑的就是 release。
#       ★ 这里不放配置文件：现场配置由顶层 config/ 统一产出到 exe/config/，
#         子工程的 config/ 只服务 build/<Config>/ 的单独调试。
#    2) output/interface/   给下游工程/外部拿接口的那一份 —— **Release + Debug 都要**
#       别的工程在 debug 阶段要链接或动态加载 debug 版（motiond.dll、
#       libUIRE_CHIPd.dll 这类），这是各工程统一的约定。
#
#  实现方式：给 output/exe 的 install 规则加 CONFIGURATIONS Release。
#  于是 Debug 构建也直接 install 一次，只会落 interface 部分，
#  不会把 debug 的 exe 灌进部署形状。
#
#  每个目录都是"整目录替换"语义：install 之前先由 istar_clean_output.cmake
#  把本次配置会写的那部分删掉（见 istar_finalize_deploy）。
# ============================================================

# ============================================================
#  istar_install_app(<target> APP <工程名> [DIR <子目录>]
#                    [TARGETS <本工程的 target 名...>] [QT])
#
#  只产出"部署形状"，且只有 Release：
#    DIR     : 落点子目录，缺省 "."（= output/ 根）。
#              io/motion/re 用 DIR exe —— output/ 下按出口分层：
#                output/exe/        给顶层装配的那一份（= exe/apps/<工程>/ 的全部）
#                output/interface/  给下游工程/外部拿接口的那一份
#    TARGETS : 该程序运行时需要的、由本工程构建出来的 dll
#    QT      : 生成 qt.conf，指向顶层共享 Qt 层 exe/shared/system
#
#  注意：部署形状里**不放配置文件**。现场配置由顶层 config/ 统一出，
#  子工程的 config/ 只服务 build/<Config>/ 的单独调试（见 istar_stage）。
# ============================================================
function(istar_install_app TARGET)
    cmake_parse_arguments(I "QT" "APP;DIR" "TARGETS" ${ARGN})

    set(_dst ".")
    if(I_DIR)
        set(_dst "${I_DIR}")
    endif()

    install(TARGETS ${TARGET} RUNTIME DESTINATION "${_dst}" CONFIGURATIONS Release)

    # 只装运行时文件（$<TARGET_FILE:> = .dll），不把导入库 .lib 带进部署形状
    if(I_TARGETS)
        foreach(_t IN LISTS I_TARGETS)
            install(FILES "$<TARGET_FILE:${_t}>"
                    DESTINATION "${_dst}" CONFIGURATIONS Release)
        endforeach()
    endif()

    if(I_QT)
        # qt.conf 相对 exe 所在目录：apps/<工程>/ -> ../../shared/system
        # 装配后每个程序都在 exe/apps/<工程>/，所以与 DIR 无关、恒为两级
        set(_QT_CONF "${CMAKE_CURRENT_BINARY_DIR}/qt.conf")
        file(WRITE "${_QT_CONF}" "[Paths]\nPrefix=../../shared/system\nPlugins=.\n")
        install(FILES "${_QT_CONF}" DESTINATION "${_dst}" CONFIGURATIONS Release)
    endif()

    # 登记到全局，供 istar_finalize_deploy() 建立构建依赖 ——
    # 否则 deploy（挂进 ALL）会与各 target 并行、甚至在链接完成前就 install，
    # 报 "file INSTALL cannot find .../Release/xxx.dll"。
    set_property(GLOBAL APPEND PROPERTY ISTAR_INSTALL_APPS ${TARGET} ${I_TARGETS})
endfunction()

# ============================================================
#  istar_install_interface(<target> [SUB <用途名>] FILES <头文件...>)
#
#  下游集成包（给别的工程拿接口用，不进 exe）：
#    output/interface/[<用途名>/]include/              头文件
#    output/interface/[<用途名>/]lib/{debug,release}/   dll + 导入库
#  SUB 是"用途"那一级，约定用 rpc（协议完整包）或 gui（给 gui 的库）。
#  两种配置都装 —— 就是上面说的第 2 类内容。
# ============================================================
function(istar_install_interface TARGET)
    cmake_parse_arguments(I "" "SUB" "FILES" ${ARGN})

    set(_base "interface")
    if(I_SUB)
        set(_base "interface/${I_SUB}")
    endif()
    set(_libdir "${_base}/lib/$<IF:$<CONFIG:Debug>,debug,release>")

    install(TARGETS ${TARGET}
        RUNTIME DESTINATION "${_libdir}"
        LIBRARY DESTINATION "${_libdir}"
        ARCHIVE DESTINATION "${_libdir}")

    if(I_FILES)
        install(FILES ${I_FILES} DESTINATION "${_base}/include")
    endif()

    set_property(GLOBAL APPEND PROPERTY ISTAR_INSTALL_APPS ${TARGET})
endfunction()

# ============================================================
#  istar_finalize_deploy() —— 在顶层 CMakeLists.txt 的最后调用一次
#
#  把 install 挂进默认构建（ALL）：
#  于是 Qt Creator / VSCode / 命令行"构建一下"就都会产出 output/，
#  不需要额外加 build step，也不用记住再敲一次 cmake --install。
#
#  跑 install 之前先执行 istar_clean_output.cmake，把本次配置会写的那部分
#  删掉再重铺，使 output/ 具备"整目录替换"的语义（install 自己只增不删）。
#
#  --prefix 显式给出，不依赖 cache 里的 CMAKE_INSTALL_PREFIX：
#  即使某棵老构建树的 cache 还留着 CMake 默认前缀（Program Files），
#  这个目标也只会写进 <子工程根>/output。
# ============================================================
function(istar_finalize_deploy)
    get_property(_apps GLOBAL PROPERTY ISTAR_INSTALL_APPS)

    # ---- 清理步骤的驱动文件 ----
    # output 路径必须**写进文件**，不能用 `-DOUT="${ISTAR_OUTPUT_DIR}"` 从命令行传：
    # VS 生成器会把它写成 `-DOUT=\"D:/.../output\"`，而 cmd 不认 `\"` 这种转义
    # （那是 shell 的写法）—— cmake -D 拿到的值于是带字面反斜杠+引号，
    # 清理脚本对着一个不存在的路径 REMOVE_RECURSE，**静默什么也没删**，
    # 结果 output/ 永远只增不减（踩过：mctrl 的 output/exe 里 09-18 的
    # threadpool_config.yaml 一直躺着）。
    # CFG 用 -D 传是安全的：$<CONFIG> 展开后不含引号，生成的是 `-DCFG=Release`。
    set(_istar_clean_driver "${CMAKE_BINARY_DIR}/istar_clean_output_run.cmake")
    file(WRITE "${_istar_clean_driver}"
        "set(OUT \"${ISTAR_OUTPUT_DIR}\")\n"
        "include(\"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/istar_clean_output.cmake\")\n")

    add_custom_target(deploy ALL
        COMMAND ${CMAKE_COMMAND} -DCFG=$<CONFIG> -P "${_istar_clean_driver}"
        COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --config $<CONFIG>
                --prefix "${ISTAR_OUTPUT_DIR}"
        COMMENT "[istar] install -> ${ISTAR_OUTPUT_DIR}"
        VERBATIM)

    if(_apps)
        add_dependencies(deploy ${_apps})
    endif()
endfunction()
