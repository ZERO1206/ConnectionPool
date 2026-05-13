#pragma once
#include <mysql.h>
#include <string>
#include <chrono>

class Connection {
public:
    Connection();
    ~Connection();

    // 建立连接
    bool connect(std::string ip, unsigned short port, 
        std::string user, std::string password, std::string dbname);

    // 更新操作
    bool update(std::string sql);

    // 查询操作
    MYSQL_RES* query(std::string sql);

    // 刷新连接开始空闲的时间点
    void refreshAliveTime();

    // 获取连接已空闲的时间量 
    long long getAliveTime() const;

private:
    MYSQL* _conn;  // 表示和MySQL Server的一条连接
    std::chrono::steady_clock::time_point _alivetime;  // 记录进入空闲状态后的起始时刻
};