cmake_minimum_required(VERSION 3.10)

project(ConnectionPool)

set(CMAKE_CXX_STANDARD 11)

include_directories(
    "D:/MySQL/mysql-8.0.46-winx64/include"
    "include"
)

link_directories(
    "D:/MySQL/mysql-8.0.46-winx64/lib"
)

add_executable(main
    src/main.cpp
    src/Connection.cpp
    src/ConnectionPool.cpp
)

target_link_libraries(main
    libmysql
)