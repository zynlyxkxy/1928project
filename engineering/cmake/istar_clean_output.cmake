# ============================================================
#  istar_clean_output.cmake —— output/ 的"干净替换"前置步骤
#
#  由 istar_finalize_deploy() 挂在 deploy 目标里，在 cmake --install 之前跑。
#
#  为什么需要它：install() 只增改、从不删除多余文件。改了一个 dll 的名字、
#  从清单里去掉一个文件，目标目录里那个旧文件会永远留着 —— 于是消费方
#  没法"删掉目标目录、整份拷过去"。这里先把**本次配置会写的那部分**删掉，
#  再让 install 重新铺一遍，语义就变成原子替换。
#
#  参数（-D 传入）：
#    OUT = 子工程的 output 目录
#    CFG = 当前配置（Debug / Release）
#
#  删除范围刻意保守 —— 只碰"本次配置会重写"的东西，不碰另一个配置的产物：
#    <OUT>/exe                          部署形状（只有 Release 会写）
#    <OUT>/interface/lib/<cfg>          本配置的库目录
#    <OUT>/interface/<sub>/lib/<cfg>
#    <OUT>/interface/include            头文件（与配置无关，两种配置写同一份）
#    <OUT>/interface/<sub>/include
# ============================================================

if(NOT DEFINED OUT OR "${OUT}" STREQUAL "")
    message(FATAL_ERROR "istar_clean_output.cmake: OUT is not set")
endif()
if(NOT DEFINED CFG OR "${CFG}" STREQUAL "")
    message(FATAL_ERROR "istar_clean_output.cmake: CFG is not set")
endif()

string(TOLOWER "${CFG}" _cfg)
# 与 istar_install_interface() 的 <IF:$<CONFIG:Debug>,debug,release> 保持一致：
# 只有 Debug 归 debug，其余（Release/MinSizeRel/RelWithDebInfo）都归 release。
if(_cfg STREQUAL "debug")
    set(_cfgname "debug")
else()
    set(_cfgname "release")
endif()

if(_cfgname STREQUAL "release")
    file(REMOVE_RECURSE "${OUT}/exe")
endif()

file(GLOB _dirs
    "${OUT}/interface/lib/${_cfgname}"
    "${OUT}/interface/*/lib/${_cfgname}"
    "${OUT}/interface/include"
    "${OUT}/interface/*/include")
foreach(_d IN LISTS _dirs)
    file(REMOVE_RECURSE "${_d}")
endforeach()
