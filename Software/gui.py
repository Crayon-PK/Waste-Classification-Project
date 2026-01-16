import sys
import time
import cv2
import numpy as np
from serial.tools import list_ports
from PySide6.QtWidgets import QApplication, QLabel, QWidget, QVBoxLayout, QPushButton, QHBoxLayout, QTextEdit
from PySide6.QtGui import QImage, QPixmap, QTextCursor
from PySide6.QtCore import QThread, Signal, Slot, QObject, QTimer
import argparse

import config  # 导入新配置
from detect import YOLODetector  # 导入新写的类
from chuanko import SerialTool

# ================================== 自定义 UI 组件 ==================================
class InfoPanel(QTextEdit):
    def __init__(self, parent=None, placeholder: str = ""):
        super().__init__(parent)
        self.setReadOnly(True)
        self.setStyleSheet(
            "QTextEdit { background-color: #f0f0f0; border: 1px solid #ccc;"
            "border-radius: 5px; padding: 8px; font-size: 12px; }"
        )
        self.setPlaceholderText(placeholder)

    def update_info(self, text: str):
        self.setPlainText(text)

    def append_info(self, text: str):
        self.append(text)
        self.moveCursor(QTextCursor.End)


class CustomButton(QPushButton):
    def __init__(self, text: str, parent=None, callback=None, **kwargs):
        super().__init__(text, parent)
        if callback:
            self.clicked.connect(callback)
        if 'style' in kwargs:
            self.setStyleSheet(kwargs['style'])
        if 'size' in kwargs:
            self.setFixedSize(*kwargs['size'])


# ================================== SendWorker (发送逻辑) ==================================
class SendWorker(QObject):
    finished = Signal()
    log = Signal(str)
    stats = Signal(dict)

    @Slot(str, list)
    def send_one(self, label, xyxy):
        # 从 config 获取 ID，如果没有找到则默认 0
        tag = config.LABELS_TO_IDS.get(label, 0)
        
        # 计算中心点
        cx = int((xyxy[0] + xyxy[2]) // 2)
        cy = int((xyxy[1] + xyxy[3]) // 2)

        self.log.emit(f"📤 准备发送 -> {label} (ID:{tag}) cx={cx} cy={cy}")
        
        success = False
        try:
            if hasattr(self, 'serial_tool') and self.serial_tool:
                success = self.serial_tool.send_frame(tag, cx, cy)
            else:
                self.log.emit("❌ 错误：串口工具未初始化")
        except Exception as e:
            self.log.emit(f"❌ 发送异常: {e}")

        if success:
            self.stats.emit({label: 1})
            self.log.emit("✅ 发送指令成功")
        else:
            self.log.emit("❌ 发送指令失败 (请检查串口)")

        self.finished.emit()


# ================================== DetectionThread (检测线程) ==================================
class DetectionThread(QThread):
    frame_ready = Signal(np.ndarray, list, float)

    def __init__(self, source=0):
        super().__init__()
        self.source = source
        self._paused = False
        self._running = True
        # 在线程初始化时加载模型，避免 run() 里的重复加载
        # 注意：这可能会导致 GUI 启动时稍微卡顿一下，是正常的
        self.detector = YOLODetector()

    def run(self):
        # 使用 detector 类的 detect 方法生成数据
        gen = self.detector.detect(source=self.source)
        
        prev_time = time.time()
        for frame, simple_det in gen:
            if not self._running:
                break
            
            # 暂停逻辑：空转等待
            while self._paused and self._running:
                time.sleep(0.05)
            
            if not self._running:
                break
                
            curr_time = time.time()
            fps = 1.0 / (curr_time - prev_time) if (curr_time - prev_time) > 0 else 0.0
            prev_time = curr_time
            
            self.frame_ready.emit(frame, simple_det, fps)

    def toggle_pause(self):
        self._paused = not self._paused

    def stop(self):
        self._running = False
        self._paused = False
        self.quit()
        self.wait()


# ================================== 主窗口 ==================================
class MainWindow(QWidget):
    log_signal = Signal(str)
    detection_signal = Signal(str)
    stats_signal = Signal(dict)
    continue_send_signal = Signal()

    def __init__(self, source=0):
        super().__init__()
        self.source = source
        self.setWindowTitle("智能垃圾分拣系统")
        self.resize(1080, 650)

        self.serial_tool = SerialTool()
        self.detection_thread = None

        # 初始化发送工作线程
        self.send_worker = SendWorker()
        self.send_worker.serial_tool = self.serial_tool # 注入串口工具
        self.send_thread = QThread()
        self.send_worker.moveToThread(self.send_thread)
        self.send_thread.start()
        
        # 连接信号
        self.send_worker.log.connect(self.safe_append_log)
        self.send_worker.stats.connect(self.safe_update_stats)
        self.continue_send_signal.connect(lambda: QTimer.singleShot(10, self._handle_continue_send))

        # 状态变量
        self.current_objects = []
        self.object_counts = {}
        self.multi_sending = False
        self.garbage_stats = {k: 0 for k in config.LABELS_TO_IDS} # 使用 config 的 key

        self.init_ui()
        self.bind_signals()
        
        # 启动逻辑
        self.start_detection_thread()
        self.init_serial()
        self.update_statistics_panel()

    def init_ui(self):
        main_layout = QHBoxLayout()
        left_layout, right_layout = QVBoxLayout(), QVBoxLayout()

        # 视频显示区域
        self.label = QLabel(self)
        self.label.setFixedSize(640, 480)
        self.label.setStyleSheet("background-color: black; border: 2px solid #333;")
        left_layout.addWidget(self.label)

        # 按钮区域
        btn_layout = QHBoxLayout()
        self.btn_pause = CustomButton("停止检测", self, size=(120, 45), callback=self.toggle_pause)
        self.btn_single = CustomButton("单次发送", self, size=(120, 45), callback=self.single_send)
        self.btn_multi = CustomButton("连续发送", self, size=(120, 45), callback=self.multi_send)
        self.btn_reset = CustomButton("系统复位", self, size=(120, 45), callback=self.reset)
        
        for btn in [self.btn_pause, self.btn_single, self.btn_multi, self.btn_reset]:
            btn_layout.addWidget(btn)
        left_layout.addLayout(btn_layout)

        # 信息面板区域
        self.detection_panel = InfoPanel(self, "当前检测到的物体...")
        self.log_panel = InfoPanel(self, "系统运行日志...")
        self.statistics_panel = InfoPanel(self, "垃圾分类统计...")
        
        right_layout.addWidget(QLabel("检测详情:"))
        right_layout.addWidget(self.detection_panel)
        right_layout.addWidget(QLabel("运行日志:"))
        right_layout.addWidget(self.log_panel)
        right_layout.addWidget(QLabel("累计统计:"))
        right_layout.addWidget(self.statistics_panel)

        main_layout.addLayout(left_layout)
        main_layout.addLayout(right_layout)
        self.setLayout(main_layout)

    def bind_signals(self):
        self.log_signal.connect(self.safe_append_log)
        self.detection_signal.connect(self.safe_update_detection)
        self.stats_signal.connect(self.safe_update_stats)

    def start_detection_thread(self):
        self.log_signal.emit("⏳ 正在加载模型，请稍候...")
        # 这里的 0 代表默认摄像头
        self.detection_thread = DetectionThread(source=self.source)
        self.detection_thread.frame_ready.connect(self.update_frame)
        self.detection_thread.start()
        self.log_signal.emit("✅ 模型加载完毕，开始检测")

    def toggle_pause(self):
        if not self.detection_thread:
            return
        self.detection_thread.toggle_pause()
        if self.detection_thread._paused:
            self.btn_pause.setText("恢复检测")
            self.log_signal.emit("⏸ 检测已暂停，列表已冻结")
        else:
            self.btn_pause.setText("停止检测")
            self.log_signal.emit("▶ 检测已恢复")

    # -------------------- 日志与界面更新 --------------------
    def safe_append_log(self, text: str):
        self.log_panel.append_info(text)

    def safe_update_detection(self, text: str):
        self.detection_panel.update_info(text)

    def safe_update_stats(self, stats: dict):
        for k, v in stats.items():
            if k in self.garbage_stats:
                self.garbage_stats[k] += v
        self.update_statistics_panel()

    def update_statistics_panel(self):
        text = "【累计分类统计】\n"
        for name, count in self.garbage_stats.items():
            text += f"{name}: {count}\n"
        self.statistics_panel.update_info(text)

    # -------------------- 发送逻辑控制 --------------------
    def single_send(self):
        if not self._check_paused(): return
        try:
            self.send_worker.finished.disconnect() 
        except: 
            pass # 防止未连接时断开报错
            
        self.multi_sending = True
        self.btn_multi.setEnabled(False)
        self.log_signal.emit("🚀 启动连续发送模式 (等待下位机 0xAA 信号)")
        self._send_next_item()

    def _single_send_finished(self):
        if self.current_objects:
            self.current_objects.pop(0)
        self.log_signal.emit(f"剩余待发送: {len(self.current_objects)}")
        if not self.current_objects:
            self._resume_detection()

    def _handle_continue_send(self):
        """收到 0xAA 后：移除上一个已完成的，发送下一个"""
        # 只有在连续模式下才处理
        if self.multi_sending:
            # 1. 移除刚刚做完的那个物体
            if self.current_objects:
                self.current_objects.pop(0) 
            
            # 2. 发送队列里的下一个
            self._send_next_item()
            
    def multi_send(self):
        if not self._check_paused(): return

        self.multi_sending = True
        self.btn_multi.setEnabled(False)
        self.log_signal.emit("🚀 启动连续发送模式 (等待下位机 0xAA 信号)")
        self._send_next_item()

    def _send_next_item(self):
        if not self.current_objects:
            self.multi_sending = False
            self.btn_multi.setEnabled(True)
            self.log_signal.emit("🏁 所有目标发送完毕")
            self._resume_detection()
            return

        label, xyxy = self.current_objects[0]
        # 发送，但不弹栈 (Pop)，等待串口收到 AA 后再弹
        QTimer.singleShot(0, lambda: self.send_worker.send_one(label, xyxy))

    def _check_paused(self):
        if not (self.detection_thread and self.detection_thread._paused):
            self.log_signal.emit("⚠️ 请先点击【停止检测】冻结目标列表")
            return False
        if not self.current_objects:
            self.log_signal.emit("❌ 当前画面未检测到任何垃圾")
            return False
        return True

    def _resume_detection(self):
        if self.detection_thread and self.detection_thread._paused:
            self.detection_thread.toggle_pause()
            self.btn_pause.setText("停止检测")
            self.log_signal.emit("🔄 自动恢复检测")

    # -------------------- 视频帧处理 --------------------
    def update_frame(self, frame, simple_det, fps):
        # 1. 绘制 FPS
        display_frame = frame.copy()
        cv2.putText(display_frame, f"FPS: {fps:.1f}", (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
        
        # 2. 转换图片格式显示 (OpenCV -> Qt)
        h, w, ch = display_frame.shape
        bytes_per_line = ch * w
        qimg = QImage(display_frame.data, w, h, bytes_per_line, QImage.Format.Format_BGR888)
        self.label.setPixmap(QPixmap.fromImage(qimg))

        # 3. 只有在【检测运行中】才更新当前物体列表
        # 如果暂停了，就保留 current_objects 不动，作为“冻结列表”
        if not (self.detection_thread and self.detection_thread._paused):
            self._process_stable_objects(simple_det)
            
            # 实时更新 UI 列表显示
            info_text = ""
            for label, xyxy in self.current_objects:
                cx, cy = (xyxy[0] + xyxy[2]) // 2, (xyxy[1] + xyxy[3]) // 2
                info_text += f"[{label}] 坐标:({cx},{cy})\n"
            self.detection_signal.emit(info_text)

    def _process_stable_objects(self, simple_det):
        """简单的帧间防抖逻辑"""
        curr_keys = set()
        new_objects = []
        
        for label, xyxy in simple_det:
            # 简单生成一个唯一key，比如 label + 大致坐标
            key = f"{label}_{xyxy[0]//10}_{xyxy[1]//10}" 
            curr_keys.add(key)
            
            self.object_counts[key] = self.object_counts.get(key, 0) + 1
            if self.object_counts[key] >= config.STABLE_THRESHOLD:
                new_objects.append((label, xyxy))
        
        # 衰减消失的物体
        for k in list(self.object_counts.keys()):
            if k not in curr_keys:
                self.object_counts[k] -= 1
                if self.object_counts[k] <= 0:
                    del self.object_counts[k]
                    
        self.current_objects = new_objects

    # -------------------- 串口通讯 --------------------
    def init_serial(self):
        if self.serial_tool.connect(port=config.SERIAL_PORT, baudrate=config.BAUD_RATE, timeout=config.TIMEOUT):
            self.log_signal.emit(f"✅ 串口已连接 (波特率 {config.BAUD_RATE})")
            self.serial_tool.start_receive(self.serial_receive_handler)
        else:
            self.log_signal.emit("❌ 串口连接失败，请检查连线")

    def serial_receive_handler(self, data: bytes):
        for byte in data:
            if byte == 0xAA:
                # 收到下位机“完成”信号
                self.log_signal.emit("📥 收到下位机完成信号 (0xAA)")
                if self.multi_sending:
                    self.continue_send_signal.emit() # 切回主线程处理
            elif byte == 0xFF:
                self.log_signal.emit("📥 收到复位确认 (0xFF)")
            else:
                pass 

    def _handle_continue_send(self):
        """主线程处理连续发送的下一步"""
        if self.multi_sending and self.current_objects:
            self.current_objects.pop(0) # 弹出刚才发完的
            self._send_next_item()      # 发下一个

    def reset(self):
        self.log_signal.emit("🔄 系统正在复位...")
        self.current_objects = []
        self.object_counts = {}
        self.garbage_stats = {k: 0 for k in config.LABELS_TO_IDS}
        self.update_statistics_panel()
        
        # 尝试发送复位信号
        try:
            if self.serial_tool.ser and self.serial_tool.ser.is_open:
                self.serial_tool.ser.write(bytes([0xFF]))
                self.log_signal.emit("📤 发送复位指令 0xFF")
        except:
            pass
            
        self.multi_sending = False
        self.btn_multi.setEnabled(True)
        self._resume_detection()

    def closeEvent(self, event):
        if self.detection_thread:
            self.detection_thread.stop()
        if self.send_thread:
            self.send_thread.quit()
        self.serial_tool.close()
        event.accept()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=str, default='0', help='摄像头源: 0, 1或视频路径')
    args = parser.parse_args()

    source = args.source
    if source.isnumeric():
        source = int(source)

    app = QApplication(sys.argv)

    win = MainWindow(source=source)
    win.show()
    sys.exit(app.exec())