# ConnectionPool - 高性能C++MySQL数据库连接池

## 📌 项目简介

本项目是基于C++11编写的高性能MySQL数据库连接池，通过池化技术预先创建并管理一组数据库连接，实现高效复用，解决多线程高并发下频繁创建和销毁连接带来的性能开销。

## 🚀 核心技术栈

- **语言标准**：C++11 / C++14
- **构建工具**：CMake + Ninja
- **设计模式**：单例模式（全局唯一连接池）、生产者-消费者模型
- **多线程同步**：`std::mutex`、`std::condition_variable`
- **智能指针**：使用 `std::shared_ptr + Lambda 自定义删除器`，将连接生命周期与作用域绑定，实现自动归还，贯彻 RAII 资源管理思想

## 🗺️ 架构设计与多线程交互模型

核心流转架构图展示了连接池内部多线程协作、队列交互以及条件变量的通知机制。

## 🔄 核心业务流程

1. **连接初始化**：启动时根据 `mysql.ini` 配置，预创建 `initSize` 个连接放入队列
2. **生产者线程（自动扩容）**：队列为空且连接总数未达 `maxSize` 时，自动创建新连接
3. **消费者（业务获取连接）**：调用接口获取连接，队列空则阻塞等待 `connectionTimeout` 毫秒。超时且队列仍空时 `getConnection()` 抛出异常，业务层需捕获处理
4. **回收与释放**：
   - **自动归还**：智能指针析构触发自定义删除器，自动将连接放回队列
   - **超时销毁**：独立线程定期扫描，闲置超过 `maxIdleTime` 的多余连接被释放

## 🛠️ 环境运行指南

需要本地配置 MySQL 开发库，并在 `build` 目录下放置 `libmysql.dll` 和 `mysql.ini`。

### 📊 mysql.ini 配置项详细对照表

| 配置项 | 类型 | 示例值 | 说明 |
|--------|------|--------|------|
| **ip** | string | `127.0.0.1` | MySQL 服务器地址 |
| **port** | int | `3306` | 端口号 |
| **username** | string | `root` | 数据库用户名 |
| **password** | string | `123456` | 密码 |
| **dbname** | string | `chat` | 目标数据库名 |
| **initSize** | int | `5` | 启动时预创建的连接数 |
| **maxSize** | int | `30` | 连接池上限 |
| **maxIdleTime** | int | `60` | 连接最大休眠秒数 |
| **connectionTimeout** | int | `100` | 获取连接最大等待毫秒数 |
| **warmupSql** | string | `SET NAMES utf8mb4` | 可选，连接创建后执行的预热 SQL |

## 💖 阶段性心路历程与技术复盘

### 🛠️ 关于代码实现与核心思考

#### 1. 核心类对比：Connection 与 ConnectionPool 的构造/拷贝机制剖析

`Connection` 和 `ConnectionPool` 都严格禁用了拷贝语义，但设计动机与构造函数可见性有本质区别。

- **核心矛盾**：若允许随意拷贝，`Connection` 会导致底层物理 Socket 句柄冲突和双重释放崩溃；`ConnectionPool` 则导致全局调度失控、多个池子野蛮扩容冲垮数据库
- **设计精髓**：一个是"独占资源型"对象，一个是"全局管控型"单例

**构造与拷贝机制横向对比：**

| 对比维度 | Connection | ConnectionPool |
|----------|-----------|----------------|
| 构造函数权限 | **public** 🟢（允许业务或池子自由创建新连接） | **private** 🔴（强制单例模式，独占控制权） |
| 拷贝构造/赋值 | `= delete` | `= delete` |
| 禁用拷贝目的 | 防止底层 `MYSQL*` 句柄冲突，杜绝双重释放 | 防止全局资源管控失控，maxSize 形同虚设 |

**关键代码对比：**

```cpp
// Connection.h
class Connection {
public:
    Connection() { _conn = mysql_init(nullptr); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
private:
    MYSQL* _conn;
};

// ConnectionPool.h
class ConnectionPool {
public:
    static ConnectionPool* getConnectionPool() {
        static ConnectionPool pool; // Meyers 单例，C++11 编译器保证线程安全
        return &pool;
    }
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
private:
    ConnectionPool() { loadConfigFile(); /* 创建初始连接... */ }
};
```

#### 2. 刷新时间戳设计：为什么用 `steady_clock` 而非 `time()`

- **核心矛盾**：`time()` 依赖系统墙上时钟，可被用户修改或 NTP 校准导致时间倒流/跳跃
- **设计保障**：`steady_clock` 是硬件级别的单调递增时钟，只增不退

**时钟类型对照：**

| 时钟类型 | C++11 结构 | 单调递增 | 应用场景 |
|---------|-----------|---------|---------|
| 系统真实时钟 | `system_clock`（对应 `time()`） | 否 ❌ | 仅适用于记录日志的绝对时间 |
| 单调递增时钟 | `steady_clock` | 是 ✅ | 完美用于 `maxIdleTime` 扫描和 `wait_for` 超时 |

**关键代码：**

```cpp
// 放入队列时记录归还的物理时间点
_alivetime = std::chrono::steady_clock::now();

// 扫描线程计算时间差
long long getAliveTime() const {
    auto res = std::chrono::steady_clock::now() - _alivetime;
    auto millsec = std::chrono::duration_cast<std::chrono::milliseconds>(res);
    return millsec.count();
}
```

#### 3. 为什么必须用单例模式？深度理解 Meyers 单例生命周期

- **核心矛盾**：多个连接池实例导致资源成倍浪费，maxSize 被绕过，瞬间冲垮 MySQL
- **设计精髓**：通过私有构造函数、禁用拷贝、引入 C++11 Meyers 单例，将生命周期交由编译器托管

**懒汉式 vs 饿汉式对比：**

| 模式 | 实例化时机 | 优点 | 缺点 | 适用性 |
|------|-----------|------|------|--------|
| 饿汉式 | main 前初始化 | 天生线程安全 | 浪费启动时间，配置失败则程序死在启动阶段 | ⚠️ 不推荐 |
| 经典懒汉式（DCL） | 首次调用时 | 延迟加载 | 代码冗长，指令重排可能产生 Bug | ❌ 不推荐 |
| **Meyers 单例** | 首次调用时 | 懒汉式优点 + 代码极致精简，C++11 编译器天然线程安全 | 依赖 C++11 支持 | ✅ 首选 |

**关键代码：**

```cpp
static ConnectionPool* getConnectionPool() {
    static ConnectionPool pool; // guard variable 机制保证线程安全
    return &pool;
}
```

#### 4. 智能指针的妙用：shared_ptr 与自定义删除器（项目最大亮点）

- **核心矛盾**：普通 `shared_ptr` 析构时直接 `delete` 对象断开连接，连接池失去复用价值
- **设计精髓**：利用自定义删除器拦截析构行为，自动执行"归还队列"而非销毁

**对比表：**

| 维度 | 普通 shared_ptr | 连接池定制版 shared_ptr |
|------|----------------|----------------------|
| 生命周期终点 | 真正销毁对象 ❌ | 回收并复用 🔄 |
| 物理连接状态 | 断开 | 保持 Keep-Alive 🟢 |
| 业务层感知 | 需手动释放 | 完全无感，出作用域自动归还 |
| 性能开销 | 高（频繁 TCP 握手挥手） | 极低（一次创建千次复用） |

**关键代码：**

```cpp
std::shared_ptr<Connection> ConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(_queueMutex);
    _waitingThreads++;

    while (true) {
        while (_connectionQue.empty()) {
            _cv.notify_all();
            auto t0 = std::chrono::steady_clock::now();
            auto result = _cv.wait_for(lock, std::chrono::milliseconds(_connectionTimeout));
            auto t1 = std::chrono::steady_clock::now();
            _totalWaitMs.fetch_add(
                std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

            if (result == std::cv_status::timeout) {
                if (_connectionQue.empty()) {
                    _totalTimeouts++;
                    _waitingThreads--;
                    throw std::runtime_error("Get connection timeout from pool");
                }
            }
        }

        Connection* p = _connectionQue.front();

        // 借用时健康检查
        if (!p->isAlive()) {
            _connectionQue.pop();
            _connectionCnt--;
            delete p;
            continue;
        }

        _totalBorrows++;
        _waitingThreads--;

        std::shared_ptr<Connection> sp(p,
            [this](Connection* pcon) {
                std::unique_lock<std::mutex> lock(_queueMutex);
                // 归还时健康检查
                if (!pcon->isAlive()) {
                    _connectionCnt--;
                    delete pcon;
                } else {
                    pcon->refreshAliveTime();
                    _connectionQue.push(pcon);
                    _cv.notify_all();
                }
            }
        );
        _connectionQue.pop();
        return sp;
    }
}
```

## 📦 功能增强记录

### 1. 连接活性探活（心跳机制）

针对原 "已知局限" 中缺少心跳机制的短板，实现了**双重检测**架构：

- **借用时检查**：`getConnection()` 取出连接时执行 `mysql_ping()` 验证存活状态，死连接直接丢弃并循环重取下一个。如果队列内所有连接均死亡，循环清空后自动唤醒生产者线程创建新连接
- **归还时检查**：自定义删除器内再次执行 `mysql_ping()`，若连接在借用期间断开（MySQL 重启、网络中断等），直接 `delete` 销毁而不放回队列，杜绝死连接污染池子

**设计要点**：`mysql_ping()` 在活连接上近乎零开销（一次轻量级 ping 包往返），死连接快速失败返回非零，不会阻塞持有锁的线程。

### 2. 连接预热（Connection Warmup）

支持 `warmupSql` 配置项，连接创建后自动执行自定义初始化 SQL：

```
mysql_init → mysql_real_connect → warmup SQL → 刷新空闲时间戳 → 放入队列
```

**应用场景**：在连接入池前统一执行 `SET NAMES utf8mb4`、设置时区、配置 `autocommit` 等会话级别的初始化操作，避免业务层每次借用后重复执行。预热失败则连接直接销毁，不进入队列。

### 3. 连接池状态监控

新增 `PoolStats` 结构体与 `getStats()` 接口，提供运行时指标：

| 指标 | 说明 |
|------|------|
| `totalConnections` | 当前存在的连接总数 |
| `idleConnections` | 队列中空闲等待的连接数 |
| `activeConnections` | 借出使用中的连接数 (total - idle) |
| `waitingThreads` | 当前在 `getConnection()` 中阻塞等待的线程数 |
| `totalBorrows` | 累计成功借用次数 |
| `totalTimeouts` | 累计超时失败次数 |
| `avgWaitMs` | 平均等待时长（毫秒） |

实现上采用 `std::atomic_long` 进行无锁计数，`getConnection()` 全链路埋点采集等待时长与借用/超时事件，`getStats()` 对 queue 操作短暂加锁保证一致性。

### 4. 配置加载加固

原配置加载存在的三个问题全部修复：

| 问题 | 解决方案 |
|------|---------|
| 配置项缺失时成员为未初始化值 | **类内默认值**：全部 9 个配置项在头文件声明处直接初始化 |
| `std::stoi` 遇到非数字输入直接崩溃 | **parseInt 安全转换**：`try-catch` 包装 + `size_t& pos` 校验全字符消费 |
| 非法值被静默接受 | **两级校验**：数据库连接参数（ip/port/dbname）硬错误直接失败；性能参数（initSize/maxSize/maxIdleTime/connectionTimeout）软纠正自动修正为默认值 |

## 🏁 总结与收获

1. **将错误拦截在编译期**：通过 `= delete` 禁用拷贝，比运行时防御更加优雅安全
2. **彻底贯彻 RAII 思想**：通过智能指针自定义删除器实现对业务层透明的资源复用
3. **高并发下的健壮性考量**：使用 `steady_clock` 防止 NTP 干扰，利用编译器特性确保单例线程安全
4. **双重健康检查**：借用 + 归还两处检测，确保池内连接始终存活可用
5. **监控可观测**：全面埋点暴露运行时指标，为容量评估和问题排查提供数据支撑

### ⚠️ 已知局限与改进空间

- **被动触发的扩容算法**：生产者线程仅在队列变空时才被动创建连接，后续可考虑基于动态水位线的预测性主动预热扩容
- **单实例路由局限**：仅支持单台 MySQL 实例，尚不支持读写分离或分布式多实例路由，未来可往 Proxy 中间件方向演进
- **健康检查时机**：当前在借用/归还时检测，空闲期间不做主动保活。若连接在队列中长期闲置后被 MySQL 服务端 `wait_timeout` 断开，可增加后台异步心跳保活线程降低借出时探测开销
