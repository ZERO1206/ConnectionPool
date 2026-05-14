#include "ConnectionPool.h"
#include "public.h"
#include <chrono>
#include <thread>
#include <functional>
#include <fstream>

ConnectionPool* ConnectionPool::getConnectionPool() {
    static ConnectionPool pool;
    return &pool;
}

ConnectionPool::ConnectionPool() { 
    // 加载配置文件（获取数据库连接信息及性能参数）
    if(!loadConfigFile()) {
        LOG("Configuration file loading failed");
        return;
    }

    // 创造初始连接量
    for(int i = 0; i < _initSize; ++i) {
        addConnection();
    }

    // 启动生产者线程
    std::thread produce(std::bind(&ConnectionPool::produceConnectionTask, this));
    /*
    std::thread produce([this]() { 
        this->produceConnectionTask(); 
    });
    */

    // 启动空间连接清理线程
    std::thread scanner(std::bind(&ConnectionPool::scannerConnectionTask, this));

    // 设置线程分离
    produce.detach();
    scanner.detach();
}

bool ConnectionPool::loadConfigFile() {
    // 打开文件
    std::ifstream ifs("mysql.ini");
    if(!ifs.is_open()) {
        LOG("The mysql.ini file does not exist.");
        return false;
    }

    std::string line;
    while(std::getline(ifs, line)) {
        // 查找等号位置
        size_t idx = line.find('=');
        if(idx == std::string::npos) continue;

        // 截取键和值
        std::string key = line.substr(0, idx);
        std::string val = line.substr(idx+1);

        // 匹配并赋值
        if (key == "ip") _ip = val;
        else if (key == "port") _port = std::stoi(val);
        else if (key == "username") _user = val;
        else if (key == "password") _password = val;
        else if (key == "dbname") _dbname = val;
        else if (key == "initSize") _initSize = std::stoi(val);
        else if (key == "maxSize") _maxSize = std::stoi(val);
        else if (key == "maxIdleTime") _maxIdleTime = std::stoi(val);
        else if (key == "connectionTimeout") _connectionTimeout = std::stoi(val);
    }
    return true;
}

void ConnectionPool::addConnection() {
    Connection* p = new Connection();  // 创建连接对象

    if(p->connect(_ip, _port, _user, _password, _dbname)) {
        // 更新连接的进入队列时间戳（用于后续空闲回收） 
        p->refreshAliveTime();
        _connectionQue.push(p);  // 修改了共享队列
        _connectionCnt++;  // 修改了共享计数器
    }
    else {
        delete p;  // 连接失败销毁
        LOG("Connection creation failed. Please check your database configuration or network.");
    }
}

std::shared_ptr<Connection> ConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(_queueMutex);

    // 检查队列是否为空
    while(_connectionQue.empty()) {
        // 通知生产者线程
        _cv.notify_all();

        if(std::cv_status::timeout == _cv.wait_for(lock, std::chrono::milliseconds(_connectionTimeout))) {
            if(_connectionQue.empty()) {
                LOG("Connection Pool: Get connection timeout, failed!");
                throw std::runtime_error("Get connection timeout from pool");
            }
        }
    }

    std::shared_ptr<Connection> sp(_connectionQue.front(), 
        [this](Connection *pcon) {
            std::unique_lock<std::mutex> lock(_queueMutex);
            pcon->refreshAliveTime(); // 归还前刷新空闲起始时间
            _connectionQue.push(pcon); // 重新放回池子
            _cv.notify_all(); // 通知有连接可用了
        });
    
    _connectionQue.pop();

    return sp;
}

void ConnectionPool::produceConnectionTask() {
    while(true) {
        std::unique_lock<std::mutex> lock(_queueMutex);

        while (!_connectionQue.empty())
        {
            _cv.wait(lock);
        }

        if (_connectionCnt < _maxSize)
        {
            addConnection();
        }

        _cv.notify_all();
    }
}

void ConnectionPool::scannerConnectionTask() {
    while(true) {
        // 定时执行scanner线程
        std::this_thread::sleep_for(std::chrono::seconds(_maxIdleTime));

        std::unique_lock<std::mutex> lock(_queueMutex);
        // 检查条件
        // 1、当前连接总数 _connectionCnt > _initSize
        while(_connectionCnt > _initSize && !_connectionQue.empty()) {
            Connection* p = _connectionQue.front();
            // 获取连接的空闲时长
            if (p->getAliveTime() >= (_maxIdleTime * 1000)) {
                _connectionQue.pop();
                _connectionCnt--;
                delete p; // 调用析构函数 关闭数据库连接 (mysql_close)
            } else {
                break;
            }
        }
    }
}