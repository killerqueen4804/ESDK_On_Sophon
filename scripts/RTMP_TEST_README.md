# 📌 RTMP 推流测试工具集

适用于 SE7 嵌入式设备的 RTMP 推流测试脚本（基于 GStreamer）。

## 📁 脚本列表

### 1. `check_gstreamer.sh` - 环境诊断

检查 GStreamer 安装、插件和 RTMP 支持。

**使用方法**:

```bash
bash check_gstreamer.sh
```

**输出内容**:

- GStreamer 版本
- 已安装工具列表
- RTMP 关键插件状态
- 插件路径和数量

**使用场景**: 首次运行时检查环境是否正确配置。

---

### 2. `quick_rtmp_test.sh` - 快速测试

快速验证 RTMP 流是否可用（10 秒测试）。

**使用方法**:

```bash
bash quick_rtmp_test.sh "rtmp://27.223.85.130:3519/live/u25IGQMHg?sign=9hcIMwaNR"
```

**测试内容**:

- ✅ 网络连通性检查（TCP 连接）
- ✅ GStreamer 连接测试（10 秒）

**优点**: 快速、无需额外工具、适合自动化测试。

---

### 3. `test_rtmp_gst.sh` - 完整测试

使用 GStreamer 工具进行完整的 RTMP 流测试。

**使用方法**:

```bash
bash test_rtmp_gst.sh "rtmp://27.223.85.130:3519/live/u25IGQMHg?sign=9hcIMwaNR"
```

**测试内容**:

- ✅ gst-discoverer 分析流
- ✅ GStreamer 管道连接测试
- ✅ playbin 播放测试（如果有显示设备）

**优点**: 详细、全面、提供调试建议。

---

### 4. `test_rtmp_stream.sh` - FFmpeg 版本（参考）

使用 FFmpeg/VLC 测试 RTMP 流（需要安装额外工具）。

**使用方法**:

```bash
bash test_rtmp_stream.sh "rtmp://27.223.85.130:3519/live/u25IGQMHg?sign=9hcIMwaNR"
```

**注意**: SE7 设备上可能未安装 FFmpeg/VLC，推荐使用上面的 GStreamer 版本。

---

## 🚀 快速上手

### 步骤 1: 检查环境

```bash
cd /data/Edge-SDK/build/bin
bash check_gstreamer.sh
```

**期望输出**:

```
✅ gst-launch-1.0 已安装
✅ rtmpsink
✅ h264parse
✅ flvmux
```

---

### 步骤 2: 启动推流程序

```bash
sudo ./ESDK_SOPHON
```

**等待日志出现**:

```
[INFO] StartH264Stream 成功
[INFO] 🎉 首帧 H.264 数据已到达
[INFO] RTMP 推流中, 已推送 30 帧
```

---

### 步骤 3: 测试推流

**在另一个终端运行**:

```bash
# 快速测试（推荐）
bash quick_rtmp_test.sh "rtmp://27.223.85.130:3519/live/u25IGQMHg?sign=9hcIMwaNR"

# 或完整测试
bash test_rtmp_gst.sh "rtmp://27.223.85.130:3519/live/u25IGQMHg?sign=9hcIMwaNR"
```

---

## 🔧 常见问题

### ❌ 网络不通

**现象**:

```
❌ 网络不通（服务器未运行或端口被阻止）
```

**解决方法**:

```bash
# 检查服务器是否可达
ping 27.223.85.130

# 检查端口是否开放
telnet 27.223.85.130 3519
```

---

### ❌ GStreamer 连接失败

**现象**:

```
WARNING: erroneous pipeline: no element "rtmpsrc"
```

**解决方法**:

```bash
# 检查插件是否安装
gst-inspect-1.0 rtmpsrc

# 如果缺失，安装 GStreamer 插件
sudo apt-get install gstreamer1.0-plugins-bad
```

---

### ❌ RTMP URL 错误或过期

**现象**:

```
Could not connect to RTMP stream
```

**解决方法**:

1. 检查 URL 是否正确（包括 `sign` 参数）
2. 检查 URL 是否过期（某些 RTMP 服务器使用时间戳签名）
3. 检查 RTMP 服务器日志（如有权限）

---

## 📚 调试技巧

### 1. 查看推流端日志

```bash
# 实时查看日志
tail -f logs/esdk_sophon_*.log

# 搜索错误信息
grep -i "error\|warning\|rtmp" logs/esdk_sophon_*.log
```

---

### 2. 启用 GStreamer 调试日志

```bash
# 在推流端启动时设置环境变量
GST_DEBUG=3 sudo ./ESDK_SOPHON
```

**调试级别**:

- `0`: 无日志
- `1`: 错误 (ERROR)
- `2`: 警告 (WARNING)
- `3`: 信息 (INFO) - **推荐**
- `4`: 调试 (DEBUG)
- `5`: 日志 (LOG)
- `6`: 跟踪 (TRACE)

---

### 3. 检查推流端是否在运行

```bash
ps aux | grep ESDK_SOPHON
```

---

### 4. 抓包分析 RTMP 握手

```bash
# 安装 tcpdump（如果未安装）
sudo apt-get install tcpdump

# 抓取 RTMP 流量
sudo tcpdump -i any -nn port 3519 -w rtmp.pcap

# 使用 Wireshark 分析 rtmp.pcap
```

---

## 🎯 成功标志

如果推流成功，应该看到:

**推流端日志**:

```
[INFO] RTMP 推流中, 已推送 30 帧
[INFO] RTMP 推流中, 已推送 60 帧
[INFO] RTMP 推流中, 已推送 90 帧
```

**测试脚本输出**:

```
✅ 网络连通
✅ RTMP 流连接成功!
```

**GStreamer 管道输出**:

```
Setting pipeline to PLAYING ...
New clock: GstSystemClock
(持续输出，无错误信息)
```

---

## 📞 技术支持

如果所有方法都失败，请收集以下信息:

1. **推流端日志**:

   ```bash
   tail -100 logs/esdk_sophon_*.log
   ```

2. **GStreamer 诊断**:

   ```bash
   bash check_gstreamer.sh
   ```

3. **网络测试**:

   ```bash
   ping 27.223.85.130
   telnet 27.223.85.130 3519
   ```

4. **完整测试输出**:
   ```bash
   bash test_rtmp_gst.sh "rtmp://..." > test_output.log 2>&1
   ```

将以上信息提供给技术支持团队。

---

**创建日期**: 2025-11-17  
**适用设备**: SE7 (ARM64)  
**GStreamer 版本**: 1.16.3+
