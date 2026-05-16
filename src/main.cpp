#include "ConnectionPool.h"
#include <thread>
#include <iostream>
#include <vector>

using namespace std;

void insertTask(int begin, int end)
{
    ConnectionPool* pool = ConnectionPool::getConnectionPool();

    for (int i = begin; i <= end; ++i)
    {
        // 从连接池获取连接
        shared_ptr<Connection> sp = pool->getConnection();

        char sql[1024] = {0};

        sprintf(sql,
                "insert into user(name, age) values('zhangsan%d', %d)",
                i, i);

        // 执行插入
        if (sp->update(sql))
        {
            cout << "thread "
                 << this_thread::get_id()
                 << " insert success: "
                 << i << endl;
        }
        else
        {
            cout << "insert failed: "
                 << i << endl;
        }
    }
}

int main()
{
    vector<thread> threads;

    // 启动4个线程
    for (int i = 0; i < 4; ++i)
    {
        threads.emplace_back(
            insertTask,
            i * 50 + 1,
            (i + 1) * 50);
    }

    // 回收线程
    for (auto& t : threads)
    {
        t.join();
    }

    cout << "all insert finished!" << endl;

    // 等待scanner回收连接
    this_thread::sleep_for(
        chrono::seconds(20));

    return 0;
}