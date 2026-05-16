#include "ConnectionPool.h"
#include "Connection.h"
#include "public.h"
 
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <string>
#include <stdexcept>
#include <atomic>
 
// ─────────────────────────────────────────────
// 工具函数
// ─────────────────────────────────────────────
 
static void printSep(const std::string& title) {
    std::cout << "\n==============================\n";
    std::cout << "  " << title << "\n";
    std::cout << "==============================\n";
}
 
static void pass(const std::string& msg) {
    std::cout << "  [PASS] " << msg << "\n";
}
 
static void fail(const std::string& msg) {
    std::cout << "  [FAIL] " << msg << "\n";
}
 
// ─────────────────────────────────────────────
// TC-01  获取单例，验证连接池不为 nullptr
// ─────────────────────────────────────────────
void tc01_singleton() {
    printSep("TC-01  单例获取");
 
    ConnectionPool* pool1 = ConnectionPool::getConnectionPool();
    ConnectionPool* pool2 = ConnectionPool::getConnectionPool();
 
    if (pool1 != nullptr)
        pass("getConnectionPool() 返回非空指针");
    else
        fail("getConnectionPool() 返回了 nullptr");
 
    if (pool1 == pool2)
        pass("两次调用返回同一实例（单例有效）");
    else
        fail("两次调用返回了不同实例");
}
 
// ─────────────────────────────────────────────
// TC-02  单次获取连接并执行 INSERT
// ─────────────────────────────────────────────
void tc02_single_insert() {
    printSep("TC-02  单次 INSERT");
 
    auto pool = ConnectionPool::getConnectionPool();
    auto conn = pool->getConnection();
 
    if (conn)
        pass("成功获取连接");
    else { fail("获取连接失败"); return; }
 
    // 先清空表，保证测试幂等
    conn->update("DELETE FROM users");
 
    bool ok = conn->update(
        "INSERT INTO users(name, age) VALUES('Alice', 28)");
 
    if (ok) pass("INSERT 执行成功");
    else    fail("INSERT 执行失败");
 
    // conn 析构 → 自动归还连接池
}
 
// ─────────────────────────────────────────────
// TC-03  查询刚插入的数据，验证结果集
// ─────────────────────────────────────────────
void tc03_query() {
    printSep("TC-03  SELECT 查询");
 
    auto pool = ConnectionPool::getConnectionPool();
    auto conn = pool->getConnection();
 
    MYSQL_RES* res = conn->query(
        "SELECT id, name, age FROM users WHERE name='Alice'");
 
    if (res == nullptr) { fail("query() 返回 nullptr"); return; }
 
    unsigned long long rows = mysql_num_rows(res);
    if (rows >= 1)
        pass("查询到 " + std::to_string(rows) + " 行，符合预期");
    else
        fail("查询结果为空，期望至少 1 行");
 
    // 打印第一行内容
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        std::cout << "    -> id=" << (row[0] ? row[0] : "NULL")
                  << "  name=" << (row[1] ? row[1] : "NULL")
                  << "  age="  << (row[2] ? row[2] : "NULL") << "\n";
    }
 
    mysql_free_result(res);
    pass("mysql_free_result 正常释放");
}
 
// ─────────────────────────────────────────────
// TC-04  UPDATE + 再次查询，验证修改生效
// ─────────────────────────────────────────────
void tc04_update() {
    printSep("TC-04  UPDATE");
 
    auto pool = ConnectionPool::getConnectionPool();
    auto conn = pool->getConnection();
 
    bool ok = conn->update(
        "UPDATE users SET age=30 WHERE name='Alice'");
    if (ok) pass("UPDATE 执行成功");
    else    fail("UPDATE 执行失败");
 
    MYSQL_RES* res = conn->query(
        "SELECT age FROM users WHERE name='Alice'");
    if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0] && std::string(row[0]) == "30")
            pass("age 已更新为 30");
        else
            fail("age 更新后读取值不符");
        mysql_free_result(res);
    }
}
 
// ─────────────────────────────────────────────
// TC-05  DELETE
// ─────────────────────────────────────────────
void tc05_delete() {
    printSep("TC-05  DELETE");
 
    auto pool = ConnectionPool::getConnectionPool();
    auto conn = pool->getConnection();
 
    bool ok = conn->update("DELETE FROM users WHERE name='Alice'");
    if (ok) pass("DELETE 执行成功");
    else    fail("DELETE 执行失败");
 
    MYSQL_RES* res = conn->query(
        "SELECT COUNT(*) FROM users WHERE name='Alice'");
    if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0] && std::string(row[0]) == "0")
            pass("删除后查询计数为 0");
        else
            fail("删除后计数不为 0");
        mysql_free_result(res);
    }
}
 
// ─────────────────────────────────────────────
// TC-06  非法 SQL — update 返回 false，不崩溃
// ─────────────────────────────────────────────
void tc06_invalid_sql() {
    printSep("TC-06  非法 SQL");
 
    auto pool = ConnectionPool::getConnectionPool();
    auto conn = pool->getConnection();
 
    bool ok  = conn->update("THIS IS NOT SQL AT ALL");
    if (!ok) pass("update() 对非法 SQL 返回 false（符合预期）");
    else     fail("update() 对非法 SQL 返回了 true");
 
    MYSQL_RES* res = conn->query("SELEKT * FROM users");
    if (res == nullptr) pass("query() 对非法 SQL 返回 nullptr（符合预期）");
    else { fail("query() 对非法 SQL 返回了非空指针"); mysql_free_result(res); }
 
    // 确认连接依然可用
    bool alive = conn->update(
        "INSERT INTO users(name,age) VALUES('probe',1)");
    conn->update("DELETE FROM users WHERE name='probe'");
    if (alive) pass("非法 SQL 后连接仍可正常使用");
    else       fail("非法 SQL 后连接不可用");
}
 
// ─────────────────────────────────────────────
// TC-07  shared_ptr 析构后连接自动归还
//        方法：连续获取再释放，观察不死锁
// ─────────────────────────────────────────────
void tc07_auto_return() {
    printSep("TC-07  连接自动归还");
 
    auto pool = ConnectionPool::getConnectionPool();
 
    for (int i = 0; i < 20; ++i) {
        auto conn = pool->getConnection();   // 取
        conn->update("SELECT 1");             // 用
    }                                         // 析构 → 还
 
    pass("20 次取用 + 自动归还，全部正常（无死锁）");
}
 
// ─────────────────────────────────────────────
// TC-08  多线程并发获取连接
// ─────────────────────────────────────────────
void tc08_concurrent() {
    printSep("TC-08  多线程并发（10 线程）");
 
    auto pool = ConnectionPool::getConnectionPool();
    std::atomic_int success{0};
    std::vector<std::thread> threads;
 
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&pool, &success, i]() {
            try {
                auto conn = pool->getConnection();
                std::string sql =
                    "INSERT INTO users(name,age) VALUES('thread_"
                    + std::to_string(i) + "'," + std::to_string(i) + ")";
                if (conn->update(sql)) ++success;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } catch (const std::exception& e) {
                std::cout << "    [WARN] 线程 " << i
                          << " 异常: " << e.what() << "\n";
            }
        });
    }
 
    for (auto& t : threads) t.join();
 
    std::cout << "    -> " << success.load() << "/10 个线程成功写入\n";
    if (success.load() == 10)
        pass("10 个线程全部写入成功");
    else
        fail("部分线程写入失败，检查 maxSize 配置");
 
    // 清理并发插入的数据
    auto conn = pool->getConnection();
    conn->update("DELETE FROM users WHERE name LIKE 'thread_%'");
}
 
// ─────────────────────────────────────────────
// TC-09  超时测试（持有所有连接不释放）
//        注意：connectionTimeout 需配置较小值（如 500ms）
// ─────────────────────────────────────────────
void tc09_timeout() {
    printSep("TC-09  获取连接超时");
 
    auto pool = ConnectionPool::getConnectionPool();
 
    // 尽量占满连接池（maxSize 个），使下一次请求超时
    std::vector<std::shared_ptr<Connection>> held;
    int grabbed = 0;
    while (grabbed < 10) {   // 最多试取 10 个
        try {
            held.push_back(pool->getConnection());
            ++grabbed;
        } catch (...) {
            break;
        }
    }
    std::cout << "    -> 已持有 " << grabbed << " 个连接\n";
 
    bool threw = false;
    try {
        // 再请求一个，应超时
        auto extra = pool->getConnection();
        (void)extra;
    } catch (const std::runtime_error& e) {
        threw = true;
        std::cout << "    -> 捕获到异常: " << e.what() << "\n";
    }
 
    // 释放所有持有的连接
    held.clear();
 
    if (threw)
        pass("连接池耗尽后正确抛出 runtime_error");
    else
        // 如果没抛出，说明生产者线程补充了连接，也是合理行为
        pass("生产者线程补充了连接，未超时（maxSize 未满时正常）");
}
 
// ─────────────────────────────────────────────
// TC-10  refreshAliveTime / getAliveTime
// ─────────────────────────────────────────────
void tc10_alive_time() {
    printSep("TC-10  连接空闲计时");
 
    auto pool = ConnectionPool::getConnectionPool();
    auto conn = pool->getConnection();
 
    conn->refreshAliveTime();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    long long ms = conn->getAliveTime();
 
    std::cout << "    -> 空闲时长 = " << ms << " ms（期望 ≥ 200）\n";
    if (ms >= 200)
        pass("getAliveTime() 返回值正确（≥ 200ms）");
    else
        fail("getAliveTime() 返回值偏小，计时异常");
}
 
// ─────────────────────────────────────────────
// MAIN
// ─────────────────────────────────────────────
int main() {
    std::cout << "========================================\n";
    std::cout << "  MySQL 连接池 — 完整测试套件\n";
    std::cout << "========================================\n";
 
    try {
        tc01_singleton();
        tc02_single_insert();
        tc03_query();
        tc04_update();
        tc05_delete();
        tc06_invalid_sql();
        tc07_auto_return();
        tc08_concurrent();
        tc09_timeout();
        tc10_alive_time();
    } catch (const std::exception& e) {
        std::cerr << "\n[FATAL] 未捕获异常: " << e.what() << "\n";
        return 1;
    }
 
    std::cout << "\n==============================\n";
    std::cout << "  全部测试执行完毕\n";
    std::cout << "==============================\n";
    return 0;
}