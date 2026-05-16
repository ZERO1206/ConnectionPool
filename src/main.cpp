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
        shared_ptr<Connection> sp = pool->getConnection();

        char sql[1024] = {0};

        sprintf(sql,
                "insert into user(name, age) values('zhangsan%d', %d)",
                i, i);

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

    for (int i = 0; i < 4; ++i)
    {
        threads.emplace_back(
            insertTask,
            i * 50 + 1,
            (i + 1) * 50);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    cout << "all insert finished!" << endl;

    this_thread::sleep_for(chrono::seconds(20));

    return 0;
}