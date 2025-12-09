# 八股知识点 - Day 4 任务模块开发新增

> **📌 文档重构说明**  
> 本文档已拆分为独立的知识点文档,便于查找和复习。  
> 请参考 [八股知识点索引](./README.md) 查看最新的文档结构。
>
> **已拆分的知识点** (全部完成 ✅):
>
> - [静态工厂方法](./八股-静态工厂方法.md) ✅
> - [单例模式与智能指针](./八股-单例模式与智能指针.md) ✅
> - [RAII 原则](./八股-RAII原则.md) ✅
> - [JSON 解析最佳实践](./八股-JSON解析最佳实践.md) ✅
> - [交叉编译链接器选项](./八股-交叉编译与动态链接.md) ✅ (补充到已有文档)
> - [测试策略与测试金字塔](./八股-测试策略与测试金字塔.md) ✅
>
> 本文档保留作为 Day 4 的完整记录,但**强烈建议优先阅读拆分后的独立文档**。

---

**日期**: 2025 年 11 月 9 日  
**来源**: Day 4 任务模块功能验证实战  
**难度**: ⭐⭐⭐ (中高级)  
**包含知识点**: 静态工厂方法 | 单例模式 | RAII 原则 | JSON 解析 | 交叉编译 | 测试策略

---

## 📌 静态工厂方法 vs 构造函数

### 知识点

**静态工厂方法 (Static Factory Method)** 是一种创建对象的设计模式,通过静态方法而非构造函数创建对象。

### 项目中的例子

```cpp
// include/esdk_sophon/task/TaskTypes.h
struct TaskConfig {
    std::string taskId;
    TaskType type;
    // ... 其他字段

    // ✅ 静态工厂方法: 从JSON创建
    static TaskConfig fromJson(const nlohmann::json& json);

    // 如果用构造函数会是这样:
    // ❌ TaskConfig(const nlohmann::json& json);  // 语义不够清晰
};

// 使用示例
TaskConfig config = TaskConfig::fromJson(mqttMessage);  // 清晰的语义
```

### 详细讲解

#### 为什么用静态工厂方法而不用构造函数?

| 特性         | 构造函数             | 静态工厂方法         |
| ------------ | -------------------- | -------------------- |
| **名称**     | 必须与类名相同       | 可以有清晰的语义名称 |
| **返回类型** | 只能返回当前类       | 可以返回子类或派生类 |
| **实例控制** | 每次调用都创建新对象 | 可以返回缓存的对象   |
| **参数类型** | 重载可能产生歧义     | 不同名称避免歧义     |

#### 实际例子对比

```cpp
// ❌ 使用构造函数 (语义不清晰)
class TaskConfig {
public:
    TaskConfig(const nlohmann::json& json);        // 从JSON?
    TaskConfig(const std::string& configFile);     // 从文件?
    TaskConfig(int taskId);                        // 从数据库?
    // 参数类型不同,但意图不清晰
};

// ✅ 使用静态工厂方法 (语义清晰)
class TaskConfig {
public:
    static TaskConfig fromJson(const nlohmann::json& json);
    static TaskConfig fromFile(const std::string& configFile);
    static TaskConfig fromDatabase(int taskId);
    // 一眼就知道从哪里来
};
```

### 面试要点

**Q1: 静态工厂方法相比构造函数有什么优势?**

**标准答案**:

1. **语义清晰**: 方法名可以描述对象的创建方式

   ```cpp
   TaskConfig::fromJson(json)    // 清楚知道是从JSON创建
   TaskConfig::fromDatabase(id)  // 清楚知道是从数据库创建
   ```

2. **返回类型灵活**: 可以返回子类

   ```cpp
   static std::unique_ptr<ITask> createTask(TaskType type) {
       if (type == LIVESTREAM) return std::make_unique<LiveStreamTask>();
       if (type == MEDIAFILE) return std::make_unique<MediaFileTask>();
   }
   ```

3. **实例控制**: 可以返回缓存的对象,减少对象创建

   ```cpp
   static TaskConfig& getDefault() {
       static TaskConfig defaultConfig;  // 单例
       return defaultConfig;
   }
   ```

4. **避免歧义**: 构造函数重载可能产生歧义

   ```cpp
   // ❌ 歧义: 两个都是 int 参数
   TaskConfig(int taskId);         // 从ID创建
   TaskConfig(int algorithmId);    // 从算法ID创建?

   // ✅ 清晰: 不同的方法名
   static TaskConfig fromTaskId(int id);
   static TaskConfig fromAlgorithmId(int id);
   ```

**Q2: 静态工厂方法有什么缺点?**

**答**:

1. **无法继承**: 子类无法继承静态方法
2. **发现性差**: IDE 不会在构造对象时自动提示静态工厂方法
3. **命名不统一**: 没有标准的命名规范 (from*, of*, valueOf*, getInstance*)

**Q3: Java 中的静态工厂方法和 C++有什么区别?**

**答**:

- **Java**: 静态工厂方法非常常见 (如 `Integer.valueOf()`, `Collections.emptyList()`)
- **C++**: 更倾向于使用构造函数,但现代 C++17 推荐使用静态工厂方法提升代码可读性

---

## 📌 单例模式与 shared_ptr 的自定义删除器

### 知识点

**单例模式 (Singleton Pattern)** 确保类只有一个实例,并提供全局访问点。
**shared_ptr 自定义删除器** 允许自定义对象的销毁行为。

### 项目中的例子

```cpp
// MqttClient 是单例
class MqttClient {
public:
    static MqttClient& getInstance() {
        static MqttClient instance;  // Meyers单例
        return instance;
    }

private:
    MqttClient() = default;  // 私有构造
    ~MqttClient() = default;
    MqttClient(const MqttClient&) = delete;  // 禁止拷贝
};

// 测试代码中需要 shared_ptr,但不能delete单例对象
auto mqttClientPtr = std::shared_ptr<MqttClient>(
    &MqttClient::getInstance(),          // 原始指针
    [](MqttClient*) {}                   // 空删除器 (什么都不做)
);
```

### 详细讲解

#### Meyers 单例 (C++11 推荐)

```cpp
// C++11之前: 双检查锁 (DCL) - 复杂且容易出错
class Singleton {
public:
    static Singleton* getInstance() {
        if (instance == nullptr) {
            std::lock_guard<std::mutex> lock(mutex);
            if (instance == nullptr) {  // 双重检查
                instance = new Singleton();
            }
        }
        return instance;
    }
private:
    static Singleton* instance;
    static std::mutex mutex;
};

// ✅ C++11之后: Meyers单例 - 简洁且线程安全
class Singleton {
public:
    static Singleton& getInstance() {
        static Singleton instance;  // C++11保证线程安全初始化
        return instance;
    }
private:
    Singleton() = default;
};
```

#### shared_ptr 自定义删除器

```cpp
// 默认删除器: delete ptr
std::shared_ptr<int> p1(new int(42));
// 等价于
std::shared_ptr<int> p1(new int(42), std::default_delete<int>());

// 自定义删除器: 文件操作
std::shared_ptr<FILE> filePtr(
    fopen("file.txt", "r"),
    [](FILE* f) {
        if (f) fclose(f);  // 关闭文件而非delete
    }
);

// 自定义删除器: 数组
std::shared_ptr<int> arrayPtr(
    new int[10],
    [](int* p) { delete[] p; }  // 使用 delete[] 而非 delete
);

// 空删除器: 单例对象
std::shared_ptr<Singleton> singletonPtr(
    &Singleton::getInstance(),
    [](Singleton*) {}  // 什么都不做,不delete单例
);
```

### 面试要点

**Q1: 为什么 C++11 的 Meyers 单例是线程安全的?**

**标准答案**:
C++11 标准规定: **局部静态变量的初始化是线程安全的**。

```cpp
static Singleton& getInstance() {
    static Singleton instance;  // ← 只会初始化一次
    return instance;
}
```

**原理** (编译器实现):

```cpp
// 编译器大致会生成类似这样的代码
static Singleton& getInstance() {
    static bool initialized = false;
    static std::aligned_storage<sizeof(Singleton)> storage;

    if (!initialized) {
        std::lock_guard<std::mutex> lock(hidden_mutex);  // 隐式加锁
        if (!initialized) {
            new (&storage) Singleton();  // placement new
            initialized = true;
        }
    }
    return *reinterpret_cast<Singleton*>(&storage);
}
```

**Q2: shared_ptr 的自定义删除器有什么用?**

**答**:

1. **非 new 分配的资源**: 文件句柄、网络连接、数组等

   ```cpp
   std::shared_ptr<FILE>(fopen("f.txt", "r"), fclose);
   ```

2. **特殊释放逻辑**: 需要先清理再释放

   ```cpp
   std::shared_ptr<Device>(
       new Device(),
       [](Device* d) {
           d->disconnect();  // 先断开连接
           delete d;         // 再释放内存
       }
   );
   ```

3. **不应该释放的资源**: 单例、栈对象等
   ```cpp
   std::shared_ptr<Singleton>(&Singleton::getInstance(), [](Singleton*){});
   ```

**Q3: 为什么单例的 shared_ptr 需要空删除器?**

**答**:

```cpp
// ❌ 错误: 会尝试delete单例对象
auto ptr1 = std::shared_ptr<Singleton>(&Singleton::getInstance());
// 当ptr1引用计数归零时,会调用 delete &getInstance()
// 导致 double free 或其他未定义行为!

// ✅ 正确: 使用空删除器
auto ptr2 = std::shared_ptr<Singleton>(
    &Singleton::getInstance(),
    [](Singleton*) {}  // 什么都不做
);
// 引用计数归零时,调用空删除器,不会delete对象
```

---

## 📌 RAII 原则 (资源获取即初始化)

### 知识点

**RAII (Resource Acquisition Is Initialization)** 是 C++中管理资源的核心技术,通过对象的生命周期自动管理资源。

### 项目中的例子

```cpp
// TaskManager 使用RAII管理资源
class TaskManager {
public:
    // 获取资源
    bool initialize(std::shared_ptr<mqtt::MqttClient> mqtt) {
        mqtt_ = mqtt;
        service_ = std::make_shared<TaskService>(mqtt_);
        initialized_ = true;
        return true;
    }

    // 释放资源
    void shutdown() {
        stopAllTasks();
        tasks_.clear();
        service_.reset();
        initialized_ = false;
    }

    // 析构时自动释放 (RAII核心)
    ~TaskManager() {
        shutdown();
    }

private:
    bool initialized_ = false;
    std::shared_ptr<TaskService> service_;
    std::shared_ptr<mqtt::MqttClient> mqtt_;
};
```

### 详细讲解

#### RAII 的三个核心原则

1. **资源在构造函数中获取**
2. **资源在析构函数中释放**
3. **资源的生命周期与对象的生命周期绑定**

#### 经典例子

```cpp
// ❌ 不使用RAII (手动管理)
void processFile() {
    FILE* file = fopen("data.txt", "r");
    if (!file) return;

    // ... 处理文件 ...

    if (error) {
        // ❌ 容易忘记关闭文件!
        return;
    }

    fclose(file);  // 需要在每个退出点都写
}

// ✅ 使用RAII (自动管理)
class FileHandle {
public:
    explicit FileHandle(const char* filename)
        : file_(fopen(filename, "r")) {
        if (!file_) throw std::runtime_error("Failed to open file");
    }

    ~FileHandle() {
        if (file_) fclose(file_);  // 自动关闭
    }

    FILE* get() const { return file_; }

private:
    FILE* file_;
};

void processFile() {
    FileHandle file("data.txt");  // 构造时打开

    // ... 处理文件 ...

    if (error) {
        return;  // ✅ 析构函数自动关闭文件
    }

    // ✅ 函数结束时自动关闭文件
}
```

#### 智能指针是 RAII 的典型应用

```cpp
// ❌ 手动管理内存
void foo() {
    MyClass* ptr = new MyClass();

    // ... 使用ptr ...

    if (error) {
        delete ptr;  // 容易忘记
        return;
    }

    delete ptr;
}

// ✅ 智能指针 (RAII)
void foo() {
    std::unique_ptr<MyClass> ptr = std::make_unique<MyClass>();

    // ... 使用ptr ...

    if (error) {
        return;  // ✅ 自动释放
    }

    // ✅ 函数结束自动释放
}
```

### 面试要点

**Q1: RAII 有什么好处?**

**标准答案**:

1. **异常安全**: 即使抛异常,析构函数也会被调用

   ```cpp
   void foo() {
       std::lock_guard<std::mutex> lock(mutex);  // RAII锁
       // ... 如果抛异常 ...
   }  // 锁自动释放
   ```

2. **代码简洁**: 无需在每个退出点手动释放资源

3. **不会忘记释放**: 编译器保证析构函数调用

4. **易于维护**: 资源管理逻辑集中在类中

**Q2: RAII 和 finally 块有什么区别?**

**答**:

```java
// Java的finally (需要手动写)
try {
    FileInputStream file = new FileInputStream("file.txt");
    // ... 使用file ...
} finally {
    if (file != null) file.close();  // 手动关闭
}
```

```cpp
// C++的RAII (自动)
{
    std::ifstream file("file.txt");  // 构造时打开
    // ... 使用file ...
}  // 析构时自动关闭,无需finally
```

**C++没有 finally,因为 RAII 更强大**:

- finally 需要手动写释放代码
- RAII 由编译器保证调用析构函数

**Q3: TaskManager 为什么需要 initialize()而不在构造函数中初始化?**

**答**:

**原因 1**: 单例的构造函数不应该执行复杂操作

```cpp
// ❌ 不好: 构造函数可能失败
TaskManager::TaskManager() {
    // 如果这里失败怎么办? 构造函数不能返回错误码
    mqtt_ = /* 复杂的初始化 */;
}

// ✅ 好: 两阶段初始化
TaskManager::TaskManager() {
    // 轻量级初始化
}

bool TaskManager::initialize(/* 参数 */) {
    // 复杂初始化,可以返回bool表示成功或失败
    return true;
}
```

**原因 2**: 依赖注入需要在构造后设置

```cpp
TaskManager& mgr = TaskManager::getInstance();  // 构造
mgr.initialize(mqttClient);  // 注入依赖
```

---

## 📌 JSON 解析最佳实践

### 知识点

使用 nlohmann/json 库解析 JSON 时的安全写法和错误处理。

### 项目中的例子

```cpp
TaskConfig TaskConfig::fromJson(const nlohmann::json& json) {
    TaskConfig config;

    // ✅ at(): 必需字段,缺失会抛异常
    config.taskId = std::to_string(json.at("taskID").get<int>());

    // ✅ value(): 可选字段,提供默认值
    config.algorithmId = json.value("algorithmRepoID", 0);

    // ✅ contains() + is_array(): 检查类型
    if (json.contains("type") && json["type"].is_array()) {
        for (const auto& typeObj : json["type"]) {
            EventType eventType;
            eventType.id = typeObj.value("id", 0);
            config.eventTypes.push_back(eventType);
        }
    }

    return config;
}
```

### 详细讲解

#### nlohmann/json 的三种访问方式

```cpp
nlohmann::json json = R"({
    "id": 123,
    "name": "test",
    "optional": "value"
})"_json;

// 方式1: at() - 严格模式,字段缺失抛异常
int id = json.at("id").get<int>();           // ✅ 字段存在
int missing = json.at("missing").get<int>(); // ❌ 抛出 json::out_of_range

// 方式2: operator[] - 宽松模式,字段缺失返回null
int id2 = json["id"].get<int>();             // ✅ 字段存在
auto val = json["missing"];                  // ✅ 返回 null (不抛异常)
// 但是:
int bad = json["missing"].get<int>();        // ❌ 运行时错误 (null转int失败)

// 方式3: value() - 带默认值,最安全
int id3 = json.value("id", 0);               // ✅ 字段存在,返回123
int def = json.value("missing", 0);          // ✅ 字段缺失,返回默认值0
```

#### 最佳实践对比

```cpp
// ❌ 不好: 不检查字段是否存在
TaskConfig fromJson(const nlohmann::json& json) {
    TaskConfig config;
    config.id = json["taskID"].get<int>();  // 如果字段不存在会怎样?
    return config;
}

// ✅ 好: 区分必需字段和可选字段
TaskConfig fromJson(const nlohmann::json& json) {
    TaskConfig config;

    // 必需字段: 使用at(),缺失应该报错
    try {
        config.id = json.at("taskID").get<int>();
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("缺少必需字段: taskID");
    }

    // 可选字段: 使用value(),提供默认值
    config.priority = json.value("priority", 0);

    // 复杂类型: 先检查类型
    if (json.contains("tags") && json["tags"].is_array()) {
        for (const auto& tag : json["tags"]) {
            config.tags.push_back(tag.get<std::string>());
        }
    }

    return config;
}
```

### 面试要点

**Q1: nlohmann/json 的 at()和 operator[]有什么区别?**

**标准答案**:

| 方法                  | 字段存在 | 字段不存在 | 使用场景 |
| --------------------- | -------- | ---------- | -------- |
| `at()`                | 返回值   | 抛出异常   | 必需字段 |
| `operator[]`          | 返回值   | 返回 null  | 动态访问 |
| `value(key, default)` | 返回值   | 返回默认值 | 可选字段 |

```cpp
json.at("id");          // 字段必须存在
json["id"];             // 字段可以不存在,返回null
json.value("id", 0);    // 字段可以不存在,返回默认值
```

**Q2: 如何判断 JSON 字段的类型?**

**答**:

```cpp
nlohmann::json json = /* ... */;

if (json.contains("field")) {
    if (json["field"].is_null())    { /* null */ }
    if (json["field"].is_boolean()) { /* bool */ }
    if (json["field"].is_number())  { /* int/float */ }
    if (json["field"].is_string())  { /* string */ }
    if (json["field"].is_array())   { /* 数组 */ }
    if (json["field"].is_object())  { /* 对象 */ }
}
```

**Q3: 项目中为什么用 at()和 value()混合?**

**答**:

```cpp
// 必需字段: taskID,缺失是客户端错误,应该报错
config.taskId = std::to_string(json.at("taskID").get<int>());

// 可选字段: priority,缺失使用默认值
config.priority = json.value("priority", 0);
```

这样做的好处:

1. **清晰的语义**: 一眼看出哪些是必需字段
2. **错误处理**: 必需字段缺失时快速失败
3. **容错性**: 可选字段缺失时使用合理默认值

---

## 📌 交叉编译链接器选项

### 知识点

交叉编译时,链接器选项 `--unresolved-symbols=ignore-in-shared-libs` 的作用和风险。

### 项目中的例子

```cmake
# CMakeLists.txt
# 解决OpenCV/GStreamer在交叉编译时的200+链接警告
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -Wl,--unresolved-symbols=ignore-in-shared-libs")
set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--unresolved-symbols=ignore-in-shared-libs")
```

### 详细讲解

#### 问题背景

```bash
# 编译时出现大量警告
warning: libz.so.1, needed by libopencv_core.so, not found
warning: libjpeg.so.8, needed by libopencv_core.so, not found
warning: libpng16.so.16, needed by libopencv_core.so, not found
... (200+ warnings)
```

#### 链接过程

```
源代码 (.cpp)
    ↓ [编译器]
目标文件 (.o)
    ↓ [链接器]
可执行文件 (./test_task_basic)
    ↓ [运行时]
动态链接器 (ld.so) 加载 .so 文件
```

#### 静态链接 vs 动态链接

```cpp
// 静态链接 (.a)
// 编译时: 将库代码复制到可执行文件中
// 优点: 无运行时依赖
// 缺点: 可执行文件体积大

// 动态链接 (.so)
// 编译时: 只记录符号引用
// 运行时: 加载共享库
// 优点: 可执行文件体积小,共享内存
// 缺点: 需要运行时依赖
```

#### 链接器选项对比

```cmake
# 默认行为: 检查所有符号
# 如果 .so 文件缺失或符号未定义 → 链接失败

# 选项1: 忽略共享库中的未定义符号
--unresolved-symbols=ignore-in-shared-libs
# 编译时: 不检查 .so 中的符号
# 运行时: 必须有真实的 .so 文件

# 选项2: 忽略所有未定义符号 (危险!)
--unresolved-symbols=ignore-all
# 编译时: 不检查任何符号
# 运行时: 可能崩溃
```

### 面试要点

**Q1: 为什么交叉编译需要这个选项?**

**答**:

```
编译环境 (Docker x86_64):
- 有 aarch64 版本的头文件
- 有 aarch64 版本的 .so 文件
- 但 .so 依赖的其他库可能版本不匹配

运行环境 (SE7 aarch64):
- 有完整的运行时库
- 所有依赖都能正确加载
```

**Q2: 这个选项有什么风险?**

**答**:

```
编译成功 ≠ 运行成功
```

风险:

1. **运行时缺少库**: 如果 SE7 设备缺少某些 .so 文件,程序会启动失败

   ```bash
   error while loading shared libraries: libdc1394.so.22: cannot open shared object file
   ```

2. **版本不匹配**: 编译时和运行时的库版本不一致,可能导致未定义行为

3. **难以调试**: 编译时不报错,运行时才发现问题

**解决方法**:

```bash
# 1. 编译后用ldd检查依赖
ldd ./test_task_basic | grep "not found"

# 2. 在运行设备上安装缺失的库
sudo apt-get install libdc1394-22 libjpeg8 libpng16-16
```

**Q3: 什么情况下应该使用这个选项?**

**答**:

- ✅ **应该用**: 交叉编译,且确定运行环境有完整的依赖库
- ❌ **不应该用**: 本地编译,应该在编译时就发现问题

---

## 📌 测试策略: 测试金字塔

### 知识点

软件测试的分层策略,平衡测试覆盖率和开发效率。

### 项目中的例子

```
        /\
       /E2E\       SE7上实际运行 (1个测试)
      /____\
     /      \
    /  集成  \     test_livestream_task.cpp (10+测试)
   /________\
  /          \
 /  单元测试  \   test_task_basic.cpp (3个测试) ← Day 4选择
/______________\
```

我们选择先写**单元测试** (test_task_basic):

- 只测试对象创建和销毁
- 不测试运行时行为
- 快速验证基础功能

### 详细讲解

#### 测试金字塔的三层

| 层级         | 数量 | 速度 | 成本 | 覆盖范围    | 示例                    |
| ------------ | ---- | ---- | ---- | ----------- | ----------------------- |
| **E2E 测试** | 少   | 慢   | 高   | 整个系统    | 在 SE7 上实际运行       |
| **集成测试** | 中   | 中   | 中   | 多个模块    | TaskManager+TaskService |
| **单元测试** | 多   | 快   | 低   | 单个类/函数 | TaskConfig::fromJson()  |

#### 实际例子

```cpp
// 单元测试 (test_task_basic.cpp) - Day 4
void test_livestream_task_creation() {
    auto task = std::make_unique<LiveStreamTask>(config, service);
    assert(task != nullptr);  // 只测试能否创建
    // 不测试 start(), stop(), processFrame() 等运行时行为
}

// 集成测试 (test_livestream_task.cpp) - 后续完善
void test_frame_processing() {
    LiveStreamTask task(config, service);
    task.start();                    // 启动任务

    // 模拟处理100帧
    for (int i = 0; i < 100; ++i) {
        task.processFrame(frame);
    }

    auto stats = task.getStatistics();
    assert(stats.processedCount == 100);  // 验证帧数
}

// E2E测试 - 在SE7上实际运行
// 1. 启动主程序
// 2. 连接真实的MQTT Broker
// 3. 发送真实的任务指令
// 4. 处理真实的视频流
// 5. 验证检测结果上报
```

### 面试要点

**Q1: 为什么测试金字塔底部是单元测试?**

**答**:

1. **快速反馈**: 单元测试运行快,可以频繁执行
2. **易于定位**: 失败时能快速定位到具体函数
3. **低成本**: 不需要复杂的环境搭建
4. **高覆盖**: 可以测试边界条件和异常路径

**Q2: Day 4 为什么选择单元测试而非集成测试?**

**答**:

```
目标: 快速验证Day 4的基础功能

选择A: 修复10+个集成测试文件
- 工作量: 大 (5+文件, 30+测试函数)
- 时间: 慢 (需要理解复杂的Mock逻辑)
- 风险: 高 (可能引入新bug)

选择B: 创建3个简单的单元测试
- 工作量: 小 (1个文件, 3个测试函数)
- 时间: 快 (1小时内完成)
- 风险: 低 (逻辑简单,不易出错)

结论: 选择B,先验证基础功能,后续再完善集成测试
```

**Q3: 单元测试有什么局限性?**

**答**:

1. **无法测试集成**: 模块间的交互可能有问题
2. **无法测试性能**: 单个函数可能很快,但整体性能差
3. **无法测试真实环境**: 实际运行时可能有意外情况

**因此需要测试金字塔**:

- 单元测试: 覆盖基础逻辑
- 集成测试: 验证模块协同
- E2E 测试: 验证真实场景

---

## 🎯 总结

### Day 4 新增的核心知识点

1. **静态工厂方法**: `TaskConfig::fromJson()` - 语义清晰的对象创建
2. **单例模式**: Meyers 单例 + shared_ptr 自定义删除器
3. **RAII 原则**: `TaskManager::initialize/shutdown` - 资源自动管理
4. **JSON 解析**: `at()` vs `value()` - 安全的字段访问
5. **交叉编译**: 链接器选项 - 处理运行时依赖
6. **测试策略**: 测试金字塔 - 快速反馈优先

### 面试高频考点

- ✅ 静态工厂方法 vs 构造函数 (⭐⭐⭐)
- ✅ 单例模式的线程安全实现 (⭐⭐⭐⭐⭐)
- ✅ RAII 原则和异常安全 (⭐⭐⭐⭐)
- ✅ shared_ptr 自定义删除器 (⭐⭐⭐)
- ✅ 测试策略和测试金字塔 (⭐⭐⭐)

### 项目经验总结

**Q: 如何在面试中讲述这个项目经验?**

**答** (STAR 法则):

- **S**ituation: 在 DJI ESDK 项目中,需要重构任务模块
- **T**ask: 我负责实现 LiveStreamTask 和 MediaFileTask,并集成 MQTT 消息处理
- **A**ction:
  - 使用静态工厂方法 `TaskConfig::fromJson()` 简化 JSON 解析
  - 应用 RAII 原则管理 TaskManager 的资源生命周期
  - 采用测试金字塔策略,先写单元测试快速验证功能
- **R**esult:
  - 代码简化 60 行,提升可维护性
  - 所有基础测试通过,验证了核心功能
  - 掌握了现代 C++17 的最佳实践

---

**日期**: 2025 年 11 月 9 日  
**作者**: ESDK Sophon Team  
**下次更新**: Day 5 完成后
