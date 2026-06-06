# config.py
# ================= 全局配置 =================

# 串口配置
SERIAL_PORT = "COM12"  # None 表示自动寻找第一个可用串口，也可以指定如 'COM3'
BAUD_RATE = 9600
TIMEOUT = 0.1

# 模型配置
MODEL_PATH = "weights/best2.pt"  # 你的模型路径
IMG_SIZE = 640
CONF_THRES = 0.45
IOU_THRES = 0.5

# 垃圾分类标签 (对应下位机通讯协议)
# 格式: '标签名': 发送给下位机的数字
LABELS_TO_IDS = {
    'recyclable waste': 1,
    'other waste': 2,
    'kitchen waste': 3,
    'hazardous waste': 4
}

# 连续发送时的稳定阈值 (检测到多少次才算有效)
STABLE_THRESHOLD = 1