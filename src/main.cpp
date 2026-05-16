#include "ConnectionPool.h"
#include <thread>
#include <iostream>
#include <vector>

void insertTask(int begin, int end)
{
    ConnectionPool* pool = ConnectionPool::getConnectionPool();

    for (int i = begin; i <= end; ++i)
    {
        std::shared_ptr<Connection> sp = pool->getConnection();

        char sql[1024] = {0};

        sprintf(sql,
                "insert into user(name, age) values('zhangsan%d', %d)",
                i, i);

        if (sp->update(sql))
        {
            std::cout << "thread "
                 << std::this_thread::get_id()
                 << " insert success: "
                 << i << std::endl;
        }
        else
        {
            std::cout << "insert failed: "
                 << i << std::endl;
        }
    }
}

int main()
{
    std::vector<std::thread> threads;

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

    std::cout << "all insert finished!" << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(20));

    return 0;
}