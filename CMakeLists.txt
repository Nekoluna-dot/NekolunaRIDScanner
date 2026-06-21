cmake_minimum_required(VERSION 3.16)

# ================================================================
# RemoteID Scanner - NekolunaRID 无人机远程识别接收器
# ================================================================
#
# 用法:
#   idf.py set-target esp32s3 -DMAIN_DIR=main_rx
#   idf.py build
#
# 模拟器详见 esp32-crid-sim-OTA/ 子项目
# ================================================================

# 默认构建目标
if(NOT DEFINED MAIN_DIR)
    set(MAIN_DIR main_rx)
endif()

message(STATUS ">>> Building target: ${MAIN_DIR}")

# 强制将 MAIN_DIR 设为主组件目录，并添加 components 目录
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/${MAIN_DIR}
                         ${CMAKE_CURRENT_SOURCE_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(remoteid_scanner)