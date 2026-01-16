import torch
import numpy as np
from numpy import random
from models.experimental import attempt_load
from utils.datasets import LoadStreams, LoadImages
from utils.general import check_img_size, non_max_suppression, scale_coords, set_logging
from utils.plots import plot_one_box
from utils.torch_utils import select_device
import config  # 导入配置文件

class YOLODetector:
    def __init__(self, weights=config.MODEL_PATH, device='', img_size=config.IMG_SIZE):
        """
        初始化检测器：这里只加载一次模型，避免重复加载导致的卡顿
        """
        set_logging()
        self.device = select_device(device)
        self.half = self.device.type != 'cpu'  # 如果有显卡就用半精度FP16

        # 加载模型
        print(f"正在加载模型: {weights} ...")
        self.model = attempt_load(weights, map_location=self.device)
        self.stride = int(self.model.stride.max())
        self.imgsz = check_img_size(img_size, s=self.stride)

        if self.half:
            self.model.half()

        # 获取标签名字和颜色
        self.names = self.model.module.names if hasattr(self.model, 'module') else self.model.names
        self.colors = [[random.randint(0, 255) for _ in range(3)] for _ in self.names]

        # 热身 (让模型跑一次空数据，加速后续推理)
        if self.device.type != 'cpu':
            self.model(torch.zeros(1, 3, self.imgsz, self.imgsz).to(self.device).type_as(next(self.model.parameters())))
        print("模型加载完成！")

    def detect(self, source=0, conf_thres=config.CONF_THRES, iou_thres=config.IOU_THRES, draw=True):
        """
        生成器函数：不断产生 (im0, detections, fps_calc)
        """
        # 数据源加载
        webcam = str(source).isnumeric() or str(source).lower().startswith(('rtsp://', 'rtmp://', 'http', 'https'))
        if webcam:
            dataset = LoadStreams(str(source), img_size=self.imgsz, stride=self.stride)
        else:
            dataset = LoadImages(source, img_size=self.imgsz, stride=self.stride)

        for path, img, im0s, vid_cap in dataset:
            # 预处理图片
            img_tensor = torch.from_numpy(img).to(self.device)
            img_tensor = img_tensor.half() if self.half else img_tensor.float()
            img_tensor /= 255.0
            if img_tensor.ndimension() == 3:
                img_tensor = img_tensor.unsqueeze(0)

            # 推理
            pred = self.model(img_tensor)[0]
            
            # NMS (非极大值抑制)
            pred = non_max_suppression(pred, conf_thres, iou_thres)

            detections = []
            
            # 处理每一帧的检测结果
            for i, det in enumerate(pred):
                im0 = im0s[i].copy() if webcam else im0s.copy()
                
                if len(det):
                    # 将坐标还原回原图尺寸
                    det[:, :4] = scale_coords(img_tensor.shape[2:], det[:, :4], im0.shape).round()
                    
                    for *xyxy, _, cls in reversed(det):
                        label_name = self.names[int(cls)]
                        # 记录结果
                        detections.append((label_name, list(map(int, xyxy))))
                        
                        if draw:
                            plot_one_box(xyxy, im0, label=label_name, color=self.colors[int(cls)], line_thickness=3)

            # yield 返回给 GUI
            yield im0, detections