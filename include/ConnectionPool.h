#pragma once
#include <queue>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "Connection.h"

class ConnectionPool {
public:
    // 获取单例
    static ConnectionPool* getConnectionPool();
s
    // 获取一个可用的数据库连接
    std::shared_ptr<Connection> getConnection();

private:
    // 运行在独立线程中负责生产新连接的任务 
    void produceConnectionTask();

    // 空间连接清理线程
    void scannerConnectionTask();

private:
    // 加载配置文件
    bool loadConfigFile();

    // 运行在生产者线程 负责创建新连接
    void addConnection(); 

    // 数据库配置信息 
    std::string _ip;
    unsigned short _port;
    std::string _user;
    std::string _password;
    std::string _dbname;

    // 连接池性能参数
    int _initSize;           // 初始连接量
    int _maxSize;            // 最大连接量
    int _maxIdleTime;        // 最大空闲时间
    int _connectionTimeout;  // 连接超时时间

private: 
    // 构造函数私有化
    ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;

    ConnectionPool& operator=(const ConnectionPool&) = delete;

    ~ConnectionPool() {
        while(!_connectionQue.empty()) {
            Connection* p = _connectionQue.front();

            _connectionQue.pop();

            delete p;
        }
    }

    std::queue<Connection*> _connectionQue;  //空闲连接队列
    std::mutex _queueMutex;                  // 互斥锁
    std::condition_variable _cv;              // 条件变量
    std::atomic_int _connectionCnt {0};          // 记录连接池所创建的连接总数
};