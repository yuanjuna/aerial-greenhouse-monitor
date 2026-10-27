# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 预留位置：如需引入第三方组件，把组件目录放到 components/Middlewares 后取消下一行注释
# set(EXTRA_COMPONENT_DIRS components/Middlewares)

add_compile_options(-fdiagnostics-color=always)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aerial_greenhouse_monitor)
