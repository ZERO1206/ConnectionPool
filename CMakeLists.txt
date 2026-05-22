cmake_minimum_required(VERSION 3.10)

project(ConnectionPool)

set(CMAKE_CXX_STANDARD 11)

# 包含头文件路径：引入 Linux 系统默认的 MySQL 头文件路径和项目的 include 目录
include_directories(
    "/usr/include/mysql"
    "include"
)

# 声明生成可执行文件
add_executable(main
    src/main.cpp
    src/Connection.cpp
    src/ConnectionPool.cpp
)

# 让 main 链接 Linux 下的 mysqlclient 动态库
target_link_libraries(main
    mysqlclient
)