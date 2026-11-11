# ============================================================
#  istar_copy.cmake —— 带重试的拷贝（供 istar_stage 用）
#
#  一个工程里的多个 target（io 的 CTL+GUI、re 的 Chip+Wafer）POST_BUILD 会往
#  **同一个** build/<Config>/ 拷同名文件。MSBuild 并行跑这两个 target 时，
#  Windows 会让其中一个 CreateFile 撞上共享冲突，报
#      Error copying file (if different) from ... to ...
#  （CMake 的 copy_if_different / copy_directory_if_different 都不重试；
#   clean 之后的第一次构建最容易撞上，因为目标文件还不存在、两边都要创建。
#   文档里记过同类问题：平台基础库整目录拷贝当初就是并行抢跑，才改成
#   单个 istar_runtime_shared 串行拷一次。）
#
#  这里失败后小睡再试 —— 纯健壮性修补，不改落点、不改语义，
#  `-t <单 target>` 构建照旧由该 target 自己完成自己的拷贝。
#
#  参数（-D 传入）：
#    SRC / DST       源 / 目标（文件或目录）
#    MODE            FILE（缺省）| DIR
# ============================================================

if(NOT DEFINED SRC OR "${SRC}" STREQUAL "" OR NOT DEFINED DST OR "${DST}" STREQUAL "")
    message(FATAL_ERROR "istar_copy.cmake: SRC and DST are required")
endif()
if(NOT DEFINED MODE OR "${MODE}" STREQUAL "")
    set(MODE FILE)
endif()

foreach(_try RANGE 1 10)
    if(MODE STREQUAL "DIR")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${SRC}" "${DST}"
            RESULT_VARIABLE _rc)
    else()
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SRC}" "${DST}"
            RESULT_VARIABLE _rc)
    endif()
    if(_rc EQUAL 0)
        return()
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.3)
endforeach()

message(FATAL_ERROR "istar_copy: still failing after 10 tries: ${SRC} -> ${DST}")
