#include "Connection.h"
#include "public.h"

Connection::Connection() {
    // 初始化
    _conn = mysql_init(nullptr);

    if(_conn == nullptr) {
        std::cout << "mysql_init failed!" << std::endl;
    }
}

Connection::~Connection() {
    if(_conn != nullptr) {
        // 释放资源
        mysql_close(_conn);
    }
}

bool Connection::connect(const std::string& ip, 
                const unsigned short& port, 
                const std::string& user,
                const std::string& password, 
                const std::string& dbname) {
    // 尝试连接数据库
    MYSQL *p = mysql_real_connect(_conn, ip.c_str(), user.c_str(), 
                                  password.c_str(), dbname.c_str(), port, nullptr, 0);
    return p != nullptr;
}
bool Connection::update(const std::string& sql) {
    // 执行SQL增删改
    if(mysql_query(_conn, sql.c_str())) {
        LOG("Update failed:" + sql);
        return false;
    }
    return true;
}

MYSQL_RES* Connection::query(const std::string& sql) {
    // 执行SQL查询
    if(mysql_query(_conn, sql.c_str())) {
        LOG("Query failed:" + sql);
        return nullptr;
    }
    return mysql_store_result(_conn);
}

void Connection::refreshAliveTime() {
    _alivetime = std::chrono::steady_clock::now();
}

long long Connection::getAliveTime() const {
    auto res = std::chrono::steady_clock::now() - _alivetime;
    auto millsec = std::chrono::duration_cast<std::chrono::milliseconds>(res);
    return millsec.count();
}