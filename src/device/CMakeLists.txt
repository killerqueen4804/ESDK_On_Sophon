# ============================================================================
# src/device/CMakeLists.txt - 设备控制模块
# ============================================================================
# 功能：无人机设备控制、命令发送、状态管理
# 依赖：core、mqtt、ESDK（DJI SDK）
# ============================================================================

# ----------------------------------------------------------------------------
# 1. 定义源文件列表
# ----------------------------------------------------------------------------
set(DEVICE_SOURCES
    DroneController.cpp
    CommandHandler.cpp
)

# ----------------------------------------------------------------------------
# 2. 创建静态库
# ----------------------------------------------------------------------------
add_library(device STATIC
    ${DEVICE_SOURCES}
)

# ----------------------------------------------------------------------------
# 3. 设置头文件包含路径
# ----------------------------------------------------------------------------
target_include_directories(device
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ----------------------------------------------------------------------------
# 4. 链接依赖库
# ----------------------------------------------------------------------------
target_link_libraries(device
    PUBLIC
        core                       # 日志、配置
    PRIVATE
        mqtt                       # MQTT通信（接收控制命令）
        ThirdParty::ESDK          # DJI设备SDK
)

# ----------------------------------------------------------------------------
# 5. 设置编译选项
# ----------------------------------------------------------------------------
target_compile_options(device
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ----------------------------------------------------------------------------
# 6. 设置库输出路径
# ----------------------------------------------------------------------------
set_target_properties(device PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# ============================================================================
# 📚 知识点：为什么mqtt用PRIVATE而不是PUBLIC？
# ============================================================================
#
# 依赖传递分析：
#
# device模块的角色：
#   - 接收MQTT消息（订阅控制命令）
#   - 控制无人机设备（调用ESDK API）
#   - 上报设备状态（发布MQTT消息）
#
# mqtt用PRIVATE的理由：
#
# 1. 接口中没有MQTT类型：
#    ```cpp
#    // DroneController.h（公共接口）
#    class DroneController {
#    public:
#        void takeOff();
#        void land();
#        void moveTo(double x, double y, double z);
#        // 没有MqttClient的类型！
#    
#    private:
#        void onCommandReceived(const std::string& cmd);  // 回调
#        MqttClient* mqttClient_;  // 内部使用
#    };
#    ```
#
# 2. 实现细节：
#    ```cpp
#    // DroneController.cpp
#    void DroneController::initialize() {
#        mqttClient_ = &MqttClient::getInstance();
#        mqttClient_->subscribe("drone/command");
#        mqttClient_->registerCallback("drone/command", 
#            [this](const std::string& msg) {
#                this->onCommandReceived(msg);
#            });
#    }
#    ```
#    MQTT只在实现中使用，不在接口中暴露
#
# 3. 依赖隔离：
#    - main.cpp链接device，不需要知道device内部用了MQTT
#    - 降低耦合度
#
# 对比：如果mqtt用PUBLIC
#   ```cpp
#   // 不推荐的设计（mqtt暴露在接口中）
#   class DroneController {
#   public:
#       void setMqttClient(MqttClient* client);  // MQTT类型出现
#       MqttClient* getMqttClient();
#   };
#   ```
#   这种情况必须用PUBLIC，但这是不好的设计
#
# 面试问题：
# Q: 如何判断依赖应该用PUBLIC还是PRIVATE？
# A: 看公共头文件（include/）中是否出现依赖库的类型
#    - 出现 → PUBLIC（类型传递）
#    - 不出现 → PRIVATE（实现细节）
#
# 口诀：接口暴露用PUBLIC，实现隐藏用PRIVATE
# ============================================================================

# ============================================================================
# 📚 知识点：命令模式（Command Pattern）
# ============================================================================
#
# CommandHandler的设计：
#
# 命令模式的作用：
# - 将请求封装成对象
# - 支持撤销、重做
# - 支持命令队列
# - 解耦命令发送者和执行者
#
# 代码示例：
#   ```cpp
#   // CommandHandler.h（内部头文件）
#   class ICommand {
#   public:
#       virtual ~ICommand() = default;
#       virtual void execute() = 0;
#       virtual void undo() = 0;  // 支持撤销
#   };
#
#   class TakeOffCommand : public ICommand {
#   public:
#       explicit TakeOffCommand(Drone* drone) : drone_(drone) {}
#       void execute() override { drone_->takeOff(); }
#       void undo() override { drone_->land(); }
#   private:
#       Drone* drone_;
#   };
#
#   class CommandHandler {
#   public:
#       void addCommand(std::unique_ptr<ICommand> cmd);
#       void executeAll();
#       void undoLast();
#   private:
#       std::queue<std::unique_ptr<ICommand>> commandQueue_;
#       std::stack<std::unique_ptr<ICommand>> history_;
#   };
#   ```
#
# 使用场景：
#   ```cpp
#   // 接收MQTT命令
#   void DroneController::onCommandReceived(const std::string& cmd) {
#       if (cmd == "takeoff") {
#           auto command = std::make_unique<TakeOffCommand>(drone_);
#           commandHandler_.addCommand(std::move(command));
#       }
#   }
#
#   // 执行命令
#   commandHandler_.executeAll();
#
#   // 撤销上一个命令
#   commandHandler_.undoLast();
#   ```
#
# 优势：
# 1. 解耦：命令发送和执行分离
# 2. 可扩展：添加新命令不修改现有代码
# 3. 可撤销：支持undo操作
# 4. 可序列化：命令可以保存到文件
# 5. 可组合：多个命令可以组合成宏命令
#
# 面试问题：
# Q: 命令模式和策略模式有什么区别？
# A: - 命令模式：封装"动作"（do something），强调执行和撤销
#    - 策略模式：封装"算法"（how to do），强调算法可替换
#    - 命令模式通常有execute()和undo()
#    - 策略模式通常只有algorithm()
# ============================================================================
