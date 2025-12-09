# 八股知识点 - JSON 解析最佳实践

**知识点**: nlohmann/json 库的安全使用方法  
**难度**: ⭐⭐⭐ (中级)  
**来源**: Day 4 任务模块开发 (2025-11-09)  
**标签**: `JSON` `nlohmann/json` `错误处理` `C++第三方库`

---

## 📌 核心概念

使用 **nlohmann/json** 库解析 JSON 时的安全写法和错误处理策略。

### 三种访问方式对比

| 方法                  | 字段存在 | 字段不存在 | 使用场景 | 安全性     |
| --------------------- | -------- | ---------- | -------- | ---------- |
| `at()`                | 返回值   | 抛出异常   | 必需字段 | ⭐⭐⭐     |
| `operator[]`          | 返回值   | 返回 null  | 动态访问 | ⭐         |
| `value(key, default)` | 返回值   | 返回默认值 | 可选字段 | ⭐⭐⭐⭐⭐ |

---

## 🎯 项目实战案例

### 场景: MQTT 任务配置解析

```cpp
// src/task/TaskTypes.cpp
TaskConfig TaskConfig::fromJson(const nlohmann::json& json) {
    TaskConfig config;

    // ✅ at(): 必需字段,缺失会抛异常
    config.taskId = std::to_string(json.at("taskID").get<int>());

    // ✅ value(): 可选字段,提供默认值
    config.algorithmId = json.value("algorithmRepoID", 0);
    config.alarmAreaScore = json.value("alarmAreaScore", 0.8f);

    // ✅ value() + 字符串转枚举
    std::string dataSource = json.value("dataSource", "live");
    config.type = (dataSource == "live")
        ? TaskType::DETECTION_LIVESTREAM
        : TaskType::DETECTION_MEDIAFILE;

    // ✅ contains() + is_array(): 检查类型再访问
    if (json.contains("type") && json["type"].is_array()) {
        for (const auto& typeObj : json["type"]) {
            EventType eventType;
            eventType.id = typeObj.value("id", 0);
            eventType.mainType = typeObj.value("main_type", 0);
            eventType.eventDescribe = typeObj.value("name", "");
            config.eventTypes.push_back(eventType);
        }
    }

    // ✅ 嵌套对象的安全访问
    if (json.contains("gps") && json["gps"].is_object()) {
        config.gpsLat = json["gps"].value("lat", 0.0);
        config.gpsLng = json["gps"].value("lng", 0.0);
    }

    return config;
}
```

**MQTT 消息示例**:

```json
{
  "taskID": 1001,
  "dataSource": "live",
  "algorithmRepoID": 42,
  "alarmAreaScore": 0.75,
  "type": [
    { "id": 1, "main_type": 101, "name": "入侵检测" },
    { "id": 2, "main_type": 102, "name": "徘徊检测" }
  ],
  "gps": {
    "lat": 39.9042,
    "lng": 116.4074
  }
}
```

---

## 📊 三种访问方式详解

### 方式 1: at() - 严格模式

```cpp
nlohmann::json json = R"({
    "id": 123,
    "name": "test"
})"_json;

// ✅ 字段存在
int id = json.at("id").get<int>();  // id = 123

// ❌ 字段不存在,抛出异常
try {
    int missing = json.at("missing").get<int>();
} catch (const nlohmann::json::out_of_range& e) {
    std::cerr << "字段不存在: " << e.what() << std::endl;
}
```

**适用场景**: 必需字段,缺失应该报错

### 方式 2: operator[] - 宽松模式

```cpp
nlohmann::json json = R"({
    "id": 123,
    "name": "test"
})"_json;

// ✅ 字段存在
int id = json["id"].get<int>();  // id = 123

// ⚠️ 字段不存在,返回null
auto val = json["missing"];  // val = null (不抛异常)

// ❌ 但是转换时会失败
int bad = json["missing"].get<int>();  // 运行时错误: null转int失败
```

**风险**: 不检查字段存在性,容易导致运行时错误

### 方式 3: value() - 带默认值 (推荐)

```cpp
nlohmann::json json = R"({
    "id": 123,
    "name": "test"
})"_json;

// ✅ 字段存在,返回实际值
int id = json.value("id", 0);  // id = 123

// ✅ 字段不存在,返回默认值
int missing = json.value("missing", -1);  // missing = -1

// ✅ 字符串默认值
std::string name = json.value("name", "unknown");  // name = "test"
std::string title = json.value("title", "unknown");  // title = "unknown"
```

**优点**: 最安全,最实用

---

## 🎓 面试要点

### 高频问题 1: nlohmann/json 的 at()和 operator[]有什么区别?

**标准答案**:

```cpp
nlohmann::json json = /* ... */;

// at(): 严格模式
json.at("id");          // 字段必须存在,否则抛异常
// 用于: 必需字段

// operator[]: 宽松模式
json["id"];             // 字段可以不存在,返回null
// 用于: 动态访问,但容易出错

// value(): 带默认值 (推荐)
json.value("id", 0);    // 字段可以不存在,返回默认值
// 用于: 可选字段
```

**项目实践**:

```cpp
// 必需字段: taskID,缺失是客户端错误,应该报错
config.taskId = std::to_string(json.at("taskID").get<int>());

// 可选字段: priority,缺失使用默认值
config.priority = json.value("priority", 0);
```

---

### 高频问题 2: 如何判断 JSON 字段的类型?

**标准答案**:

```cpp
nlohmann::json json = /* ... */;

// 1. 检查字段是否存在
if (json.contains("field")) {
    // 2. 检查字段类型
    if (json["field"].is_null())    { /* null */ }
    if (json["field"].is_boolean()) { /* bool */ }
    if (json["field"].is_number())  { /* int/float */ }
    if (json["field"].is_string())  { /* string */ }
    if (json["field"].is_array())   { /* 数组 */ }
    if (json["field"].is_object())  { /* 对象 */ }
}
```

**项目示例**:

```cpp
// 检查 eventTypes 是否为数组
if (json.contains("type") && json["type"].is_array()) {
    for (const auto& typeObj : json["type"]) {
        EventType eventType;
        eventType.id = typeObj.value("id", 0);
        config.eventTypes.push_back(eventType);
    }
}
```

---

### 高频问题 3: 如何安全地解析嵌套 JSON?

**标准答案**:

```cpp
nlohmann::json json = R"({
    "user": {
        "profile": {
            "name": "Alice",
            "age": 30
        },
        "settings": {
            "theme": "dark"
        }
    }
})"_json;

// ❌ 不安全: 不检查中间节点
std::string name = json["user"]["profile"]["name"];
// 如果 user 或 profile 不存在 → 崩溃

// ✅ 安全: 逐层检查
std::string name = "unknown";
if (json.contains("user") && json["user"].is_object()) {
    auto& user = json["user"];
    if (user.contains("profile") && user["profile"].is_object()) {
        name = user["profile"].value("name", "unknown");
    }
}

// ✅ 更简洁: 使用指针语法 (JSON Pointer)
name = json.value("/user/profile/name"_json_pointer, "unknown");
```

**项目示例**:

```cpp
// 安全解析 GPS 坐标
if (json.contains("gps") && json["gps"].is_object()) {
    config.gpsLat = json["gps"].value("lat", 0.0);
    config.gpsLng = json["gps"].value("lng", 0.0);
}
```

---

### 高频问题 4: 如何处理 JSON 解析异常?

**标准答案**:

```cpp
// 方案1: try-catch 捕获异常
TaskConfig parseConfig(const std::string& jsonStr) {
    try {
        nlohmann::json json = nlohmann::json::parse(jsonStr);
        return TaskConfig::fromJson(json);

    } catch (const nlohmann::json::parse_error& e) {
        // JSON格式错误
        Logger::error("JSON解析失败: {}", e.what());
        throw std::runtime_error("无效的JSON格式");

    } catch (const nlohmann::json::type_error& e) {
        // 类型转换错误
        Logger::error("JSON类型错误: {}", e.what());
        throw std::runtime_error("JSON字段类型不匹配");

    } catch (const nlohmann::json::out_of_range& e) {
        // 字段不存在
        Logger::error("缺少必需字段: {}", e.what());
        throw std::runtime_error("JSON缺少必需字段");
    }
}

// 方案2: 使用 value() 避免异常
TaskConfig parseConfigSafe(const nlohmann::json& json) {
    TaskConfig config;

    // 必需字段使用默认值代替异常
    config.taskId = std::to_string(json.value("taskID", -1));
    if (config.taskId == "-1") {
        Logger::warn("缺少 taskID,使用默认值");
    }

    // 可选字段正常使用 value()
    config.algorithmId = json.value("algorithmRepoID", 0);

    return config;
}
```

---

## 💡 实战技巧

### 技巧 1: 定义 JSON Schema 验证

```cpp
bool validateTaskConfig(const nlohmann::json& json) {
    // 检查必需字段
    if (!json.contains("taskID") || !json["taskID"].is_number()) {
        Logger::error("缺少或错误的 taskID 字段");
        return false;
    }

    if (!json.contains("dataSource") || !json["dataSource"].is_string()) {
        Logger::error("缺少或错误的 dataSource 字段");
        return false;
    }

    // 检查可选字段的类型
    if (json.contains("type") && !json["type"].is_array()) {
        Logger::error("type 字段必须是数组");
        return false;
    }

    return true;
}

// 使用
TaskConfig TaskConfig::fromJson(const nlohmann::json& json) {
    if (!validateTaskConfig(json)) {
        throw std::invalid_argument("JSON格式验证失败");
    }

    // ... 安全解析 ...
}
```

### 技巧 2: 使用结构化绑定 (C++17)

```cpp
// C++17 结构化绑定简化代码
for (const auto& [key, value] : json.items()) {
    if (value.is_string()) {
        std::cout << key << ": " << value.get<std::string>() << std::endl;
    }
}
```

### 技巧 3: 自定义类型转换

```cpp
// 定义 from_json 和 to_json
struct EventType {
    int id;
    int mainType;
    std::string eventDescribe;
};

// 自定义转换函数
void from_json(const nlohmann::json& j, EventType& e) {
    e.id = j.value("id", 0);
    e.mainType = j.value("main_type", 0);
    e.eventDescribe = j.value("name", "");
}

void to_json(nlohmann::json& j, const EventType& e) {
    j = nlohmann::json{
        {"id", e.id},
        {"main_type", e.mainType},
        {"name", e.eventDescribe}
    };
}

// 使用时自动转换
EventType et = json.get<EventType>();  // from_json
nlohmann::json j = et;                  // to_json
```

### 技巧 4: JSON Pointer 快速访问

```cpp
nlohmann::json json = R"({
    "config": {
        "server": {
            "host": "192.168.1.1",
            "port": 8080
        }
    }
})"_json;

// 使用 JSON Pointer 直接访问深层字段
std::string host = json.value("/config/server/host"_json_pointer, "localhost");
int port = json.value("/config/server/port"_json_pointer, 80);
```

---

## 🔗 相关知识点

- **静态工厂方法**: fromJson() 是静态工厂方法的典型应用
- **异常处理**: JSON 解析需要合理的异常处理策略
- **RAII**: JSON 对象遵循 RAII 原则
- **C++17 特性**: 结构化绑定、if 初始化语句

---

## 📝 总结

### 核心要点

1. ✅ 必需字段用 `at()`,可选字段用 `value()`
2. ✅ 访问前先用 `contains()` 和 `is_xxx()` 检查
3. ✅ 嵌套对象逐层检查,或使用 JSON Pointer
4. ✅ 异常处理要区分解析错误、类型错误、字段缺失

### 最佳实践

```cpp
// ✅ 推荐的解析模式
TaskConfig fromJson(const nlohmann::json& json) {
    TaskConfig config;

    // 1. 必需字段: at() + try-catch
    try {
        config.id = json.at("id").get<int>();
    } catch (const nlohmann::json::exception& e) {
        throw std::invalid_argument("缺少必需字段: id");
    }

    // 2. 可选字段: value()
    config.name = json.value("name", "");

    // 3. 复杂类型: contains() + is_xxx()
    if (json.contains("tags") && json["tags"].is_array()) {
        for (const auto& tag : json["tags"]) {
            config.tags.push_back(tag.get<std::string>());
        }
    }

    return config;
}
```

### 面试答题模板

**Q: 如何安全地解析 JSON?**

**A**: 使用 nlohmann/json 库时,应该根据字段类型选择合适的访问方式:

1. **必需字段**: 使用 `at()`,缺失时抛异常

   ```cpp
   config.id = json.at("id").get<int>();
   ```

2. **可选字段**: 使用 `value()`,提供默认值

   ```cpp
   config.name = json.value("name", "");
   ```

3. **复杂类型**: 先用 `contains()` 和 `is_xxx()` 检查
   ```cpp
   if (json.contains("tags") && json["tags"].is_array()) {
       // 安全访问数组
   }
   ```

**项目应用**: 在 DJI ESDK 项目中,MQTT 消息使用 JSON 格式。`TaskConfig::fromJson()` 使用 `at()` 解析必需字段 `taskID`,使用 `value()` 解析可选字段 `algorithmRepoID`,确保解析的健壮性。

---

**最后更新**: 2025-11-09  
**下次复习**: 建议 1 周后
