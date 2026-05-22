#pragma once
#include <mysql.h>
#include <string>
#include <chrono>

class Connection {
public:
    Connection();

    Connection(const Connection&) = delete;

    Connection& operator=(const Connection&) = delete;

    ~Connection();

    // 建立连接
    bool connect(const std::string& ip, 
                const unsigned short& port, 
                const std::string& user,
                const std::string& password, 
                const std::string& dbname);

    // 更新操作
    bool update(const std::string& sql);

    // 查询操作
    MYSQL_RES* query(const std::string& sql);

    // 检测连接是否存活
    bool isAlive();

    // 刷新连接开始空闲的时间点
    void refreshAliveTime();

    // 获取连接已空闲的时间量 
    long long getAliveTime() const;

private:
    MYSQL* _conn;  // 表示和MySQL Server的一条连接
    std::chrono::steady_clock::time_point _alivetime;  // 记录进入空闲状态后的起始时刻
};