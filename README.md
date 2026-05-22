#ConnectionPool - 高性能C++MySQL数据库连接池

## 📌 项目简介
本项目是基于C++11编写的高性能MySQL数据库连接池。为解决在多线程高并发的环境下，频繁地创建和销毁数据库连接带来的巨大性能开销，本项目通过池化技术，预先创建并管理一组数据库连接，实现连接的高效复用，能够显著提高数据库的访问性能。

## 🚀 核心技术栈
* **语言标准**：C++11 / C++14
* **构建工具**：CMake + Ninja
* **设计模式**：单例模式（管理全局唯一的连接池对象）、生产者-消费者模型
* **多线程同步**：`std::mutex`（互斥锁）、`std::condition_variable`（条件变量）
* **智能指针**：智能指针：使用 `std::shared_ptr + Lambda 自定义删除器`，将数据库连接生命周期与业务作用域绑定，实现连接的自动归还（而非销毁），完整贯彻 RAII 资源管理思想。

## 🗺️ 架构设计与多线程交互模型
为了更好地理解连接池内部多线程协作、队列交互以及条件变量的通知机制，系统的核心流转架构如下图所示：
<img width="1068" height="345" alt="image" src="https://github.com/user-attachments/assets/bacb7ae2-cc70-4245-9f0a-a7587682e633" />

## 🔄 核心业务流程
1. **连接初始化**：启动时，连接池根据 `mysql.ini` 配置文件，预先创建 `initSize` 个数据库连接并放入队列中。
2. **生产者线程（连接自动扩容）**：当队列为空时，且当前连接总数未达到 `maxSize` 时，生产者线程会自动创建新连接。
3. **消费者（业务线程获取连接）**：业务代码调用接口获取连接，若队列为空，则阻塞等待 `connectionTimeout` 时间。若等待超时且maxSize已满（连接全部被业务占用未归还必须），getConnection()返回nullptr，业务层判空处理，否则会直接段错误。如果MySQL服务器宕机，生产者线程创建连接失败，应有重试机制或同期日志，否则池子将被安静无声息地运动。
4. **回收与释放**：
   * **自动归还**：业务用完连接后，智能指针析构时触发自定义删除器，自动将连接放回队列。
   * **超时销毁**：独立线程定期扫描，若连接闲置时间超过 `maxIdleTime` 且池内连接充裕，则释放多余连接。

## 🛠️ 环境运行指南
运行本项目需要本地配置好 MySQL 开发库，并在编译生成的 `build` 目录下放置：

1. `libmysql.dll`（Windows 运行动态库）
2. `mysql.ini`（配置好你本地的数据库账号、密码和具体的 dbname）

### 📊 mysql.ini 配置项详细对照表

| 配置项 | 类型 | 示例值 | 说明 |
| :--- | :--- | :--- | :--- |
| **ip** | string | `127.0.0.1` | MySQL 服务器地址 |
| **port** | int | `3306` | 端口号 |
| **username** | string | `root` | 数据库用户名 |
| **password** | string | `123456` | 密码 |
| **dbname** | string | `chat` | 目标数据库名 |
| **initSize** | int | `5` | 启动时预创建的连接数 |
| **maxSize** | int | `30` | 连接池上限 |
| **maxIdleTime** | int | `60` | 连接最大休眠秒数 |
| **connectionTimeout** | int | `100` | 业务获取连接的最大等待毫秒数 |

## 💖 阶段性心路历程与技术复盘

### 🛠️ 关于代码实现与核心思考

#### 1. 核心类对比：Connection 与 ConnectionPool 的构造/拷贝机制剖析
在项目设计中，`Connection`（单个连接）和 `ConnectionPool`（全局连接池）都严格禁用了拷贝语义，但它们的**核心设计动机**以及**构造函数的可见性（Public vs Private）**有着本质的区别。

* **核心矛盾**：如果对象允许被随意拷贝，`Connection` 会导致底层物理 Socket 句柄冲突和双重释放崩溃；而 `ConnectionPool` 则会导致全局资源调度失控、多个池子野蛮扩容冲垮数据库。
* **设计精髓**：一个是“独占资源型”对象，一个是“全局管控型”单例。通过不同的访问权限控制，实现了兼顾高性能与极致安全的资源隔离。

---

<details>
<summary>🔍 <b>点击展开：查看 Connection 与 ConnectionPool 的构造/拷贝全方位硬核对比</b></summary>

##### 📊 构造与拷贝机制横向对比表

| 对比维度 | Connection (单个数据库连接) | ConnectionPool (全局连接池) |
| :--- | :--- | :--- |
| **构造函数权限** | **`public` (公有)** 🟢 | **`private` (私有)** 🔴 |
| **为什么这样设计权限** | **允许业务或池子自由创建新连接**。<br>当连接池需要扩容，或者业务需要独立连接时，必须能直接 `new Connection()`。 | **强制实施单例模式，独占控制权**。<br>严禁外部私自 `new` 实例，只能通过 `getInstance()` 访问全局唯一的老大。 |
| **拷贝构造 / 拷贝赋值** | `Connection(const Connection&) = delete;`<br>`operator=(const Connection&) = delete;` | `ConnectionPool(const ConnectionPool&) = delete;`<br>`operator=(const Connection&) = delete;` |
| **禁用拷贝的本质目的** | **防止底层物理句柄（`MYSQL*`）冲突**。<br>确保一个内存对象严格对应一个真实的 TCP 通道，防止多线程抢占 Socket 数据错乱，并杜绝 **双重释放 (Double Free)** 崩溃。 | **防止全局资源管控失控**。<br>防止外部由于拷贝复制出多个“小池子”。如果各池子各自扩容，最大连接数（`maxSize`）将形同虚设，瞬间冲垮 MySQL。 |
| **移动语义 (C++11)** | **允许移动** (`= default` 或自定义)<br>允许连接的所有权在队列（`std::queue`）或线程间安全转移。 | **禁止移动** (`= delete`)<br>单例对象在全局内存中位置绝对固定，绝不允许所有权发生任何转移。 |

##### 💻 核心代码切片与架构对齐

我们将两个类的核心声明放在一起，能更直观地看出这种“和而不同”的架构设计：

```cpp
// ==================== 1. Connection.h ====================
class Connection {
public:
    // 🟢 构造函数必须公开：允许连接池内部或守护线程随时根据需要创建物理连接
    Connection() {
        _priv_conn = mysql_init(nullptr);
    }
    
    // ❌ 禁用拷贝：一个 Connection 独占一个底层的物理 TCP 管道
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    //  允许移动：连接可以被推入标准库容器（如队列）进行所有权转移
    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

private:
    MYSQL* _priv_conn; // 唯一的底层 MySQL 连接句柄
};

// ==================== 2. ConnectionPool.h ====================
class ConnectionPool {
public:
    // 🟢 外部访问池子的唯一合法入口
    static ConnectionPool* getInstance() {
        static ConnectionPool pool; // Meyers 单例，C++11 编译器保证线程安全
        return &pool;
    }

    // ❌ 彻底锁死：单例既不能被拷贝，也不能被移动，必须做到全局绝对唯一
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    ConnectionPool(ConnectionPool&&) = delete;
    ConnectionPool& operator=(ConnectionPool&&) = delete;

private:
    // 🔴 构造函数严格私有化：剥夺外部 new 对象的权利
    ConnectionPool() {
        loadConfigFile();       // 加载 mysql.ini
        initConnectionQueue();  // 初始化连接队列
    }
};
```
---
</details>

#### 2. 刷新时间戳的设计：为什么不用 time()，而是用 steady_clock？
在记录连接空闲起点（用于超时销毁）以及阻塞等待（`wait_for`）时，项目严格使用了 C++11 的 `std::chrono::steady_clock`，而不是传统的 `time()` 或 `std::chrono::system_clock`。

* **核心矛盾**：传统的 `time()` 依赖于系统墙上时钟（Wall Clock），是允许被用户修改、被 NTP（网络时间协议）服务自动同步校准的。这在服务器生命周期内可能导致时间“倒流”或“跳跃”。
* **设计保障**：`steady_clock` 是一个硬件级别的**单调递增时钟**，它的时间戳就像秒表一样只会稳定往前走，绝不可能后退。
---

<details>
<summary>🔍 <b>点击展开：查看关于 time() 隐患与 steady_clock 优势的深度对比</b></summary>

##### ⚠️ 致命场景模拟：为什么系统时钟会引发死锁或崩溃？
假设我们设置连接的最大空闲时间为 `maxIdleTime = 60秒`：
1. 一个连接在 `12:00:00` 变为空闲，连接池使用 `time()` 记录了它的开始闲置时间。
2. 过了 5 秒（真实时间 `12:00:05`），服务器的 NTP 服务突然发现时间走快了，自动将服务器系统时间向后校准了 10 分钟（时间变回 `11:50:05`）。
3. 此时连接池线程去扫描该连接，计算空闲时间：`当前时间(11:50:05) - 记录时间(12:00:00) = -595秒`。
4. **灾难发生**：连接池误以为这个连接永远不会超时，从而导致大量本该被销毁的闲置连接长期霸占内存和数据库通道，失去“动态收缩”的能力。如果是等待队列，甚至可能导致线程陷入永久死锁。

##### 📊 C++11 时间时钟深度对照表

| 时钟类型 | 对应 C++11 结构 | 是否单调递增 | 核心特点与受控因素 | 数据库连接池应用场景 |
| :--- | :--- | :--- | :--- | :--- |
| **系统真实时钟** | `std::chrono::system_clock`<br>(对应 C/C++ `time()`) | **否** ❌ | 反映真实的当前人类时间。**可被手动修改**，可被 NTP 同步。会出现时间倒流或突变。 | ❌ **禁止用于中间件定时器**。<br>（仅适用于记录日志打印时的当前绝对时间） |
| **单调递增时钟** | `std::chrono::steady_clock` | **是** | 物理世界每过去一秒，时钟绝对稳定加一。**不可被修改**，不受对时影响。 |  **完美契合**。<br>用于计算 `maxIdleTime` 扫描和条件变量的 `wait_for` 超时。 |

##### 💻 核心代码切片展示
在连接池内部，我们使用类似以下方式来实现不惧时间篡改的高健壮性定时检测：

```cpp
// 1. 放入队列时记录绝对放回的“物理时间点”
_lastTime = std::chrono::steady_clock::now();

// 2. 守护线程扫描时计算时间差（绝对稳定安全）
auto now = std::chrono::steady_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - conn->_lastTime);
if (duration.count() >= maxIdleTime) {
    // 安全地销毁超出闲置阈值的多余连接...
}
```
---
</details>

#### 3. 为什么连接池必须用单例模式？深度理解 Meyers 单例生命周期
作为一个全局的资源调度中心，整个应用程序在运行期间**有且仅能有一个** `ConnectionPool` 实例。

* **核心矛盾**：如果允许任意创建连接池实例，不同的业务线程就会各自拥有一套独立的连接池。这不仅会导致内存和 TCP 连接资源被成倍浪费，更致命的是，多个池子之间无法集中管控最大连接数（`maxSize`），极易瞬间**将后端的 MySQL 数据库直接冲垮**。
* **设计精髓**：通过控制构造函数权限、全盘禁用拷贝语义，并引入 **C++11 Meyers 单例模式**，将连接池的生命周期安全地移交给编译器托管，实现全局资源的绝对唯一与集中式配额管理。

---

<details>
<summary>🔍 <b>点击展开：查看单例模式抉择、现代 C++ 线程安全与局部 static 底层生命周期剖析</b></summary>

##### ⚠️ 致命场景模拟：不用单例模式会发生什么？
假设配置的 MySQL 最大并发连接数为 `100`。由于没有采用单例模式：
1. 线程 A、线程 B、线程 C 分别私自 `new` 了一个 `ConnectionPool` 对象，每个池子都以为自己最大能开 `50` 个连接。
2. 随着业务并发量上升，三个池子各自疯狂扩容，最终尝试向 MySQL 建立 `3 * 50 = 150` 个连接。
3. **灾难发生**：超过了 MySQL 自身的承载上限，数据库直接抛出 `Too many connections` 错误并拒绝后续所有请求，导致整个线上系统全面瘫痪。

##### 📊 懒汉式 (Lazy) vs 饿汉式 (Eager) 抉择对照表

| 模式类型 | 实例化时机 | 优点 | 缺点 | 数据库连接池适用性评级 |
| :--- | :--- | :--- | :--- | :--- |
| **饿汉式** | 程序一启动（`main` 函数执行前）就立即初始化。 | 天生线程安全，没有多线程并发抢夺创建的问题。 | 浪费启动时间和内存。如果连接池配置（如 `mysql.ini`）读取失败，程序会直接死在启动阶段。 | ⚠️ **不推荐**。<br>（中间件通常依赖外部配置，应延迟加载） |
| **经典懒汉式**<br>(双检锁/DCL) | 第一次调用 `getInstance()` 时才初始化。 | 延迟加载（Lazy Loading），不占用宝贵的启动资源。 | 需要手动配合 `std::mutex` 和双重 `if` 判断，代码冗长，且容易因指令重排产生 Bug。 | ❌ **不推荐**。<br>（代码过于老旧且维护成本高） |
| **Meyers 单例**<br>(C++11 局部静态) | 第一次调用 `getInstance()` 时才初始化。 | 拥有懒汉式的所有优点，且代码极致精简。**利用 C++11 编译器底层特性，天然保证线程安全**。 | 依赖编译器对 C++11 标准的完整支持。 |  **完美契合（首选）**。<br>现代 C++ 工业界标准的单例写法。 |

##### 💻 核心代码切片展示：基于 C++11 的 Meyers 单例机制

在项目实现中，我们采用了最优雅的 **Meyers 单例（局部静态变量寿命特性）**。不仅天生具备懒汉式的延迟加载优势，还规避了繁琐的加锁操作：

```cpp
class ConnectionPool {
public:
    // 🟢 外部获取全局唯一实例的唯一合法接口
    static ConnectionPool* getInstance() {
        // C++11 标准严格规定：如果多个线程同时试图初始化同一个局部静态变量，
        // 编译器会在底层自动加锁（使用 guard variable 机制），保证初始化过程百分之百线程安全！
        static ConnectionPool pool; 
        return &pool;
    }

    // ❌ 既然是单例，必须死死封锁外部拷贝与移动的任何可能性
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    ConnectionPool(ConnectionPool&&) = delete;
    ConnectionPool& operator=(ConnectionPool&&) = delete;

private:
    // 🔴 构造函数私有化：剥夺外部自由 new 实例的权利
    ConnectionPool() {
        if (!loadConfigFile()) {
            throw std::runtime_error("Failed to load mysql.ini config!");
        }
        initConnectionQueue();
    }
    
    bool loadConfigFile();
    void initConnectionQueue();
};
```
---
</details>

#### 4. 智能指针的妙用：shared_ptr 与自定义删除器的完美结合（项目最大亮点）
在业务层获取连接时，项目没有返回原始的裸指针（`Connection*`），而是返回了一个经过特殊定制的 `std::shared_ptr<Connection>`。

* **核心矛盾**：传统的 RAII 智能指针在其引用计数归零（出作用域）时，默认行为是直接调用 `delete ptr` 销毁对象、关闭 TCP 通道。如果每次业务用完连接都直接销毁并重新创建，连接池将彻底失去“减少 TCP 握手开销”的核心价值。
* **设计精髓**：利用 **自定义删除器 (Custom Deleter)** 拦截并重写 `shared_ptr` 的析构行为。让智能指针在“毁灭”时，自动执行“将连接放回队列”的回收逻辑，实现对业务层完全透明的资源复用。

---

<details>
<summary>🔍 <b>点击展开：查看普通 shared_ptr 与自定义删除器的硬核对比及底层机制</b></summary>

##### 📊 普通 shared_ptr vs 定制化连接池 shared_ptr 深度对照

| 对比维度 | 普通 std::shared_ptr | 连接池定制版 std::shared_ptr |
| :--- | :--- | :--- |
| **生命周期终点行为** | **真正销毁对象** ❌<br>直接触发 `delete Connection;`。 | **回收并复用资源** 🔄<br>执行自定义 Lambda，将连接重新推入连接队列。 |
| **底层物理连接状态** | **断开** 🛑<br>调用 `mysql_close()`，释放操作系统 Socket 资源。 | **保持存活 (Keep-Alive)** 🟢<br>TCP 管道保持长连接状态，随时等待下一个业务请求。 |
| **业务层使用感知** | 必须小心翼翼地手动释放，或者任由其销毁。 | **完全无感**。正常像普通指针一样使用，出了作用域自动归还，绝无资源泄露风险。 |
| **性能开销** | **高**。每次使用都要经历频繁的 TCP 三次握手与四次挥手。 | **极低**。一次创建，千次复用，消除了 99% 的网络建链延迟。 |

##### 💻 核心代码切片展示：自定义删除器的优雅实现
在 `ConnectionPool.cpp` 中，业务层调用 `getConnection()` 时，我们通过特殊的构造方式为 `shared_ptr` 注入了回收灵魂：

```cpp
std::shared_ptr<Connection> ConnectionPool::getConnection() {
    // 1. 加锁从空闲队列中取出一个连接
    std::unique_lock<std::mutex> lock(_queueMutex);
    while (_connectionQueue.empty()) {
        // 如果池子空了，阻塞等待 connectionTimeout 毫秒
        if (std::cv_status::timeout == _cv.wait_for(lock, std::chrono::milliseconds(_connectionTimeout))) {
            if (_connectionQueue.empty()) {
                LOG("获取连接超时，池中无可用连接！");
                return nullptr;
            }
        }
    }

    // 2. 核心魔法：从队列中弹出裸指针，但包装成带自定义删除器的 shared_ptr
    /*
     ⚠️ 注意：这里不能使用 std::make_shared
     ⚠️ 为何不能用 std::make_shared<Connection>(rawPtr, deleter)？
     make_shared 会将控制块与对象内存合并分配，它的第一个参数是构造参数，
     而非一个已有的裸指针。此处我们的连接已经存在于队列中，
     必须用 shared_ptr 的"接管已有裸指针"构造形式，才能注入自定义删除器。
    */
    std::shared_ptr<Connection> sp(_connectionQueue.front(), 
        [this](Connection* pcon) {
            // 这就是自定义删除器！当 sp 的引用计数归零时，不会调用 delete pcon，
            // 而是触发这个 Lambda 表达式，自动把连接安全的放回队列尾部！
            std::unique_lock<std::mutex> lock(_queueMutex);
            pcon->refreshLastTime(); // 归还前刷新空闲时间戳
            _connectionQueue.push(pcon);
            _cv.notify_all(); // 通知生产线程或正在等待连接的其他业务线程
        }
    );

    _connectionQueue.pop();
    return sp; // 返回给业务层
}
```
---
</details>

---

## 🏁 总结与收获

通过对本项目（高性能 MySQL 连接池）的第一阶段开发与深度复盘，我不仅攻克了多线程高并发下的诸多“细节暗坑”，更对 C++ 的核心哲学有了全新的认识：

1. **将错误拦截在编译期**：通过显式使用 `= delete` 禁用拷贝与移动，利用 C++ 的类型系统和语法特性做强约束，远比在运行时写各种 `try-catch` 或防御代码更加优雅和安全。
2. **彻底贯彻 RAII 思想**：资源的生死与对象的生命周期强绑定。在连接池中，通过智能指针自定义删除器的巧妙“变奏”，实现了对业务层完全透明的资源复用。这种“零负担”的接口设计，正是现代化 C++ 框架追求的极致。
3. **高并发下的健壮性考量**：不轻易信任系统时间（使用 `steady_clock` 防止 NTP 干扰）、利用编译器底层特性确保单例线程安全（Meyers 单例）。中间件的编写不仅关乎算法与数据结构，更关乎在复杂多变的操作系统环境下的生存能力。

### ⚠️ 已知局限与改进空间
我下一阶段优化突破的动力所在：

* **缺少活性探活（心跳机制）**：当前未完善底层的 `Ping` 探活机制。如果连接在队列中长时间闲置，可能会被 MySQL 服务端由于 `wait_timeout` 自动断开，导致连接池吐出无法使用的“死连接”。未来计划引入定期的异步心跳包侦测。
* **被动触发的扩容算法**：目前的生产者线程是在队列彻底变空、且业务线程阻塞时才被动触发创建。为了应对突发洪峰，后续可考虑引入基于**动态水位线（如池内空闲连接低于 20%）的预测性主动预热扩容**。
* **单实例路由局限**：目前连接池仅支持配置单台 MySQL 实例，尚不支持读写分离（Master/Slave）或分布式多实例的路由分配，未来架构可往 Proxy 中间件或多实例分发方向进一步演进。
---
感谢你看到这里！如果你对这个项目的底层设计或 C++ 并发编程感兴趣，欢迎提交 Issue 交流，或者为这个仓库点一个宝贵的 **Star ⭐**！
