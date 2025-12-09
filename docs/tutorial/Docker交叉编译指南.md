# Docker 交叉编译与部署指南

> 📦 本项目采用 Docker 容器进行交叉编译，在 SE7 微服务器上运行

---

## 🏗️ 编译环境

### 开发环境

- **主机**: Windows x86_64
- **项目路径**: `F:\CV\DJI\ESDK_On_Sophon\ESDK_On_Sophon`
- **Docker 容器**: `stream_lzy`
- **容器挂载**: `/workspace` ↔️ `F:\CV\DJI\ESDK_On_Sophon\ESDK_On_Sophon`
- **工具链**: ARM64 GCC 交叉编译器
- **CMake 版本**: 3.15+

### 目标环境

- **设备**: 算能 SE7 微服务器
- **芯片**: Sophon BM1684X
- **架构**: ARM64 (aarch64)
- **操作系统**: Linux

---

## 📋 编译流程

### 1. 进入 Docker 容器

```bash
# 启动Docker容器（如果未启动）
docker start stream_lzy

# 进入容器
docker exec -it stream_lzy /bin/bash
```

### 2. 进入项目目录

```bash
# 容器内项目路径（已挂载）
cd /workspace

# 验证目录挂载
pwd  # 应该显示: /workspace
ls   # 应该看到: CMakeLists.txt, src/, third_party/ 等
```

### 3. 创建构建目录

```bash
# 创建并进入build目录
mkdir -p build
cd build
```

### 4. 配置 CMake（交叉编译）

```bash
# ✅ 简化版本（推荐）- 交叉编译配置已内置在CMakeLists.txt中
cmake ..

# 或者指定构建类型
cmake .. -DCMAKE_BUILD_TYPE=Release  # Release模式（默认）
cmake .. -DCMAKE_BUILD_TYPE=Debug    # Debug模式

# ❌ 不再需要指定toolchain文件
# cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake
```

**说明**：

- 交叉编译配置已直接写在`CMakeLists.txt`中
- 与旧项目保持一致，简化操作
- 不需要记住额外的 CMake 参数

### 5. 编译

```bash
# 使用多线程编译（加快速度）
cmake --build . --parallel 8

# 或者使用make
make -j8
```

### 6. 检查生成的文件

```bash
# 查看可执行文件
ls -lh bin/ESDK_Sophon

# 验证是否为ARM64架构
file bin/ESDK_Sophon
# 输出应该包含：ELF 64-bit LSB executable, ARM aarch64
```

---

## 🚀 部署到 SE7

### 方法 1: SCP 传输

```bash
# 从Docker容器复制到SE7
scp bin/ESDK_Sophon user@se7:/path/to/deploy/

# 复制配置文件
scp -r ../config user@se7:/path/to/deploy/

# 复制依赖库（如果需要）
scp -r lib/*.so user@se7:/path/to/deploy/lib/
```

### 方法 2: 共享目录

如果 Docker 容器和 SE7 共享网络存储：

```bash
# 复制到共享目录
cp bin/ESDK_Sophon /shared/path/
cp -r ../config /shared/path/
```

### 方法 3: rsync 同步

```bash
# 同步整个部署目录
rsync -avz --progress \
    bin/ config/ lib/ \
    user@se7:/path/to/deploy/
```

---

## 🔧 SE7 上运行

### 1. 登录 SE7

```bash
ssh user@se7
```

### 2. 设置环境变量

```bash
# 设置库路径
export LD_LIBRARY_PATH=/path/to/deploy/lib:$LD_LIBRARY_PATH

# 或者添加到 ~/.bashrc
echo 'export LD_LIBRARY_PATH=/path/to/deploy/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

### 3. 运行程序

```bash
cd /path/to/deploy
./ESDK_Sophon
```

### 4. 后台运行

```bash
# 使用nohup后台运行
nohup ./ESDK_Sophon > output.log 2>&1 &

# 查看进程
ps aux | grep ESDK_Sophon

# 查看日志
tail -f output.log
```

---

## 🐛 调试技巧

### 在 SE7 上调试

```bash
# 检查依赖库是否齐全
ldd ./ESDK_Sophon

# 如果有缺失库，显示为：
#   libXXX.so => not found

# 使用gdb调试
gdb ./ESDK_Sophon
(gdb) run
(gdb) backtrace  # 查看崩溃堆栈
```

### 在 Docker 容器中测试

虽然是交叉编译，但可以用 QEMU 模拟 ARM64 环境：

```bash
# 安装QEMU（在Docker容器中）
apt-get install qemu-user-static

# 运行ARM64程序
qemu-aarch64-static ./bin/ESDK_Sophon
```

---

## 📦 依赖库管理

### 第三方库的交叉编译版本

确保`third_party/`目录下的库都是**ARM64 版本**：

```bash
# 检查库的架构
file third_party/opencv4.2/lib/libopencv_core.so
# 应该输出：ELF 64-bit LSB shared object, ARM aarch64

# 如果是x86_64，需要重新获取ARM64版本
```

### 静态链接 vs 动态链接

**本项目策略**：

- **模块间**: 静态链接（libcore.a, libmqtt.a 等）
- **第三方库**: 根据需要选择
  - 如果 SE7 已有：动态链接（减小可执行文件大小）
  - 如果 SE7 没有：静态链接（便于部署）

```cmake
# CMakeLists.txt中设置静态链接
target_link_libraries(ESDK_Sophon
    PRIVATE
        core        # 静态库
        mqtt        # 静态库
        -static-libgcc      # 静态链接gcc库
        -static-libstdc++   # 静态链接stdc++库
)
```

---

## 🔍 常见问题

### Q1: 编译时找不到头文件

**原因**: 交叉编译环境中缺少 ARM64 版本的头文件

**解决**:

```bash
# 确保toolchain文件中设置了正确的include路径
# 或者在CMakeLists.txt中添加
include_directories(/path/to/arm64/include)
```

### Q2: 运行时找不到.so 库

**原因**: SE7 上缺少依赖的动态库

**解决**:

```bash
# 方法1: 复制缺失的库到SE7
scp missing.so user@se7:/usr/local/lib/

# 方法2: 设置LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/custom/lib:$LD_LIBRARY_PATH

# 方法3: 静态链接该库（修改CMakeLists.txt）
```

### Q3: 程序运行时 Segmentation Fault

**原因**: 可能的原因

- 库版本不匹配（编译时和运行时的库不一致）
- 内存访问错误
- 未初始化的指针

**调试**:

```bash
# 使用gdb调试
gdb ./ESDK_Sophon
(gdb) run
(gdb) backtrace

# 使用valgrind检查内存问题（如果SE7上有）
valgrind --leak-check=full ./ESDK_Sophon
```

### Q4: 性能问题

**原因**: ARM64 和 x86 架构差异

**优化**:

```cmake
# 在toolchain文件中添加ARM64优化选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a -mtune=cortex-a72")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
```

---

## 📊 完整工作流程图

```
┌─────────────┐
│  开发主机    │
│  (Windows)  │
└──────┬──────┘
       │ docker exec
       ▼
┌─────────────────────────┐
│  Docker容器             │
│  stream_lzy             │
│  ┌──────────────────┐  │
│  │ ARM64工具链      │  │
│  │ cmake + gcc      │  │
│  └──────────────────┘  │
│         │              │
│         ▼              │
│  ┌──────────────────┐  │
│  │ 交叉编译         │  │
│  │ ARM64可执行文件  │  │
│  └──────────────────┘  │
└─────────┬───────────────┘
          │ scp/rsync
          ▼
    ┌─────────────┐
    │  SE7微服务器 │
    │  (ARM64)    │
    │  Sophon芯片 │
    └─────────────┘
```

---

## 🎓 面试要点

### 交叉编译相关

**Q: 什么是交叉编译？为什么需要？**

A: 交叉编译是在一个平台（如 x86）上编译另一个平台（如 ARM）的程序。
需要交叉编译的原因：

1.  嵌入式设备资源有限，编译慢或无法编译
2.  开发机性能强，编译速度快
3.  统一开发环境，便于团队协作

**Q: 交叉编译有哪些注意事项？**

A: 1. 使用正确的工具链（目标平台的编译器） 2. 确保链接目标平台的库，而不是主机的库 3. 头文件和库的版本要与目标平台一致 4. 注意字节序、指针大小等架构差异

**Q: 如何验证交叉编译结果？**

A: 1. 使用`file`命令查看可执行文件架构 2. 使用`ldd`命令查看依赖库 3. 在目标设备上运行测试 4. 使用 QEMU 模拟器在开发机上初步测试

---

## 📝 快速命令参考

```bash
# === Docker环境 ===
docker start stream_lzy && docker exec -it stream_lzy /bin/bash

# === 编译（简化版）===
cd /workspace
mkdir -p build && cd build
cmake ..                          # ✅ 就这么简单！
cmake --build . --parallel 8

# === 或者使用便捷脚本 ===
cd /workspace
bash scripts/build_in_docker.sh

# === 部署 ===
scp bin/ESDK_Sophon user@se7:/opt/esdk/
scp -r ../config user@se7:/opt/esdk/

# === SE7运行 ===
ssh user@se7
cd /opt/esdk
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
./ESDK_Sophon
```

---

**创建日期**: 2025-10-25  
**适用环境**: Docker 容器 stream_lzy → SE7 微服务器
