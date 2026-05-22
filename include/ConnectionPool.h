#pragma once
#include <queue>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "Connection.h"


struct PoolStats {
    int totalConnections;       // 当前存在的连接总数(_connectionCnt)
    int idleConnections;        // 队列中空闲的连接数(加锁读queue.size())
    int activeConnections;      // 借出在用的连接数 = total - idle
    int waitingThreads;         // 当前在getConnection()中阻塞等待的线程数
    long long totalBorrows;     // 累计成功借用次数
    long long totalTimeouts;    // 累计超时失败次数
    double avgWaitMs;           // 累计等待时长(毫秒)
};

class ConnectionPool {
public:
    // 获取单例
    static ConnectionPool* getConnectionPool();

    // 获取一个可用的数据库连接
    std::shared_ptr<Connection> getConnection();

    // 获取连接池运行状态
    PoolStats getStats() const;

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
    std::string _ip = "127.0.0.1";
    unsigned short _port = 3306;
    std::string _user = "root";
    std::string _password = "";
    std::string _dbname = "test";

    // 连接池性能参数
    int _initSize = 5;              // 初始连接量
    int _maxSize = 100;             // 最大连接量
    int _maxIdleTime = 60;          // 最大空闲时间
    int _connectionTimeout = 1000;  // 连接超时时间
    std::string _warmupSql;         // 连接预热SQL

    std::atomic_long _totalBorrows{0};  // 累计借用成功次数
    std::atomic_long _totalTimeouts{0}; // 累计超时失败次数
    std::atomic_long _totalWaitMs{0};   // 累计等待时长(毫秒)
    std::atomic_int _waitingThreads{0}; // 当前正在等待的线程数

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
    mutable std::mutex _queueMutex;          // 互斥锁
    std::condition_variable _cv;             // 条件变量
    std::atomic_int _connectionCnt {0};      // 记录连接池所创建的连接总数
};