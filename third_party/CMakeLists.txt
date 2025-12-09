# ==============================================================================
# 第三方库管理 CMakeLists.txt
# ==============================================================================
#
# 本文件负责配置所有第三方库，为项目提供统一的依赖管理。
# 
# 设计原则：
# 1. 每个库创建一个 IMPORTED 目标（如 ThirdParty::OpenCV）
# 2. 使用 INTERFACE_* 属性自动传递头文件路径和编译选项
# 3. 主项目只需 target_link_libraries 即可自动配置
#
# 作者: Your Name
# 日期: 2025-10-23
# ==============================================================================

message(STATUS "========================================")
message(STATUS "配置第三方库...")
message(STATUS "========================================")

# 第三方库根目录
set(THIRD_PARTY_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# ==============================================================================
# 1. DJI ESDK (核心依赖)
# ==============================================================================
message(STATUS "配置 DJI ESDK...")

# 创建 IMPORTED 目标（STATIC 表示静态库）
add_library(ThirdParty::ESDK STATIC IMPORTED GLOBAL)

# 设置库文件位置
set_target_properties(ThirdParty::ESDK PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/esdk/lib/aarch64/libedgesdk.a"
    # 设置接口包含目录（使用这个库的目标会自动获得这个路径）
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/esdk/include"
)

message(STATUS "  ✓ ESDK 已配置")

# ==============================================================================
# 2. OpenCV 4.2
# ==============================================================================
message(STATUS "配置 OpenCV 4.2...")

# OpenCV 通常有多个库，我们创建一个主目标
add_library(ThirdParty::OpenCV INTERFACE IMPORTED GLOBAL)

# 设置包含目录
set_target_properties(ThirdParty::OpenCV PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/opencv4.2/include/opencv4"
)

# OpenCV 的多个库文件
set(OPENCV_LIBS
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    opencv_videoio
    opencv_highgui
    opencv_video
)

# 为每个 OpenCV 库创建 IMPORTED 目标
foreach(lib ${OPENCV_LIBS})
    add_library(ThirdParty::${lib} SHARED IMPORTED GLOBAL)
    set_target_properties(ThirdParty::${lib} PROPERTIES
        IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/opencv4.2/lib/lib${lib}.so"
    )
    # 将各个库链接到主 OpenCV 目标
    target_link_libraries(ThirdParty::OpenCV INTERFACE ThirdParty::${lib})
endforeach()

message(STATUS "  ✓ OpenCV 已配置 (${OPENCV_LIBS})")

# ==============================================================================
# 3. Qt5 (Core, Widgets, Network, Mqtt)
# ==============================================================================
message(STATUS "配置 Qt5...")

# Qt Core
add_library(ThirdParty::Qt5Core SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::Qt5Core PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/qt/lib/libQt5Core.so.5.14.0"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/qt/include;${THIRD_PARTY_ROOT}/qt/include/QtCore"
)

# Qt Widgets
add_library(ThirdParty::Qt5Widgets SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::Qt5Widgets PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/qt/lib/libQt5Widgets.so.5.14.0"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/qt/include/QtWidgets"
)
target_link_libraries(ThirdParty::Qt5Widgets INTERFACE ThirdParty::Qt5Core)

# Qt Network
add_library(ThirdParty::Qt5Network SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::Qt5Network PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/qt/lib/libQt5Network.so.5.14.0"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/qt/include/QtNetwork"
)
target_link_libraries(ThirdParty::Qt5Network INTERFACE ThirdParty::Qt5Core)

# Qt Mqtt
add_library(ThirdParty::Qt5Mqtt SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::Qt5Mqtt PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/qt/lib/libQt5Mqtt.so.5.14.0"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/qt/include/QtMqtt"
)
target_link_libraries(ThirdParty::Qt5Mqtt INTERFACE ThirdParty::Qt5Network)

# Qt Sql
add_library(ThirdParty::Qt5Sql SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::Qt5Sql PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/qt/lib/libQt5Sql.so.5.14.0"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/qt/include/QtSql"
)
target_link_libraries(ThirdParty::Qt5Sql INTERFACE ThirdParty::Qt5Core)

message(STATUS "  ✓ Qt5 已配置 (Core, Widgets, Network, Mqtt, Sql)")

# ==============================================================================
# 4. FastDeploy (AI 推理框架)
# ==============================================================================
message(STATUS "配置 FastDeploy...")

add_library(ThirdParty::FastDeploy SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::FastDeploy PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/fastdeploy/lib/libfastdeploy.so"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/fastdeploy/include"
)

message(STATUS "  ✓ FastDeploy 已配置")

# ==============================================================================
# 5. FFmpeg (视频处理)
# ==============================================================================
message(STATUS "配置 FFmpeg...")

# FFmpeg 主目标
add_library(ThirdParty::FFmpeg INTERFACE IMPORTED GLOBAL)
set_target_properties(ThirdParty::FFmpeg PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/soc-sdk/include"
)

# FFmpeg 各个库
set(FFMPEG_LIBS avcodec avformat avutil swscale)
foreach(lib ${FFMPEG_LIBS})
    add_library(ThirdParty::${lib} SHARED IMPORTED GLOBAL)
    set_target_properties(ThirdParty::${lib} PROPERTIES
        IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/soc-sdk/lib/lib${lib}.so"
    )
    target_link_libraries(ThirdParty::FFmpeg INTERFACE ThirdParty::${lib})
endforeach()

message(STATUS "  ✓ FFmpeg 已配置 (${FFMPEG_LIBS})")

# ==============================================================================
# 6. GStreamer (流媒体)
# ==============================================================================
message(STATUS "配置 GStreamer...")

add_library(ThirdParty::GStreamer INTERFACE IMPORTED GLOBAL)

# 设置头文件目录（关键修复！）
# 需要包含 third_party 目录，这样才能 #include <gst/gst.h>
set_target_properties(ThirdParty::GStreamer PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES 
        "${THIRD_PARTY_ROOT};${THIRD_PARTY_ROOT}/gst/app;${THIRD_PARTY_ROOT}/glib-2.0"
)

# GStreamer 库文件
# 注意：库文件名是 libgstreamer-1.0.so，但 CMake 会自动添加 lib 前缀
set(GST_LIBS gstreamer-1.0 gstapp-1.0 gobject-2.0 glib-2.0)
foreach(lib ${GST_LIBS})
    add_library(ThirdParty::${lib} SHARED IMPORTED GLOBAL)
    set_target_properties(ThirdParty::${lib} PROPERTIES
        IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/gst/lib/lib${lib}.so"
    )
    target_link_libraries(ThirdParty::GStreamer INTERFACE ThirdParty::${lib})
endforeach()

message(STATUS "  ✓ GStreamer 已配置")
message(STATUS "    - 头文件: ${THIRD_PARTY_ROOT} (包含 gst/, glib-2.0/)")
message(STATUS "    - 库文件: ${THIRD_PARTY_ROOT}/gst/lib (${GST_LIBS})")

# ==============================================================================
# 7. 图像处理库 (libexif, exiv2)
# ==============================================================================
message(STATUS "配置图像处理库...")

# libexif
add_library(ThirdParty::libexif SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::libexif PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/libexif/lib/libexif.so.12.3.4"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/libexif/include"
)

# exiv2
add_library(ThirdParty::exiv2 SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::exiv2 PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/exiv2/lib/libexiv2.so"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/exiv2/include"
)

message(STATUS "  ✓ libexif, exiv2 已配置")

# ==============================================================================
# 8. 压缩库 (minizip)
# ==============================================================================
message(STATUS "配置 minizip...")

add_library(ThirdParty::minizip SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::minizip PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/minizip/lib/libminizip.so"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/minizip/include"
)

message(STATUS "  ✓ minizip 已配置")

# ==============================================================================
# 9. SSH 库 (libssh2)
# ==============================================================================
message(STATUS "配置 libssh2...")

add_library(ThirdParty::libssh2 SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::libssh2 PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/libssh2-arm64/lib/libssh2.so"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/libssh2-arm64/include"
)

message(STATUS "  ✓ libssh2 已配置")

# ==============================================================================
# 10. 加密库 (libcrypto)
# ==============================================================================
message(STATUS "配置 libcrypto...")

add_library(ThirdParty::libcrypto SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::libcrypto PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/libcrypto/libcrypto.so"
    # libcrypto 是系统库,头文件在系统路径,不需要指定 INTERFACE_INCLUDE_DIRECTORIES
)

message(STATUS "  ✓ libcrypto 已配置")

# ==============================================================================
# 11. JSON 库 (nlohmann/json 或其他)
# ==============================================================================
message(STATUS "配置 JSON...")

# JSON 通常是 header-only 库
add_library(ThirdParty::Json INTERFACE IMPORTED GLOBAL)
set_target_properties(ThirdParty::Json PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/Json/include"
)

message(STATUS "  ✓ JSON 已配置")

# ==============================================================================
# 12. MQTT 库 (paho.mqtt.c)
# ==============================================================================
message(STATUS "配置 paho.mqtt.c...")

# Paho MQTT C 库（libpaho-mqtt3c.so - 同步版本）
add_library(ThirdParty::PahoMqttC SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::PahoMqttC PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/mqtt/lib/libpaho-mqtt3c.so.1.3.14"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/mqtt/include"
)

message(STATUS "  ✓ paho.mqtt.c 已配置")

# ==============================================================================
# 13. Sophon SDK (算能推理)
# ==============================================================================
message(STATUS "配置 Sophon SDK...")

add_library(ThirdParty::SophonSDK INTERFACE IMPORTED GLOBAL)
set_target_properties(ThirdParty::SophonSDK PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/soc-sdk/include"
)

# Sophon 库
set(SOPHON_LIBS bmlib bmrt yuv bmcv)
foreach(lib ${SOPHON_LIBS})
    add_library(ThirdParty::${lib} SHARED IMPORTED GLOBAL)
    set_target_properties(ThirdParty::${lib} PROPERTIES
        IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/soc-sdk/lib/lib${lib}.so"
    )
    target_link_libraries(ThirdParty::SophonSDK INTERFACE ThirdParty::${lib})
endforeach()

message(STATUS "  ✓ Sophon SDK 已配置 (${SOPHON_LIBS})")

# ==============================================================================
# 汇总：创建便捷目标
# ==============================================================================

# 创建一个包含所有常用库的目标（可选）
add_library(ThirdParty::Common INTERFACE IMPORTED GLOBAL)
target_link_libraries(ThirdParty::Common INTERFACE
    ThirdParty::ESDK
    ThirdParty::OpenCV
    ThirdParty::Qt5Core
    ThirdParty::Json
)

message(STATUS "========================================")
message(STATUS "第三方库配置完成！")
message(STATUS "========================================")

# ==============================================================================
# 使用说明
# ==============================================================================
# 
# 在你的 CMakeLists.txt 中使用：
#
# # 1. 添加第三方库目录
# add_subdirectory(third_party)
#
# # 2. 链接需要的库
# target_link_libraries(myapp
#     ThirdParty::ESDK
#     ThirdParty::OpenCV
#     ThirdParty::Qt5Core
#     ThirdParty::Qt5Mqtt
# )
#
# 优点：
# 1. 自动获得头文件路径（不需要手动 include_directories）
# 2. 自动处理依赖关系（Qt5Mqtt 会自动链接 Qt5Network 和 Qt5Core）
# 3. 统一管理，修改方便
# 4. 符合现代 CMake 最佳实践
#
# ==============================================================================

# ==============================================================================
# 14. DJI Edge-SDK (官方SDK库)
# ==============================================================================
message(STATUS "配置 DJI Edge-SDK...")

# Edge-SDK 使用 third_party/esdk 目录（容器内可用）
set(EDGE_SDK_ROOT "${THIRD_PARTY_ROOT}/esdk")

# 创建 Edge-SDK IMPORTED 目标（静态库）
add_library(ThirdParty::EdgeSDK STATIC IMPORTED GLOBAL)

# 设置库文件位置（使用ARM64版本）
set_target_properties(ThirdParty::EdgeSDK PROPERTIES
    IMPORTED_LOCATION "${EDGE_SDK_ROOT}/lib/aarch64/libedgesdk.a"
    # 设置头文件目录
    INTERFACE_INCLUDE_DIRECTORIES "${EDGE_SDK_ROOT}/include"
)

# Edge-SDK 依赖 libssh2 和 libcrypto
# 使用 INTERFACE 自动传递依赖
target_link_libraries(ThirdParty::EdgeSDK INTERFACE
    ThirdParty::libssh2      # SSH通信库
    ThirdParty::libcrypto    # 加密库
)

message(STATUS "  ✓ Edge-SDK 已配置")
message(STATUS "    - 库文件: ${EDGE_SDK_ROOT}/lib/aarch64/libedgesdk.a")
message(STATUS "    - 头文件: ${EDGE_SDK_ROOT}/include")
message(STATUS "    - 依赖: libssh2, libcrypto")

# ==============================================================================
# 15. CURL 库 (HTTP 客户端)
# ==============================================================================
message(STATUS "配置 CURL...")

add_library(ThirdParty::CURL SHARED IMPORTED GLOBAL)
set_target_properties(ThirdParty::CURL PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_ROOT}/curl/lib/libcurl.so"
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/curl/include"
)

message(STATUS "  ✓ CURL 已配置")
message(STATUS "    - 库文件: ${THIRD_PARTY_ROOT}/curl/lib/libcurl.so")
message(STATUS "    - 头文件: ${THIRD_PARTY_ROOT}/curl/include")

# ==============================================================================
# 16. ByteTrack 追踪库 (目标追踪)
# ==============================================================================
message(STATUS "配置 ByteTrack...")

# 添加 ByteTrack 子目录（它有自己的 CMakeLists.txt）
add_subdirectory(bytetrack)

# 创建别名，方便使用
add_library(ThirdParty::ByteTrack ALIAS bytetrack)

message(STATUS "  ✓ ByteTrack 已配置")
message(STATUS "    - 源码目录: ${THIRD_PARTY_ROOT}/bytetrack")
message(STATUS "    - 头文件: ${THIRD_PARTY_ROOT}/bytetrack/include")
# ==============================================================================
# 知识点: Edge-SDK 集成要点
# ==============================================================================
#
# 1. **静态库 vs 共享库**:
#    - Edge-SDK 提供的是 .a 静态库,不是 .so
#    - 编译时会完整打包到最终可执行文件中
#
# 2. **依赖传递**:
#    target_link_libraries(my_app ThirdParty::EdgeSDK)
#    ↓ 自动传递
#    - 头文件: ${EDGE_SDK_ROOT}/include
#    - 库文件: libedgesdk.a + libssh2.so + libcrypto.so
#
# 3. **使用示例**:
#    ```cmake
#    # src/device/CMakeLists.txt
#    add_library(device STATIC DeviceManager.cpp)
#    target_link_libraries(device
#        PUBLIC esdk_sophon::core
#        PRIVATE ThirdParty::EdgeSDK  # Edge-SDK只在.cpp中使用
#    )
#    ```
#
# 4. **面试要点**:
#    Q: 为什么Edge-SDK用PRIVATE链接?
#    A: DeviceManager.h不暴露Edge-SDK的类型(通过Pimpl隐藏),
#       上层模块不需要知道Edge-SDK的存在,所以用PRIVATE。
#
# ==============================================================================
