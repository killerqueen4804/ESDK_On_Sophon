# Device 模块设计文档

## 📋 模块概述

Device 模块负责与 DJI 无人机设备通信,管理设备连接、控制云台、获取视频流等功能。

### 核心职责

1. **设备连接管理** - 连接/断开 DJI 设备,维护连接状态
2. **视频流获取** - 获取 H.264 视频流和 RGB 图像流
3. **云台控制** - 控制云台俯仰角和偏航角
4. **状态监控** - 实时监控设备状态,通知观察者
5. **配置驱动** - 从 Config 读取设备参数

---

## 🏗️ 架构设计

### 类图

```
┌─────────────────────────────────────────────────────────────┐
│                    IDeviceObserver                          │
│  ────────────────────────────────────────────────────────   │
│  + onDeviceConnected(deviceInfo: DeviceInfo): void          │
│  + onDeviceDisconnected(reason: string): void               │
│  + onCameraStreamReady(format: StreamFormat): void          │
│  + onGimbalAngleChanged(pitch, yaw: float): void            │
│  + onDeviceError(errorCode: int, msg: string): void         │
└─────────────────────────────────────────────────────────────┘
                               ▲
                               │ implements
                               │
                    ┌──────────┴──────────┐
                    │                     │
         ┌──────────────────┐   ┌──────────────────┐
         │  VisionModule    │   │   MqttModule     │
         │  (观察者示例)     │   │   (观察者示例)    │
         └──────────────────┘   └──────────────────┘


┌─────────────────────────────────────────────────────────────┐
│                     DeviceManager                            │
│  ────────────────────────────────────────────────────────   │
│  - pImpl_: unique_ptr<Impl>                    (Pimpl模式)  │
│  ────────────────────────────────────────────────────────   │
│  + getInstance(): DeviceManager&                 (单例模式)  │
│  + initialize(): bool                                        │
│  + connect(config: DeviceConfig): bool                       │
│  + disconnect(): void                                        │
│  + isConnected(): bool                                       │
│  ────────────────────────────────────────────────────────   │
│  + enableH264Stream(): bool                                  │
│  + enableRGBStream(): bool                                   │
│  + disableAllStreams(): void                                 │
│  ────────────────────────────────────────────────────────   │
│  + controlGimbal(pitch, yaw: float): bool                    │
│  + getGimbalAngle(): GimbalAngle                             │
│  ────────────────────────────────────────────────────────   │
│  + addObserver(observer: IDeviceObserver*): void             │
│  + removeObserver(observer: IDeviceObserver*): void          │
└─────────────────────────────────────────────────────────────┘
                               │
                               │ uses
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                 DeviceManager::Impl                          │
│  ────────────────────────────────────────────────────────   │
│  - esdkManager_: unique_ptr<EsdkWrapper>   (封装ESDK API)   │
│  - observers_: vector<IDeviceObserver*>                      │
│  - observersMutex_: mutex                                    │
│  - connected_: atomic<bool>                                  │
│  - logger_: Logger&                                          │
│  - config_: Config&                                          │
│  ────────────────────────────────────────────────────────   │
│  - onEsdkConnectionChanged(connected: bool): void            │
│  - onEsdkStreamReady(stream: StreamInfo): void               │
│  - notifyConnected(info: DeviceInfo): void                   │
│  - notifyDisconnected(reason: string): void                  │
└─────────────────────────────────────────────────────────────┘
```
