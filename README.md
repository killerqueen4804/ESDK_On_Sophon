# CMake项目模板

## 1. 环境及第三方库
1. qt安装
```bash
   sudo apt-get install qt5-default qtcreator -y
```

## 2. 编译
```bash
cd build
cmake ..
make -j8
```

## 3. 运行
```bash
./bin/hello
```

## 4.目录结构
```
.
├── CMakeLists.txt
├── build                     // 编译目录
|   └── bin                   // 可执行文件目录
├── src
│   ├── main.cpp              // 主程序入口
│   └── LogicControl          // 逻辑控制器实现
│       ├── LogicControl.cpp
│       └── LogicControl.h
│   └── Mqtt                  // Mqtt实现
│       ├── MqttClient.cpp
│       └── MqttClient.h
│   └── Go2Control            // Go2控制实现
│       ├── Go2Control.cpp
│       └── Go2Control.h
├── 3rdparty
│   ├── include               // 第三方库头文件
│   └── lib                   // 第三方库文件
└── README.md                 // 项目说明文件
```

