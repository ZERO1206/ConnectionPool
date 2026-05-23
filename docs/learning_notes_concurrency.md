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
