# 八股知识点 - Task 模块核心知识

> **项目背景**: DJI ESDK Sophon 项目 Task 模块重构
> **更新日期**: 2025-11-02
> **学习阶段**: Day 1 - 接口定义层

---

## 目录

1. [enum class (强类型枚举)](#1-enum-class-强类型枚举)
2. [虚析构函数 (Virtual Destructor)](#2-虚析构函数-virtual-destructor)
3. [纯虚函数和抽象类](#3-纯虚函数和抽象类)
4. [std::function 回调机制](#4-stdfunction-回调机制)
5. [模板方法模式](#5-模板方法模式-template-method-pattern)
6. [智能指针详解](#6-智能指针详解)
7. [Pimpl 惯用法](#7-pimpl-惯用法)
8. [Meyers' Singleton (线程安全单例)](#8-meyers-singleton-线程安全单例)
9. [mutable 关键字](#9-mutable-关键字)
10. [依赖注入 (Dependency Injection)](#10-依赖注入-dependency-injection)

---

## 1. enum class (强类型枚举)

### 📌 知识点

C++11 引入的 `enum class` 是**强类型、作用域安全**的枚举类型,解决了传统 `enum` 的诸多问题。

### 经典例子

```cpp
// ❌ 传统 enum 的问题
enum Color { RED, GREEN, BLUE };
enum Fruit { APPLE, BANANA, ORANGE };

int x = RED;  // ✅ 编译通过,隐式转换为 int,不安全!
if (RED == APPLE) { }  // ✅ 编译通过,逻辑错误!

// ✅ enum class 的优势
enum class Color { RED, GREEN, BLUE };
enum class Fruit { APPLE, BANANA };

int x = Color::RED;  // ❌ 编译错误,类型安全!
if (Color::RED == Fruit::APPLE) { }  // ❌ 编译错误,防止混淆!
Color c = Color::RED;  // ✅ 必须带作用域访问
```

### 项目中的例子

```cpp
// TaskTypes.h
enum class TaskType {
    DETECTION_LIVESTREAM = 0,  // 直播流检测
    DETECTION_MEDIAFILE = 1,   // 媒体文件检测
    UNKNOWN = 99
};

enum class TaskState {
    IDLE = 0,
    PENDING,
    RUNNING,
    PAUSED,
    COMPLETED,
    FAILED,
    CANCELLED
};

// 使用时必须带类型前缀
TaskType type = TaskType::DETECTION_LIVESTREAM;
if (type == TaskType::DETECTION_MEDIAFILE) { ... }
```

### 详细讲解

**三大优势**:

1. **类型安全**: 不会隐式转换为 int

   ```cpp
   enum class State { ON, OFF };
   int x = State::ON;  // ❌ 编译错误
   int x = static_cast<int>(State::ON);  // ✅ 必须显式转换
   ```

2. **作用域隔离**: 枚举值不会污染外部命名空间

   ```cpp
   enum class A { Value };
   enum class B { Value };  // ✅ OK,不冲突

   // 传统 enum
   enum C { Value };
   enum D { Value };  // ❌ 编译错误,重定义!
   ```

3. **指定底层类型**: 可以控制内存占用
   ```cpp
   enum class SmallEnum : uint8_t { A, B, C };  // 只占 1 字节
   enum class BigEnum : uint64_t { X, Y, Z };   // 占 8 字节
   ```

### 面试要点

**Q1: `enum` 和 `enum class` 的区别?**

A:

1. **类型安全**: enum class 不会隐式转换为 int
2. **作用域**: enum class 需要用 `::` 访问,避免命名冲突
3. **前向声明**: enum class 可以前向声明 (如果指定底层类型)
4. **底层类型**: enum class 默认是 int,可以指定其他类型

**Q2: 什么时候用 enum,什么时候用 enum class?**

A:

- **优先使用 enum class** (现代 C++ 推荐)
- 只有在需要与 C 代码交互或需要隐式转换时才用 enum
- enum class 更安全,应该是默认选择

**Q3: 如何将 enum class 转换为 int?**

```cpp
enum class TaskType { A = 0, B = 1 };
int value = static_cast<int>(TaskType::A);  // 显式转换
```

### 易错点

```cpp
// ❌ 错误: 忘记作用域
enum class State { ON, OFF };
State s = ON;  // ❌ 编译错误

// ✅ 正确
State s = State::ON;

// ❌ 错误: 尝试隐式转换
enum class Code { SUCCESS = 0, ERROR = 1 };
if (Code::SUCCESS) { }  // ❌ 编译错误,不能当作 bool

// ✅ 正确: 显式比较
if (code == Code::SUCCESS) { }
```

---

## 2. 虚析构函数 (Virtual Destructor)

### 📌 知识点

当基类指针指向派生类对象时,如果基类析构函数不是 `virtual`,通过基类指针 delete 会导致**内存泄漏**。

### 经典例子

```cpp
// ❌ 非虚析构的问题
class Base {
public:
    ~Base() { std::cout << "~Base()" << std::endl; }
};

class Derived : public Base {
    int* data_;
public:
    Derived() : data_(new int[1000]) {}
    ~Derived() {
        delete[] data_;  // 释放资源
        std::cout << "~Derived()" << std::endl;
    }
};

Base* ptr = new Derived();  // 多态
delete ptr;
// 输出: ~Base()  ❌ 只调用了基类析构!
// data_ 没有被释放 → 内存泄漏 4000 字节!

// ✅ 虚析构的正确写法
class Base {
public:
    virtual ~Base() { std::cout << "~Base()" << std::endl; }
};

delete ptr;
// 输出: ~Derived()  ← 先调用派生类
//       ~Base()     ← 再调用基类
// ✅ 资源正确释放
```

### 项目中的例子

```cpp
// ITask.h - 任务抽象接口
class ITask {
public:
    /**
     * @brief 虚析构函数 (必须!)
     *
     * 因为 TaskManager 持有 TaskPtr (shared_ptr<ITask>),
     * 实际指向 LiveStreamTask 或 MediaFileTask 派生类对象。
     * 如果不是 virtual,派生类资源不会释放!
     */
    virtual ~ITask() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    // ...
};

// LiveStreamTask 派生类
class LiveStreamTask : public ITask {
    std::thread thread_;          // 线程资源
    std::unique_ptr<Buffer> buf_; // 内存资源

public:
    ~LiveStreamTask() override {
        // 必须被调用以释放 thread_ 和 buf_
        if (thread_.joinable()) {
            thread_.join();
        }
    }
};

// TaskManager 中使用
TaskPtr task = std::make_shared<LiveStreamTask>(config);
// task 析构时,会正确调用 ~LiveStreamTask() → ~ITask()
```

### 详细讲解

**底层原理: 虚函数表 (vtable)**

```cpp
// 虚析构函数的实现原理
class Base {
    virtual ~Base() { }
};

// 编译器生成的 vtable (简化版)
struct Base_vtable {
    void (*destructor)(Base*);  // 指向析构函数
    // ... 其他虚函数
};

Base* ptr = new Derived();
// ptr 内部有一个 vptr 指向 Derived 的 vtable
delete ptr;
// 1. 查找 vptr → Derived_vtable
// 2. 调用 Derived_vtable.destructor (动态绑定)
// 3. ~Derived() 调用完后自动调用 ~Base()
```

**为什么 = default?**

```cpp
virtual ~ITask() = default;
// 等价于空实现: virtual ~ITask() { }
// 但 = default 更明确表达意图: "我需要虚析构,但不需要额外逻辑"
```

### 面试要点

**Q1: 为什么基类析构函数必须是 virtual?**

A: 当基类指针指向派生类对象时,如果通过基类指针 delete:

- **非 virtual**: 只调用基类析构 (静态绑定) → 派生类资源泄漏
- **virtual**: 先调用派生类析构,再调用基类析构 (动态绑定) → 正确释放

这是 C++ 多态的基本要求。

**Q2: 什么时候析构函数应该是 virtual?**

A: **只要类中有 virtual 函数,析构函数就应该是 virtual**。

- 作为基类 → 必须 virtual
- 不会被继承 → 可以不 virtual (性能优化)
- 使用 `final` 关键字 → 可以不 virtual

**Q3: virtual 析构有性能开销吗?**

A: 有,但通常可以忽略:

- 每个对象多一个 vptr (8 字节,64 位系统)
- 析构时多一次虚函数调用 (间接跳转)
- 对于需要多态的类,这个开销是必须的

### 易错点

```cpp
// ❌ 错误1: 忘记虚析构
class Shape {
public:
    virtual void draw() = 0;  // 有虚函数
    ~Shape() { }              // ❌ 但析构不是 virtual!
};

// ❌ 错误2: 派生类析构不调用 override
class Circle : public Shape {
    ~Circle() { }  // ⚠️ 建议写 override 明确表达意图
};

// ✅ 正确
class Circle : public Shape {
    ~Circle() override { }  // ✅ 明确重写
};

// ❌ 错误3: 以为所有析构都要 virtual
class Vector {  // 不是基类,不需要多态
    virtual ~Vector() { }  // ❌ 浪费 (每个对象多 8 字节)
};

// ✅ 正确: 不需要继承的类
class Vector final {  // final 表示不可继承
    ~Vector() { }  // ✅ 不需要 virtual
};
```

---

## 3. 纯虚函数和抽象类

### 📌 知识点

**纯虚函数** (= 0) 强制派生类必须实现该函数。包含纯虚函数的类是**抽象类**,不能实例化。

### 经典例子

```cpp
// 抽象类: 定义接口
class IShape {
public:
    virtual ~IShape() = default;
    virtual double area() const = 0;      // 纯虚函数
    virtual double perimeter() const = 0; // 纯虚函数

    // 普通虚函数: 提供默认实现
    virtual void draw() const {
        std::cout << "Drawing shape" << std::endl;
    }
};

// ❌ 错误: 抽象类不能实例化
IShape shape;  // ❌ 编译错误: cannot instantiate abstract class

// 派生类必须实现所有纯虚函数
class Circle : public IShape {
    double radius_;
public:
    Circle(double r) : radius_(r) {}

    // ❌ 如果不实现 area(),Circle 也是抽象类!
    double area() const override {
        return 3.14159 * radius_ * radius_;
    }

    double perimeter() const override {
        return 2 * 3.14159 * radius_;
    }

    // draw() 可以不重写,使用基类默认实现
};

// ✅ Circle 实现了所有纯虚函数,可以实例化
Circle c(5.0);
IShape* ptr = &c;  // ✅ 多态
std::cout << ptr->area() << std::endl;  // 调用 Circle::area()
```

### 项目中的例子

```cpp
// ITask.h - 任务抽象接口
class ITask {
public:
    virtual ~ITask() = default;

    // ===== 纯虚函数: 强制派生类实现 =====
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;

    virtual TaskState getState() const = 0;
    virtual const TaskConfig& getConfig() const = 0;
    virtual TaskStatistics getStatistics() const = 0;

    virtual void setStateCallback(TaskCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;

protected:
    // 派生类实现的核心逻辑
    virtual void execute() = 0;

    virtual void notifyStateChanged(TaskState newState) = 0;
    virtual void notifyError(const std::string& errorMsg) = 0;
};

// LiveStreamTask 实现
class LiveStreamTask : public ITask {
public:
    // 必须实现所有纯虚函数
    bool start() override { /* ... */ }
    void stop() override { /* ... */ }
    bool pause() override { /* ... */ }
    bool resume() override { /* ... */ }

    TaskState getState() const override { return state_; }
    const TaskConfig& getConfig() const override { return config_; }
    TaskStatistics getStatistics() const override { return stats_; }

    void setStateCallback(TaskCallback callback) override {
        stateCallback_ = callback;
    }
    void setErrorCallback(ErrorCallback callback) override {
        errorCallback_ = callback;
    }

protected:
    void execute() override {
        // 直播流处理逻辑
        while (!shouldStop_) {
            cv::Mat frame = captureFrame();
            processFrame(frame);
        }
    }

    void notifyStateChanged(TaskState newState) override {
        if (stateCallback_) {
            stateCallback_(config_.taskId, newState);
        }
    }

    void notifyError(const std::string& errorMsg) override {
        if (errorCallback_) {
            errorCallback_(config_.taskId, errorMsg);
        }
    }

private:
    TaskState state_;
    TaskConfig config_;
    TaskStatistics stats_;
    TaskCallback stateCallback_;
    ErrorCallback errorCallback_;
    bool shouldStop_;
};
```

### 详细讲解

**纯虚函数 vs 普通虚函数**

| 特性       | 纯虚函数 `= 0`      | 普通虚函数      |
| ---------- | ------------------- | --------------- |
| 默认实现   | 无 (必须派生类实现) | 有 (可以不重写) |
| 类可实例化 | 否 (抽象类)         | 是              |
| 强制重写   | 是                  | 否 (可选)       |
| 使用场景   | 定义接口契约        | 提供默认行为    |

**接口 vs 抽象类**

```cpp
// 纯接口: 只有纯虚函数
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(const std::string& msg) = 0;
    virtual void error(const std::string& msg) = 0;
};

// 抽象类: 有纯虚函数 + 普通函数
class AbstractTask {
public:
    virtual ~AbstractTask() = default;
    virtual void execute() = 0;  // 纯虚

    // 普通函数: 提供通用实现
    void start() {
        std::cout << "Starting task..." << std::endl;
        execute();  // 调用纯虚函数
    }
};
```

### 面试要点

**Q1: 什么是抽象类?**

A: **包含至少一个纯虚函数的类**就是抽象类。抽象类:

- 不能实例化 (不能创建对象)
- 可以有指针和引用
- 用于定义接口,强制派生类实现

**Q2: 纯虚函数可以有实现吗?**

A: **可以**!虽然不常见,但纯虚函数可以有实现:

```cpp
class Base {
public:
    virtual void foo() = 0;  // 纯虚函数
};

// 提供实现 (在类外)
void Base::foo() {
    std::cout << "Base::foo()" << std::endl;
}

class Derived : public Base {
    void foo() override {
        Base::foo();  // 可以调用基类实现
        std::cout << "Derived::foo()" << std::endl;
    }
};
```

用途: 提供默认实现,但强制派生类必须显式重写。

**Q3: 抽象类可以有构造函数吗?**

A: **可以**!抽象类可以有构造函数,用于初始化基类成员:

```cpp
class AbstractTask {
protected:
    std::string name_;

    AbstractTask(const std::string& name) : name_(name) { }

public:
    virtual ~AbstractTask() = default;
    virtual void execute() = 0;
};

class ConcreteTask : public AbstractTask {
public:
    ConcreteTask() : AbstractTask("MyTask") { }
    void execute() override { /* ... */ }
};
```

### 易错点

```cpp
// ❌ 错误1: 忘记实现纯虚函数
class MyTask : public ITask {
    bool start() override { return true; }
    // ❌ 忘记实现 stop(), MyTask 仍然是抽象类!
};
MyTask task;  // ❌ 编译错误

// ❌ 错误2: 函数签名不匹配
class ITask {
    virtual void foo(int x) = 0;
};
class MyTask : public ITask {
    void foo(double x) override { }  // ❌ 参数类型不同,没有重写!
};
// MyTask 仍然是抽象类

// ✅ 正确: 使用 override 关键字检查
class MyTask : public ITask {
    void foo(double x) override { }  // ❌ 编译错误: 没有匹配的虚函数!
};

// ❌ 错误3: 析构函数忘记 virtual
class ITask {
    virtual void execute() = 0;
    ~ITask() { }  // ❌ 应该是 virtual!
};
```

---

## 4. std::function 回调机制

### 📌 知识点

`std::function` (C++11) 是一个**通用的函数包装器**,可以存储任何可调用对象 (函数、Lambda、Functor、成员函数)。

### 经典例子

```cpp
#include <functional>
#include <iostream>

// 1. 普通函数
void myFunc(int x) {
    std::cout << "myFunc: " << x << std::endl;
}

// 2. Lambda 表达式
auto lambda = [](int x) {
    std::cout << "Lambda: " << x << std::endl;
};

// 3. 函数对象 (Functor)
struct MyFunctor {
    void operator()(int x) {
        std::cout << "Functor: " << x << std::endl;
    }
};

// 4. 成员函数
class MyClass {
public:
    void memberFunc(int x) {
        std::cout << "Member: " << x << std::endl;
    }
};

int main() {
    // std::function 可以存储所有这些!
    std::function<void(int)> callback;

    // 绑定普通函数
    callback = myFunc;
    callback(10);  // 输出: myFunc: 10

    // 绑定 Lambda
    callback = lambda;
    callback(20);  // 输出: Lambda: 20

    // 绑定 Functor
    callback = MyFunctor();
    callback(30);  // 输出: Functor: 30

    // 绑定成员函数 (需要 std::bind)
    MyClass obj;
    callback = std::bind(&MyClass::memberFunc, &obj, std::placeholders::_1);
    callback(40);  // 输出: Member: 40

    // 最常用: Lambda 捕获外部变量
    int count = 0;
    callback = [&count](int x) {
        count += x;  // 可以修改外部变量!
        std::cout << "Count: " << count << std::endl;
    };
    callback(5);   // 输出: Count: 5
    callback(10);  // 输出: Count: 15
}
```

### 项目中的例子

```cpp
// ITask.h - 定义回调类型
using TaskCallback = std::function<void(const std::string& taskId, TaskState state)>;
using ErrorCallback = std::function<void(const std::string& taskId, const std::string& error)>;

class ITask {
public:
    virtual void setStateCallback(TaskCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
};

// LiveStreamTask.cpp - 实现回调
class LiveStreamTask : public ITask {
    TaskCallback stateCallback_;
    ErrorCallback errorCallback_;

public:
    void setStateCallback(TaskCallback callback) override {
        stateCallback_ = callback;
    }

    void setErrorCallback(ErrorCallback callback) override {
        errorCallback_ = callback;
    }

    // 状态改变时触发回调
    void changeState(TaskState newState) {
        state_ = newState;
        if (stateCallback_) {  // 检查是否设置了回调
            stateCallback_(config_.taskId, newState);
        }
    }

    // 错误发生时触发回调
    void reportError(const std::string& error) {
        if (errorCallback_) {
            errorCallback_(config_.taskId, error);
        }
    }
};

// TaskManager.cpp - 设置回调
TaskPtr TaskManager::createTask(const TaskConfig& config) {
    TaskPtr task = std::make_shared<LiveStreamTask>(config);

    // 设置状态回调 (使用 Lambda 捕获 this)
    task->setStateCallback([this](const std::string& taskId, TaskState state) {
        this->onTaskStateChanged(taskId, state);  // 调用成员函数
    });

    // 设置错误回调
    task->setErrorCallback([this](const std::string& taskId, const std::string& error) {
        LOG_ERROR("Task {} error: {}", taskId, error);
        this->onTaskError(taskId, error);
    });

    return task;
}

void TaskManager::onTaskStateChanged(const std::string& taskId, TaskState state) {
    LOG_INFO("Task {} state changed to {}", taskId, taskStateToString(state));

    // 自动清理完成或失败的任务
    if (state == TaskState::COMPLETED || state == TaskState::FAILED) {
        // 延迟清理,避免在回调中删除任务
        std::thread([this, taskId]() {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            removeTask(taskId);
        }).detach();
    }
}
```

### 详细讲解

**std::function vs 函数指针**

```cpp
// ❌ C 风格函数指针
typedef void (*Callback)(int);  // 只能指向函数,不能捕获状态

void setCallback(Callback cb) {
    cb(10);
}

// ✅ C++ std::function
void setCallback(std::function<void(int)> cb) {
    cb(10);
}

// Lambda 捕获外部变量
int count = 0;
setCallback([&count](int x) {
    count += x;  // ✅ 函数指针做不到!
});
```

**Lambda 捕获方式**

```cpp
int x = 10;
int y = 20;

// [=]: 按值捕获所有外部变量 (拷贝)
auto f1 = [=]() { return x + y; };

// [&]: 按引用捕获所有外部变量 (可修改)
auto f2 = [&]() { x++; y++; };

// [x, &y]: 按值捕获 x,按引用捕获 y
auto f3 = [x, &y]() { y = x + 1; };

// [this]: 捕获当前对象指针
class MyClass {
    int value_;
    void foo() {
        auto f = [this]() {
            value_++;  // 访问成员变量
        };
    }
};
```

### 面试要点

**Q1: std::function 的底层实现原理?**

A: **类型擦除 (Type Erasure)**:

```cpp
// 简化版实现
template<typename R, typename... Args>
class function<R(Args...)> {
    struct Concept {
        virtual R call(Args...) = 0;
        virtual ~Concept() = default;
    };

    template<typename F>
    struct Model : Concept {
        F func_;
        Model(F f) : func_(f) {}
        R call(Args... args) override {
            return func_(args...);
        }
    };

    std::unique_ptr<Concept> impl_;

public:
    template<typename F>
    function(F f) : impl_(new Model<F>(f)) {}

    R operator()(Args... args) {
        return impl_->call(args...);
    }
};
```

核心思想: 用多态隐藏具体类型。

**Q2: std::function 的性能开销?**

A:

1. **内存分配**: 通常需要堆分配 (除非对象很小,用 Small Object Optimization)
2. **虚函数调用**: 内部用虚函数实现,有间接跳转开销
3. **拷贝开销**: 拷贝 std::function 会拷贝内部对象

对于性能敏感的场景,可以考虑:

- 直接传递函数指针 (如果不需要状态)
- 使用模板参数 (编译时多态,零开销)

**Q3: std::function 可以存储成员函数吗?**

A: 可以,但需要 `std::bind`:

```cpp
class MyClass {
    void foo(int x) { }
};

MyClass obj;
std::function<void(int)> f = std::bind(&MyClass::foo, &obj, std::placeholders::_1);
f(10);  // 调用 obj.foo(10)

// 或者用 Lambda (推荐)
std::function<void(int)> f = [&obj](int x) { obj.foo(x); };
```

### 易错点

```cpp
// ❌ 错误1: 捕获局部变量的引用
std::function<void()> createCallback() {
    int x = 10;
    return [&x]() { std::cout << x; };  // ❌ x 已析构,悬空引用!
}

// ✅ 正确: 按值捕获
std::function<void()> createCallback() {
    int x = 10;
    return [x]() { std::cout << x; };  // ✅ 拷贝 x
}

// ❌ 错误2: 忘记检查 std::function 是否为空
std::function<void()> callback;
callback();  // ❌ 运行时错误: std::bad_function_call

// ✅ 正确
if (callback) {  // 检查是否有效
    callback();
}

// ❌ 错误3: Lambda 捕获 this 后对象已销毁
class MyClass {
    void setCallback(std::function<void()> cb) {
        callback_ = cb;
    }

    void foo() {
        setCallback([this]() {  // ⚠️ 捕获 this
            value_++;
        });
    }

    std::function<void()> callback_;
    int value_;
};

MyClass* obj = new MyClass();
obj->foo();
delete obj;  // ❌ obj 被销毁
callback_();  // ❌ this 悬空,崩溃!

// ✅ 正确: 用 weak_ptr
class MyClass : public std::enable_shared_from_this<MyClass> {
    void foo() {
        std::weak_ptr<MyClass> weak = shared_from_this();
        setCallback([weak]() {
            if (auto self = weak.lock()) {  // 检查对象是否存活
                self->value_++;
            }
        });
    }
};
```

---

## 5. 模板方法模式 (Template Method Pattern)

### 📌 知识点

**模板方法模式**: 在基类中定义算法的**骨架**,将某些步骤延迟到派生类中实现。

**核心思想**: "Don't call us, we'll call you" (好莱坞原则)

### 经典例子

```cpp
// 基类: 定义算法框架
class DataProcessor {
public:
    // 模板方法: 定义执行流程 (不是 virtual!)
    void processData() {
        openFile();       // 步骤1: 基类实现
        readData();       // 步骤2: 派生类实现 (纯虚)
        processCore();    // 步骤3: 派生类实现 (纯虚)
        closeFile();      // 步骤4: 基类实现
    }

protected:
    // 步骤1和4: 基类提供通用实现
    void openFile() {
        std::cout << "Opening file..." << std::endl;
    }

    void closeFile() {
        std::cout << "Closing file..." << std::endl;
    }

    // 步骤2和3: 派生类实现细节
    virtual void readData() = 0;
    virtual void processCore() = 0;
};

// CSV 处理器
class CSVProcessor : public DataProcessor {
protected:
    void readData() override {
        std::cout << "Reading CSV data" << std::endl;
    }

    void processCore() override {
        std::cout << "Processing CSV" << std::endl;
    }
};

// JSON 处理器
class JSONProcessor : public DataProcessor {
protected:
    void readData() override {
        std::cout << "Reading JSON data" << std::endl;
    }

    void processCore() override {
        std::cout << "Processing JSON" << std::endl;
    }
};

// 使用
DataProcessor* processor = new CSVProcessor();
processor->processData();
// 输出:
// Opening file...
// Reading CSV data
// Processing CSV
// Closing file...
```

### 项目中的例子

```cpp
// BaseTask.h - 基类定义算法框架 (假设我们有这个基类)
class BaseTask : public ITask {
protected:
    TaskConfig config_;
    TaskState state_;
    std::thread thread_;
    bool shouldStop_;

public:
    BaseTask(const TaskConfig& config)
        : config_(config)
        , state_(TaskState::IDLE)
        , shouldStop_(false)
    {}

    // 模板方法: start() 定义启动流程框架
    bool start() override {
        // 1. 验证配置
        if (!config_.isValid()) {
            LOG_ERROR("Invalid task config");
            return false;
        }

        // 2. 初始化资源 (派生类实现)
        if (!initResources()) {
            LOG_ERROR("Failed to init resources");
            return false;
        }

        // 3. 改变状态
        state_ = TaskState::RUNNING;
        notifyStateChanged(state_);

        // 4. 启动执行线程 (调用派生类的 execute)
        thread_ = std::thread(&BaseTask::execute, this);

        // 5. 等待线程就绪 (派生类实现)
        waitForReady();

        LOG_INFO("Task {} started", config_.taskId);
        return true;
    }

    // 模板方法: stop() 定义停止流程框架
    void stop() override {
        // 1. 设置停止标志
        shouldStop_ = true;

        // 2. 通知线程停止 (派生类实现)
        notifyStop();

        // 3. 等待线程结束
        if (thread_.joinable()) {
            thread_.join();
        }

        // 4. 释放资源 (派生类实现)
        releaseResources();

        // 5. 改变状态
        state_ = TaskState::COMPLETED;
        notifyStateChanged(state_);

        LOG_INFO("Task {} stopped", config_.taskId);
    }

protected:
    // 派生类实现的抽象步骤
    virtual bool initResources() = 0;      // 初始化资源
    virtual void waitForReady() = 0;       // 等待就绪
    virtual void execute() = 0;            // 核心执行逻辑
    virtual void notifyStop() = 0;         // 通知停止
    virtual void releaseResources() = 0;   // 释放资源
};

// LiveStreamTask - 派生类实现具体细节
class LiveStreamTask : public BaseTask {
    cv::VideoCapture capture_;
    std::condition_variable readyCV_;
    std::mutex readyMutex_;
    bool ready_;

protected:
    bool initResources() override {
        // 打开视频流
        capture_.open(config_.mediaPath);
        if (!capture_.isOpened()) {
            return false;
        }
        return true;
    }

    void waitForReady() override {
        std::unique_lock<std::mutex> lock(readyMutex_);
        readyCV_.wait(lock, [this]() { return ready_; });
    }

    void execute() override {
        // 标记就绪
        {
            std::lock_guard<std::mutex> lock(readyMutex_);
            ready_ = true;
        }
        readyCV_.notify_one();

        // 主循环: 读取帧并处理
        while (!shouldStop_) {
            cv::Mat frame;
            if (!capture_.read(frame)) {
                break;  // 读取失败,退出
            }

            // 调用 TaskService 处理帧
            taskService_->processFrame(frame, config_);

            // 控制帧率
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    }

    void notifyStop() override {
        // 唤醒可能在等待的线程
        readyCV_.notify_all();
    }

    void releaseResources() override {
        // 释放视频流
        if (capture_.isOpened()) {
            capture_.release();
        }
    }
};

// MediaFileTask - 不同的实现细节
class MediaFileTask : public BaseTask {
    std::vector<std::string> files_;
    size_t currentIndex_;

protected:
    bool initResources() override {
        // 扫描文件列表
        files_ = scanMediaFiles(config_.mediaPath);
        currentIndex_ = 0;
        return !files_.empty();
    }

    void waitForReady() override {
        // 文件任务立即就绪
    }

    void execute() override {
        // 顺序处理每个文件
        while (!shouldStop_ && currentIndex_ < files_.size()) {
            const std::string& file = files_[currentIndex_];
            cv::Mat frame = cv::imread(file);

            if (!frame.empty()) {
                taskService_->processFrame(frame, config_);
            }

            currentIndex_++;
        }
    }

    void notifyStop() override {
        // 文件任务不需要特殊通知
    }

    void releaseResources() override {
        files_.clear();
    }
};
```

### 详细讲解

**模板方法模式的结构**

```cpp
class AbstractClass {
public:
    // 模板方法 (final 防止派生类重写)
    void templateMethod() {
        primitiveOperation1();  // 抽象操作
        concreteOperation();    // 具体操作
        hook();                 // 钩子方法
        primitiveOperation2();  // 抽象操作
    }

protected:
    // 抽象操作: 派生类必须实现
    virtual void primitiveOperation1() = 0;
    virtual void primitiveOperation2() = 0;

    // 具体操作: 基类提供实现
    void concreteOperation() {
        // 通用逻辑
    }

    // 钩子方法: 派生类可以重写,也可以不重写
    virtual void hook() {
        // 默认实现 (可以为空)
    }
};
```

**与策略模式的区别**

| 特性     | 模板方法模式      | 策略模式          |
| -------- | ----------------- | ----------------- |
| 结构     | 继承              | 组合              |
| 灵活性   | 低 (编译时确定)   | 高 (运行时切换)   |
| 代码复用 | 基类复用公共代码  | 策略类独立实现    |
| 耦合度   | 紧耦合 (父子关系) | 松耦合 (接口依赖) |

### 面试要点

**Q1: 模板方法模式的优缺点?**

A:
**优点**:

1. **代码复用**: 公共逻辑在基类,避免重复
2. **控制反转**: 基类控制算法流程,派生类只实现细节
3. **符合开闭原则**: 扩展派生类,不修改基类

**缺点**:

1. **紧耦合**: 派生类依赖基类实现
2. **不够灵活**: 算法骨架固定,难以动态切换
3. **类数量增加**: 每个变体都需要一个子类

**Q2: 什么时候用模板方法模式?**

A: 当满足以下条件时:

- 算法有**固定的执行流程**,但某些步骤需要定制
- 希望**避免代码重复**,提取公共逻辑到基类
- 想要**控制子类的扩展点** (只允许在特定步骤扩展)

例如: 框架初始化流程、数据处理流程、生命周期管理

**Q3: 模板方法为什么通常不是 virtual?**

A: 因为模板方法定义了**不可变的算法骨架**,不应该被派生类重写。
如果需要修改流程,应该重写**步骤方法**,而非整个模板方法。

```cpp
// ✅ 推荐: 模板方法 final
class Base {
public:
    void templateMethod() final {  // 防止重写
        step1();
        step2();
    }
protected:
    virtual void step1() = 0;
    virtual void step2() = 0;
};

// ❌ 不推荐: 模板方法 virtual
class Base {
public:
    virtual void templateMethod() {  // 派生类可能破坏流程
        step1();
        step2();
    }
};
```

### 易错点

```cpp
// ❌ 错误1: 模板方法定义为 protected
class Base {
protected:
    void templateMethod() { }  // ❌ 外部无法调用
};

// ✅ 正确: 模板方法应该是 public
class Base {
public:
    void templateMethod() { }  // ✅ 对外接口
};

// ❌ 错误2: 抽象步骤定义为 public
class Base {
public:
    void templateMethod() { step1(); }
    virtual void step1() = 0;  // ❌ 外部可以直接调用步骤
};

// ✅ 正确: 抽象步骤应该是 protected
class Base {
public:
    void templateMethod() { step1(); }
protected:
    virtual void step1() = 0;  // ✅ 只能通过模板方法调用
};

// ❌ 错误3: 基类析构不是 virtual
class Base {
    void templateMethod() { }
    ~Base() { }  // ❌ 多态删除时有问题
};

// ✅ 正确
class Base {
    void templateMethod() { }
    virtual ~Base() { }  // ✅ 虚析构
};
```

---

**[继续下一部分: 智能指针详解...]**

由于篇幅较长,我会分批次创建文档。现在先保存这部分,您觉得怎么样?我们继续补充剩余的 5 个知识点 (智能指针、Pimpl、单例、mutable、依赖注入),还是先把改进记录更新了? 😊
