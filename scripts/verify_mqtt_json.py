#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
MQTT JSON 格式验证工具

功能:
1. 订阅 MQTT 事件推送 Topic
2. 解析 JSON 格式
3. 验证字段完整性
4. 保存 Base64 图片到本地

使用方法:
    python3 scripts/verify_mqtt_json.py

作者: GitHub Copilot
日期: 2025-12-12
"""

import json
import base64
import time
from datetime import datetime
import paho.mqtt.client as mqtt

# ==================== 配置 ====================
MQTT_HOST = "jiaoyujidi.work"
MQTT_PORT = 1883
MQTT_USERNAME = "admin"
MQTT_PASSWORD = "admin123"
DEVICE_SN = "1581F5BKD223K00A0025"  # 替换为你的设备序列号
TOPIC = f"drone/{DEVICE_SN}/info/event"

OUTPUT_DIR = "./mqtt_results/"

# ==================== 颜色输出 ====================
class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

def print_success(msg):
    print(f"{Colors.OKGREEN}✅ {msg}{Colors.ENDC}")

def print_error(msg):
    print(f"{Colors.FAIL}❌ {msg}{Colors.ENDC}")

def print_warning(msg):
    print(f"{Colors.WARNING}⚠️  {msg}{Colors.ENDC}")

def print_info(msg):
    print(f"{Colors.OKCYAN}ℹ️  {msg}{Colors.ENDC}")

# ==================== JSON 验证 ====================
def verify_json_format(payload):
    """验证 JSON 格式是否符合新接口规范"""
    
    try:
        data = json.loads(payload)
        
        print_info("开始验证 JSON 格式...")
        print("-" * 60)
        
        # 1. 验证顶层字段
        required_top_fields = ["UUID", "taskID", "main_type", "eventType", 
                               "createTime", "result"]
        
        for field in required_top_fields:
            if field in data:
                print_success(f"顶层字段 '{field}': {data.get(field, 'N/A')}")
            else:
                print_error(f"缺少顶层字段: {field}")
        
        # 2. 验证 taskID 类型 (应该是 int)
        if isinstance(data.get("taskID"), int):
            print_success(f"taskID 类型正确: int ({data['taskID']})")
        else:
            print_error(f"taskID 类型错误: {type(data.get('taskID'))} (期望 int)")
        
        # 3. 验证 result 对象
        if "result" in data:
            result = data["result"]
            
            # 3.1 验证 result.image
            if "image" in result:
                image_b64 = result["image"]
                if len(image_b64) > 0:
                    print_success(f"result.image 存在 (Base64 长度: {len(image_b64)} 字符)")
                    
                    # 尝试解码 Base64
                    try:
                        img_data = base64.b64decode(image_b64)
                        print_success(f"Base64 解码成功 (图片大小: {len(img_data)} 字节)")
                        
                        # 保存图片
                        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                        filename = f"{OUTPUT_DIR}result_image_{timestamp}.jpg"
                        
                        import os
                        os.makedirs(OUTPUT_DIR, exist_ok=True)
                        
                        with open(filename, "wb") as f:
                            f.write(img_data)
                        print_success(f"图片已保存: {filename}")
                        
                    except Exception as e:
                        print_error(f"Base64 解码失败: {e}")
                else:
                    print_warning("result.image 为空")
            else:
                print_error("缺少 result.image 字段")
            
            # 3.2 验证 result.objects
            if "objects" in result:
                objects = result["objects"]
                print_success(f"result.objects 存在 (共 {len(objects)} 个对象)")
                
                for i, obj in enumerate(objects):
                    print(f"\n  📌 对象 {i+1}:")
                    
                    # label
                    if "label" in obj:
                        print_success(f"    label: {obj['label']}")
                    else:
                        print_error(f"    缺少 label 字段")
                    
                    # bbox
                    if "bbox" in obj:
                        bbox = obj["bbox"]
                        print_success(f"    bbox: x={bbox.get('x')}, y={bbox.get('y')}, "
                                     f"w={bbox.get('w')}, h={bbox.get('h')}")
                        
                        # bbox.location
                        if "location" in bbox:
                            loc = bbox["location"]
                            print_success(f"    bbox.location: lon={loc.get('lon')}, lat={loc.get('lat')}")
                        else:
                            print_error(f"    bbox 缺少 location 字段")
                    else:
                        print_error(f"    缺少 bbox 字段")
                    
                    # mask
                    if "mask" in obj:
                        mask = obj["mask"]
                        print_success(f"    mask: {len(mask)} 个点")
                        
                        if len(mask) > 0:
                            # 显示第一个点
                            pt = mask[0]
                            print_info(f"    mask[0]: x={pt.get('x')}, y={pt.get('y')}, "
                                      f"location={{lon={pt.get('location', {}).get('lon')}, "
                                      f"lat={pt.get('location', {}).get('lat')}}}")
                    else:
                        print_warning(f"    mask 字段为空 (目标检测任务正常)")
                
            else:
                print_error("缺少 result.objects 字段")
        else:
            print_error("缺少 result 对象")
        
        print("-" * 60)
        print_success("JSON 格式验证完成!")
        
        # 保存完整 JSON
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        json_filename = f"{OUTPUT_DIR}event_{timestamp}.json"
        with open(json_filename, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        print_success(f"JSON 已保存: {json_filename}")
        
        return True
        
    except json.JSONDecodeError as e:
        print_error(f"JSON 解析失败: {e}")
        return False
    except Exception as e:
        print_error(f"验证异常: {e}")
        return False

# ==================== MQTT 回调 ====================
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print_success(f"已连接到 MQTT Broker: {MQTT_HOST}:{MQTT_PORT}")
        client.subscribe(TOPIC)
        print_success(f"已订阅 Topic: {TOPIC}")
    else:
        print_error(f"连接失败, 错误码: {rc}")

def on_message(client, userdata, msg):
    print("\n" + "=" * 80)
    print_info(f"收到消息: {msg.topic}")
    print_info(f"时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 80)
    
    payload = msg.payload.decode('utf-8')
    verify_json_format(payload)
    
    print("\n等待下一条消息...\n")

# ==================== 主程序 ====================
def main():
    print("=" * 80)
    print(f"{Colors.HEADER}{Colors.BOLD}MQTT JSON 格式验证工具{Colors.ENDC}")
    print("=" * 80)
    print_info(f"MQTT Broker: {MQTT_HOST}:{MQTT_PORT}")
    print_info(f"订阅 Topic: {TOPIC}")
    print_info(f"输出目录: {OUTPUT_DIR}")
    print("=" * 80)
    print()
    
    import os
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    client = mqtt.Client()
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(MQTT_HOST, MQTT_PORT, 60)
        print_info("开始监听 MQTT 消息... (按 Ctrl+C 停止)")
        client.loop_forever()
        
    except KeyboardInterrupt:
        print_info("\n用户中断,正在退出...")
        client.disconnect()
    except Exception as e:
        print_error(f"连接异常: {e}")

if __name__ == "__main__":
    main()
