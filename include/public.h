#pragma once
#include <iostream>
#include <ctime>

// 定义一个简单的日志宏 输出文件、行号、时间和自定义信息
#define LOG(str) \
    std::cout << __FILE__ << ":" << __LINE__ << " " << \
    __TIMESTAMP__ << " : " << str << std::endl;