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

// 解决stoi安全问题
static bool parseInt(const std::string& s, int& out) {
    try{
        size_t pos;
        out = std::stoi(s, &pos);
        return pos == s.size();
    }
    catch(const std::exception&) {
        return false;
    }
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
        if (key == "ip") {
            _ip = val;
        }
        else if (key == "port") {
            int tmp;
            if(!parseInt(val, tmp)) {
                LOG("Invalid port value:" + val);
                return false;
            }
            _port = static_cast<unsigned short>(tmp);
        } 
        else if (key == "username") {
            _user = val;
        } 
        else if (key == "password") {
            _password = val;
        }
        else if (key == "dbname") {
            _dbname = val;
        }
        else if (key == "initSize") {
            int tmp;
            if (parseInt(val, tmp)) {
                _initSize = tmp;
            } 
            else {
                LOG("Invalid initSize, using default: " + std::to_string(_initSize));
            }
        }
        else if (key == "maxSize") {
            int tmp;
            if (parseInt(val, tmp)) {
                _maxSize = tmp;
            } 
            else {
                LOG("Invalid maxSize, using default: " + std::to_string(_maxSize));
            }
        }
        else if (key == "maxIdleTime") {
            int tmp;
            if (parseInt(val, tmp)) {
                _maxIdleTime = tmp;
            } 
            else {
                LOG("Invalid maxIdleTime, using default: " + std::to_string(_maxIdleTime));
            }
        }
        else if (key == "connectionTimeout") {
            int tmp;
            if (parseInt(val, tmp)) {
                _connectionTimeout = tmp;
            } 
            else {
                LOG("Invalid connectionTimeout, using default: " + std::to_string(_connectionTimeout));
            }
        }
        else if (key == "warmupSql") {
            _warmupSql = val;
        }
    }

    if(_ip.empty()) {
        LOG("ip为空");
        return false;
    }
    if(_port == 0) {
        LOG("port==0(unsigned short不可能<0)");
        return false;
    }
    if(_dbname.empty()) {
        LOG("dbname为空");
        return false;
    }
    if(_initSize <= 0) {
        _initSize = 5;
        LOG("initSize<=0 修正为5");
    }
    if(_maxSize <= 0) {
        _maxSize = 100;
        LOG("maxSize<=0 修正为100");
    }
    if(_maxIdleTime <= 0) {
        _maxIdleTime = 60;
        LOG("maxIdleTime<=0 修正为60");
    }
    if(_connectionTimeout <= 0) {
        _connectionTimeout = 1000;
        LOG("connectionTimeout<=0 修正为1000");
    }
    if(_maxSize < _initSize) {
        _maxSize = _initSize*2;
        LOG("maxSize < initSize 将maxSize修正为initSize*2");
    }
    
    return true;
}

void ConnectionPool::addConnection() {
    Connection* p = new Connection();  // 创建连接对象

    if(p->connect(_ip, _port, _user, _password, _dbname)) {
        if(!_warmupSql.empty() && !p->update(_warmupSql)) {
            delete p;
            LOG("Error: Database connection warmup failed. SQL statement execution dropped: " + _warmupSql);
            return;
        }
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
    _waitingThreads++;

    while (true) {
        // 等待队列非空
        while (_connectionQue.empty()) {
            _cv.notify_all();
            auto t0 = std::chrono::steady_clock::now();  //等待前
            auto result = _cv.wait_for(lock, std::chrono::milliseconds(_connectionTimeout));
            auto t1 = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
            _totalWaitMs.fetch_add(ms);

            if (result == std::cv_status::timeout) {
                if (_connectionQue.empty()) {
                    _totalTimeouts++;
                    _waitingThreads--;
                    LOG("Connection Pool: Get connection timeout, failed!");
                    throw std::runtime_error("Get connection timeout from pool");
                }
            }
        }

        // 取出队头连接并检查存活状态
        Connection* p = _connectionQue.front();

        if (!p->isAlive()) {
            _connectionQue.pop();
            _connectionCnt--;
            delete p;
            continue;
        }

        _totalBorrows++;
        _waitingThreads--;

        std::shared_ptr<Connection> sp(p,
            [this](Connection *pcon) {
                std::unique_lock<std::mutex> lock(_queueMutex);
                if(!pcon->isAlive()) {
                    _connectionCnt--;
                    delete pcon;
                }
                else {
                    pcon->refreshAliveTime();
                    _connectionQue.push(pcon);
                    _cv.notify_all();
                }
            });

        _connectionQue.pop();
        return sp;
    }
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

PoolStats ConnectionPool::getStats() const {
    PoolStats stats;
    stats.totalConnections = _connectionCnt.load();
    stats.waitingThreads = _waitingThreads.load();
    stats.totalBorrows = _totalBorrows.load();
    stats.totalTimeouts = _totalTimeouts.load();

    // 计算平均等待时长
    long long totalWait = _totalWaitMs.load();
    stats.avgWaitMs = stats.totalBorrows > 0 ? (double)totalWait / stats.totalBorrows : 0.0;

    // 队列大小需要加锁读取
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        stats.idleConnections = _connectionQue.size();
    }
    stats.activeConnections = stats.totalConnections - stats.idleConnections;

    return stats;
}