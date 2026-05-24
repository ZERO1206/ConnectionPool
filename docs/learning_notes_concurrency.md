# C++ 连接池核心知识点学习笔记

## 一、MySQL C API

MySQL C API 是 MySQL 官方提供的 C 语言接口，所有操作围绕核心类型 `MYSQL*`（不透明指针，代表一条 TCP 数据库连接）。

本项目只用到了以下 5 个函数：

---

### 1. mysql_init — 初始化句柄

```c
MYSQL *mysql_init(MYSQL *mysql);
```

| 项目 | 说明 |
|------|------|
| **作用** | 分配并初始化一个 `MYSQL` 句柄（尚未建立网络连接，仅分配内存） |
| **参数** | 传 `nullptr` 让其自动 `malloc` 一个新的 `MYSQL` 对象 |
| **返回值** | 成功返回 `MYSQL*`；失败返回 `nullptr`（仅内存不足时，极为罕见） |
| **类比** | `fopen` 拿到 `FILE*` 之前，先要有句柄——这是操作 MySQL 的"身份证" |

---

### 2. mysql_real_connect — 建立 TCP 连接

```c
MYSQL *mysql_real_connect(MYSQL *mysql,
                          const char *host,
                          const char *user,
                          const char *passwd,
                          const char *db,
                          unsigned int port,
                          const char *unix_socket,
                          unsigned long clientflag);
```

| 项目 | 说明 |
|------|------|
| **作用** | 使用 `mysql_init` 初始化好的句柄，建立 TCP 连接到 MySQL 服务端 |
| **参数 1** | `MYSQL*` — 已初始化的连接句柄 |
| **参数 2** | `host` — IP 地址（`std::string::c_str()` 转换为 C 字符串） |
| **参数 3** | `user` — 用户名 |
| **参数 4** | `passwd` — 密码 |
| **参数 5** | `db` — 目标数据库名 |
| **参数 6** | `port` — 端口号 |
| **参数 7** | `unix_socket` — 传 `nullptr` 即可（使用 TCP 而非 Unix Socket） |
| **参数 8** | `clientflag` — 传 `0` 即可（无特殊标志） |
| **返回值** | 成功返回**同一个** `MYSQL*` 指针；失败返回 `nullptr` |
| **关键点** | 返回的指针与参数 1 是同一个指针，所以 `return p != nullptr` 即可判断成功与否 |

---

### 3. mysql_query — 执行 SQL

```c
int mysql_query(MYSQL *mysql, const char *query);
```

| 项目 | 说明 |
|------|------|
| **作用** | 向 MySQL 服务端发送一条 SQL 语句并等待其执行完成 |
| **参数** | `MYSQL*` 连接句柄 + C 风格 SQL 字符串 |
| **返回值** | `0` = 执行成功；非 `0` = 执行出错 |
| **注意** | 对于 `SELECT` 查询，执行成功后结果仍在服务端，需要 **mysql_store_result** 取回 |

---

### 4. mysql_store_result — 取回查询结果

```c
MYSQL_RES *mysql_store_result(MYSQL *mysql);
```

| 项目 | 说明 |
|------|------|
| **作用** | 将 `SELECT` 的结果集**全部**从服务端拉取到客户端内存 |
| **参数** | `MYSQL*` 连接句柄 |
| **返回值** | 成功返回 `MYSQL_RES*`（调用者需手动 `mysql_free_result` 释放）；失败返回 `nullptr` |
| **使用场景** | 仅在 `mysql_query` 执行 `SELECT` 成功后调用 |

---

### 5. mysql_ping — 连接存活性检测

```c
int mysql_ping(MYSQL *mysql);
```

| 项目 | 说明 |
|------|------|
| **作用** | 向服务端发送最小探测包，检查 TCP 连接是否仍存活 |
| **参数** | `MYSQL*` 连接句柄 |
| **返回值** | `0` = 连接存活；非 `0` = 连接已断开 |
| **开销** | 极小——仅一次 ping-pong 网络往返，不涉及 SQL 解析或执行 |

---

### 6. mysql_close — 关闭连接

```c
void mysql_close(MYSQL *mysql);
```

| 项目 | 说明 |
|------|------|
| **作用** | 关闭 TCP 连接，并释放 `MYSQL` 句柄占用的所有内存 |
| **参数** | `MYSQL*` 连接句柄 |
| **返回值** | 无 |

---

### MySQL C API 调用链全景图

```
mysql_init( )        → 拿"身份证"（空壳句柄）
       ↓
mysql_real_connect( ) → 持身份证 + TCP 连接 MySQL 服务端
       ↓
mysql_query( )        → 发送 SQL 语句
       ↓
mysql_store_result( ) → 如果是 SELECT，将结果拉取到客户端本地
       ↓
mysql_close( )        → 断开连接 + 销毁句柄

mysql_ping( )         → 任意时刻均可调用，验证连接是否存活
```

---

## 二、\<chrono\> 时间库

本项目仅使用了 `std::chrono` 的一个子集，核心围绕三个概念：

### 1. 时钟（Clock）—— steady_clock

```cpp
std::chrono::steady_clock
```

| 项目 | 说明 |
|------|------|
| **本质** | 硬件级别的单调递增秒表 |
| **特性** | 只增不退，不受系统时间修改、NTP 校时影响 |
| **它不是什么** | 不是墙上时钟——不反映"现在是几点"，只反映"从某个起点过了多久" |
| **对比** | `system_clock`（对应 `time()`）可被用户/NTP 修改导致时间倒流；`steady_clock` 绝对安全 |

---

### 2. 时间点（TimePoint）—— time_point 与 now()

```cpp
// "咔嚓"按一下秒表，记录当前时刻
auto t = std::chrono::steady_clock::now();
// t 的类型：std::chrono::steady_clock::time_point
```

| 操作 | 说明 |
|------|------|
| `now()` | 获取当前时刻 |
| 两个 `time_point` 相减 | 得到一个时长（Duration） |
| 存储为成员变量 | 用于后续计算差值（如空闲时长） |

---

### 3. 时长（Duration）—— duration_cast 与 count()

两个 `time_point` 相减得到一个 `duration`（时长），但 chrono 默认精度是纳秒，需**转换单位**。

```cpp
auto now = std::chrono::steady_clock::now();    // 1. 记录当前时刻
auto dur = now - _alivetime;                     // 2. 两个时间点相减 → duration
auto ms  = std::chrono::duration_cast<           // 3. 转换为毫秒
               std::chrono::milliseconds>(dur);
return ms.count();                               // 4. .count() 取出普通整数
```

| 调用 | 作用 |
|------|------|
| `duration_cast<目标单位>(差值)` | 单位转换（纳秒 → 毫秒/秒等） |
| `.count()` | 将 chrono 内部类型转为普通整数（`long long`） |

---

### 常用单位速查

| 写法 | 含义 |
|------|------|
| `std::chrono::nanoseconds` | 纳秒 |
| `std::chrono::microseconds` | 微秒 |
| `std::chrono::milliseconds` | 毫秒 |
| `std::chrono::seconds` | 秒 |

---

### chrono 三段式公式

```
auto t0 = std::chrono::steady_clock::now();           // ① 记录
// ... 耗时操作 ...
auto t1 = std::chrono::steady_clock::now();           // ① 记录
auto dur = t1 - t0;                                    // ② 相减
auto ms = std::chrono::duration_cast<                  // ③ 换算
    std::chrono::milliseconds>(dur).count();           // ③ 读数
```

---

### 项目中的应用场景

| 场景 | 用法 |
|------|------|
| **空闲超时回收** | `_alivetime` 记录归还时刻；`getAliveTime()` 计算 (now - _alivetime) 毫秒数，与 `maxIdleTime * 1000` 比较 |
| **借用超时控制** | `wait_for` 传入 `std::chrono::milliseconds(_connectionTimeout)` |
| **等待耗时监控** | `wait_for` 前后各记录 `steady_clock::now()`，差值累加到 `_totalWaitMs` |

---

## 三、C++ 文件读取 — fstream

### 1. 头文件

```cpp
#include <fstream>   // std::ifstream
#include <string>    // std::string, std::getline, std::stoi
```

---

### 2. 打开文件 — ifstream

```cpp
std::ifstream ifs("mysql.ini");
```

| 项目 | 说明 |
|------|------|
| `std::ifstream` | Input File Stream — 只读文件流对象 |
| **构造参数** | 文件路径（相对路径相对于程序运行目录） |
| `ifs.is_open()` | 返回 `bool`，文件是否成功打开 |
| **文件不存在** | `is_open()` 返回 `false`，不会崩溃 |

**标准打开检查**：

```cpp
std::ifstream ifs("mysql.ini");
if (!ifs.is_open()) {
    return false;  // 文件不存在或无法打开
}
```

---

### 3. 逐行读取 — std::getline

```cpp
std::string line;
while (std::getline(ifs, line)) {
    // 每次循环，line 存储文件的下一行
}
```

| 项目 | 说明 |
|------|------|
| **参数 1** | 文件流对象（`std::ifstream&`） |
| **参数 2** | `std::string&`，读到的行内容存入此变量（每次覆盖） |
| **返回值** | 流对象引用；读到文件末尾（EOF）自动转为 `false`，退出循环 |

**执行过程**：文件有 10 行 → 循环 10 次，每次 `line` 存一行。

---

### 4. 字符串查找 — find

```cpp
std::string line = "port=3306";
size_t idx = line.find('=');  // idx = 4
```

| 项目 | 说明 |
|------|------|
| **参数** | 要查找的字符 |
| **返回值** | `size_t`。找到返回位置（0-based，从 0 开始计数）；找不到返回 `std::string::npos` |

**应对非 key=value 行**（空行、注释）：

```cpp
if (idx == std::string::npos) {
    continue;  // 跳过本行
}
```

---

### 5. 字符串截取 — substr

```cpp
std::string line = "port=3306";
size_t idx = line.find('=');       // idx = 4

std::string key = line.substr(0, idx);     // "port"
std::string val = line.substr(idx + 1);    // "3306"
```

**可视化**：

```
"port=3306"
 012345678
   ↑ idx=4

key = substr(0, 4)    → "port"   (位置 0 开始，取 4 个字符)
val = substr(5)       → "3306"   (位置 5 开始，取到末尾)
```

---

### 6. 字符串转整数 — std::stoi 与安全包装

`std::stoi` 极脆弱：传入 `"abc"` 直接抛异常。

**项目所用的安全包装 `parseInt`**：

```cpp
static bool parseInt(const std::string& s, int& out) {
    try {
        size_t pos;
        out = std::stoi(s, &pos);      // &pos 记录解析停止位置
        return pos == s.size();        // 全部字符消费完毕 → 合法整数
    } catch (...) {
        return false;                  // 转换异常 → 非法
    }
}
```

| 调用 | `std::stoi` 裸调 | `parseInt` 包装 |
|------|-----------------|----------------|
| `"3306"` | 成功 | 成功（pos 走到末尾） |
| `"abc"` | **崩溃** | `false` |
| `"100abc"` | 成功（只读前 3 字）| `false`（pos 未到末尾） |

---

### 7. 完整解析套路

```cpp
bool ConnectionPool::loadConfigFile() {
    std::ifstream ifs("mysql.ini");
    if (!ifs.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        size_t idx = line.find('=');
        if (idx == std::string::npos) continue;

        std::string key = line.substr(0, idx);
        std::string val = line.substr(idx + 1);

        // 字符串类型配置
        if (key == "ip") _ip = val;
        else if (key == "port") {
            int tmp;
            if (!parseInt(val, tmp)) {
                LOG("Invalid port: " + val);
                return false;  // 硬错误
            }
            _port = static_cast<unsigned short>(tmp);
        }
        // ... 以此类推
    }

    // 末尾校验块（硬错误 + 软纠正）
    return true;
}
```

---

### 8. 涉及的 C++ 类型速查

| 类型 | 头文件 | 用途 |
|------|--------|------|
| `std::ifstream` | `<fstream>` | 文件读取流 |
| `std::string` | `<string>` | 字符串（key/value 存储） |
| `std::getline` | `<string>` | 从流中读取一行到 string |
| `std::stoi` | `<string>` | string → int（需 try-catch 保护） |
| `std::stod` | `<string>` | string → double |

---

## 四、多线程同步 — thread, mutex, lock, condition_variable, atomic

### 1. std::thread — 创建线程

```cpp
#include <thread>
```

| 项目 | 说明 |
|------|------|
| **本质** | 代表一个操作系统线程的句柄 |
| **构造** | `std::thread t(可调用对象, 参数...)` — 构造即启动线程 |
| **线程函数** | 可以是普通函数、成员函数、lambda |
| 线程结束后 | 线程函数 return 时线程自然结束 |

#### 启动普通函数

```cpp
void worker() { /* ... */ }
std::thread t(worker);     // 启动线程，执行 worker()
t.detach();                // 分离：线程独立运行，t 不再管它
```

#### 启动成员函数（本项目用法）

```cpp
std::thread t(&ConnectionPool::produceConnectionTask, this);
t.detach();
```

| 参数 | 含义 |
|------|------|
| `&ConnectionPool::produceConnectionTask` | 成员函数地址 |
| `this` | 绑定的对象指针 |

**为什么不能直接写 `produceConnectionTask`？** 非静态成员函数有一个隐式的 `this` 参数，编译器必须知道绑到哪个对象。写成 `std::thread t(produceConnectionTask)` 语法不对——编译器找不到这个自由函数。

#### 或者用 lambda（更直观）

```cpp
std::thread t([this]() { this->produceConnectionTask(); });
t.detach();
```

#### detach vs join

| 操作 | 效果 | 使用场景 |
|------|------|---------|
| `t.detach()` | 线程与 t 对象脱钩，独立运行到结束 | 后台守护线程（本项目） |
| `t.join()` | 阻塞等待线程结束 | 需要等待结果再继续 |

**本项目全部用 detach**：生产者、扫描者都是后台永久运行的守护线程，没人等它们结束。

---

### 2. std::this_thread::sleep_for — 线程休眠

```cpp
#include <thread>
#include <chrono>

std::this_thread::sleep_for(std::chrono::seconds(60));     // 睡 60 秒
std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 睡 100 毫秒
```

| 项目 | 说明 |
|------|------|
| **作用** | 让当前线程挂起指定时长 |
| **需要锁吗** | **不需要**，线程直接休眠，不持有任何资源 |
| **与条件变量 wait 的区别** | `sleep_for` 纯时间驱动，不会被 notify 唤醒；`cv.wait_for` 可被 notify 提前唤醒 |

**本项目应用**：`scannerConnectionTask()` 用 `sleep_for` 定期清理——睡 `maxIdleTime` 秒后醒来扫描一圈，然后继续睡。无需条件变量通知。

---

### 3. std::mutex — 互斥锁

```cpp
#include <mutex>

std::mutex _queueMutex;
```

| 项目 | 说明 |
|------|------|
| **本质** | 一把"门锁"，同一时刻只有一个线程能持有 |
| `_queueMutex.lock()` | 拿锁——别人拿着就阻塞等待 |
| `_queueMutex.unlock()` | 放锁——下一个等待的线程拿到 |
| **手动 lock/unlock 的风险** | 中间抛异常 → unlock 永远执行不到 → 死锁 |

**比喻**：一个厕所坑位，进门锁门（lock），出来开锁（unlock）。忘记开锁后面的人永远进不去。

---

### 4. std::lock_guard — RAII 自动锁（轻量级）

```cpp
{
    std::lock_guard<std::mutex> lock(_queueMutex);
    // 构造时自动 lock()
    stats.idleConnections = _connectionQue.size();
    // 离开作用域自动 unlock()
}
```

| 项目 | 说明 |
|------|------|
| **构造时** | 自动 `lock()` |
| **析构时** | 自动 `unlock()`，无论如何离开作用域都执行 |
| **适用场景** | 简单的"加锁-干活-解锁"，不需要中途解锁 |
| **不可手动 unlock** | lock_guard 没有 `unlock()` 方法 |
| **不可拷贝** | 不能赋值/传参 |

**项目应用**：`getStats()` 中读 `queue.size()` 用了 lock_guard。

---

### 5. std::unique_lock — 灵活锁（配合条件变量）

```cpp
std::unique_lock<std::mutex> lock(_queueMutex);
// 可以随时 unlock() 再 lock()
// 配合条件变量必须用 unique_lock
```

| 对比 | lock_guard | unique_lock |
|------|-----------|-------------|
| **自动加锁** | ✅ | ✅ |
| **自动解锁** | ✅ | ✅ |
| **手动 unlock()** | ❌ | ✅ |
| **配合条件变量** | ❌ 不行 | ✅ 必须 |
| **性能开销** | 极小 | 稍大（多了状态标志） |
| **适用场景** | 简单临界区 | 需要 wait/notify 或中途解锁 |

**项目应用**：`getConnection()`、`produceConnectionTask()`、`scannerConnectionTask()` 全部用 unique_lock，因为需要配合 `condition_variable`。

---

### 6. std::condition_variable — 条件变量（线程间通知）

这把锁的脑子。光有 mutex 不够——消费者看到队列空时不能一直空转（浪费 CPU），需要"睡"着等，等有人放了连接进来"叫醒"。

```cpp
#include <condition_variable>

std::condition_variable _cv;
```

#### 核心操作

| 操作 | 作用 | 前提 |
|------|------|------|
| `_cv.wait(lock)` | **"我睡了，有货叫我"**。释放锁→阻塞→被唤醒→重新拿锁→继续 | 必须持有锁 |
| `_cv.wait_for(lock, 时长)` | 同上，但最多等一段时间，超时自动醒来 | 必须持有锁 |
| `_cv.notify_one()` | **"醒一个"**。唤醒一个等待线程 | 可持锁可不持 |
| `_cv.notify_all()` | **"全醒"**。唤醒所有等待线程 | 可持锁可不持 |

#### wait 的详细过程

```cpp
std::unique_lock<std::mutex> lock(_queueMutex);
while (_connectionQue.empty()) {
    _cv.wait(lock);  // ① 释放锁 ② 阻塞睡觉 ③ 被唤醒 ④ 重新拿锁 ⑤ 返回
}
```

**为什么 while 不是 if？** `wait` 可能被**虚假唤醒**（POSIX 允许操作系统无故唤醒线程）。if 只检查一次，被虚假唤醒后跳过检查继续执行，队列可能还是空的 → UB。while 醒后重新检查条件。

#### 项目中条件变量的使用场景

| 位置 | 代码 | 含义 |
|------|------|------|
| `getConnection()` 等连接 | `_cv.wait_for(lock, timeout)` | 队列空，睡等生产者/归还者通知 |
| `getConnection()` 等前 | `_cv.notify_all()` | 告诉生产者"队列空了，快生产" |
| `getConnection()` 归还删除器 | `_cv.notify_all()` | 归还了一条连接，通知等待者来取 |
| `Producer` 省电 | `_cv.wait(lock)` | 队列非空，生产者睡等队列变空 |
| `Producer` 生产完 | `_cv.notify_all()` | 新连接入队，通知消费者 |

---

### 7. std::atomic — 无锁原子变量

```cpp
#include <atomic>

std::atomic_long _connectionCnt{0};
std::atomic_long _totalBorrows{0};
```

| 操作 | 含义 |
|------|------|
| `_connectionCnt++` | 原子自增 |
| `_connectionCnt--` | 原子自减 |
| `_connectionCnt.load()` | 原子读取当前值 |
| `_connectionCnt.store(5)` | 原子写入 |
| `_totalWaitMs.fetch_add(ms)` | 原子加 ms，返回旧值 |

**为什么不用普通 `int` + mutex？** atomic 是 CPU 硬件级别的原子指令，比 mutex 快数十倍。适用于**简单的增减计数**。

**但原子不能替代 mutex！** 原子只管一个变量操作是原子的，管不了"检查队列空 + 取连接"这种多步事务。复合操作必须用 mutex。

---

### 8. 项目同步全景图

```
共享资源：_connectionQue (queue)、_connectionCnt (atomic)

  消费者 getConnection()
  ├── lock(_queueMutex)
  ├── while (queue空) { notify_all(); wait_for(timeout); }
  ├── 取队头
  ├── unlock (unique_lock 析构自动)
  └── 用完 → 自定义删除器
            ├── lock
            ├── push 回队列
            ├── notify_all()
            └── unlock

  生产者 produceConnectionTask()
  ├── lock
  ├── while (queue非空) { wait(); }  // 有货就睡
  ├── if (cnt < max) addConnection()
  ├── notify_all()
  └── unlock

  扫描者 scannerConnectionTask()
  ├── sleep(maxIdleTime秒)  // 不在锁内
  ├── lock
  ├── while (cnt > init && queue非空 && 队头超时)
  │     { pop; delete; cnt--; }
  └── unlock
```

---

### 9. 关键原则总结

| 原则 | 原因 |
|------|------|
| **永远用 lock_guard / unique_lock，不手动 lock/unlock** | 异常安全，忘记解锁=死锁 |
| **条件变量配合 while，不是 if** | 防止虚假唤醒 |
| **wait 前检查条件，wait 后复查条件** | 可能被 notify 但条件仍未满足 |
| **锁内操作尽量轻量** | 锁是瓶颈，持锁做耗时操作会堵死所有线程 |
| **atomic 只用于简单计数** | 复合操作用 mutex |
| **notify 放在 unlock 前还是后都可以** | 都是正确的，放解锁后理论上稍高效 |


