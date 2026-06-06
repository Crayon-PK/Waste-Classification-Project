import sys
import time
import cv2
import numpy as np
from serial.tools import list_ports
from PySide6.QtWidgets import QApplication, QLabel, QWidget, QVBoxLayout, QPushButton, QHBoxLayout, QTextEdit
from PySide6.QtGui import QImage, QPixmap, QTextCursor
from PySide6.QtCore import QThread, Signal, Slot, QObject, QTimer
import argparse

import config  # Import configuration
from detect import YOLODetector  # Import detection class
from chuanko import SerialTool

# ================================== Custom UI Components ==================================
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


# ================================== SendWorker (Sending Logic) ==================================
class SendWorker(QObject):
    finished = Signal()
    log = Signal(str)
    stats = Signal(dict)

    @Slot(str, list)
    def send_one(self, label, xyxy):
        # 获取ID
        tag = config.LABELS_TO_IDS.get(label, 0)
        
        # 计算像素中心点
        cx_pixel = int((xyxy[0] + xyxy[2]) // 2)
        cy_pixel = int((xyxy[1] + xyxy[3]) // 2)

        # ================= 坐标映射配置 (三点仿射变换) =================
        # 填入你测量的3个标定点数据
        # src_points: 像素坐标 [[x1, y1], [x2, y2], [x3, y3]]
        src_points = np.float32([
            [117, 255],  # 点1 像素
            [284, 135],  # 点2 像素
            [481, 55 ]   # 点3 像素
        ])

        # dst_points: 对应的机械臂脉冲坐标 [[X1, Y1], [X2, Y2], [X3, Y3]]
        # 注意：这里的 [X, Y] 必须与上面的像素点一一对应！
        dst_points = np.float32([
            [500,  1000],  # 点1 脉冲
            [1000, 1700],  # 点2 脉冲
            [1400, 2600]   # 点3 脉冲
        ])

        try:
            # 1. 自动计算 2x3 的仿射变换矩阵 M
            M = cv2.getAffineTransform(src_points, dst_points)

            # 2. 将当前识别到的像素坐标转换为矩阵格式 [x, y, 1]
            pixel_coord = np.array([cx_pixel, cy_pixel, 1.0])

            # 3. 矩阵乘法直接算出对应的脉冲坐标 [X, Y]
            pulse_coord = np.dot(M, pixel_coord)
            cx_pulse = int(pulse_coord[0]) # 对应的电机X轴脉冲
            cy_pulse = int(pulse_coord[1]) # 对应的电机Y轴脉冲

            # 4. 软限位保护（依然不可省去，防止异常识别导致撞机）
            MAX_PULSE_X = 2000 
            MAX_PULSE_Y = 3000
            cx_pulse = max(0, min(cx_pulse, MAX_PULSE_X))
            cy_pulse = max(0, min(cy_pulse, MAX_PULSE_Y))

        except Exception as e:
            self.log.emit(f"❌ 仿射变换计算出错: {e}")
            self.finished.emit()
            return
        # =============================================================

        self.log.emit(f"📤 Send -> {label} Px:({cx_pixel},{cy_pixel}) Pulse:({cx_pulse},{cy_pulse})")
        
        success = False
        try:
            if hasattr(self, 'serial_tool') and self.serial_tool:
                # 发送计算后的脉冲坐标
                success = self.serial_tool.send_frame(tag, cx_pulse, cy_pulse)
            else:
                self.log.emit("❌ Error: Serial tool not initialized")
        except Exception as e:
            self.log.emit(f"❌ Sending Exception: {e}")

        if success:
            self.stats.emit({label: 1})
            self.log.emit("✅ Command sent successfully")
        else:
            self.log.emit("❌ Failed to send command (check serial port)")

        self.finished.emit()


# ================================== DetectionThread (Detection Thread) ==================================
class DetectionThread(QThread):
    frame_ready = Signal(np.ndarray, list, float)

    def __init__(self, source=0):
        super().__init__()
        self.source = source
        self._paused = False
        self._running = True
        # Load model during initialization
        self.detector = YOLODetector()

    def run(self):
        # Use detector class detect method
        gen = self.detector.detect(source=self.source)
        
        prev_time = time.time()
        for frame, simple_det in gen:
            if not self._running:
                break
            
            # Pause logic
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


# ================================== Main Window ==================================
class MainWindow(QWidget):
    log_signal = Signal(str)
    detection_signal = Signal(str)
    stats_signal = Signal(dict)
    continue_send_signal = Signal()

    def __init__(self, source=0):
        super().__init__()
        self.source = source
        self.setWindowTitle("Intelligent Waste Sorting System")
        self.resize(1080, 650)

        self.serial_tool = SerialTool()
        self.detection_thread = None

        # Initialize sending worker thread
        self.send_worker = SendWorker()
        self.send_worker.serial_tool = self.serial_tool
        self.send_thread = QThread()
        self.send_worker.moveToThread(self.send_thread)
        self.send_thread.start()
        
        # Connect signals
        self.send_worker.log.connect(self.safe_append_log)
        self.send_worker.stats.connect(self.safe_update_stats)
        self.continue_send_signal.connect(lambda: QTimer.singleShot(10, self._handle_continue_send))

        # State variables
        self.current_objects = []
        self.object_counts = {}
        self.multi_sending = False
        self.garbage_stats = {k: 0 for k in config.LABELS_TO_IDS}

        self.init_ui()
        self.bind_signals()
        
        # Startup logic
        self.start_detection_thread()
        self.init_serial()
        self.update_statistics_panel()

    def init_ui(self):
        main_layout = QHBoxLayout()
        left_layout, right_layout = QVBoxLayout(), QVBoxLayout()

        # Video Display
        self.label = QLabel(self)
        self.label.setFixedSize(640, 480)
        self.label.setStyleSheet("background-color: black; border: 2px solid #333;")
        left_layout.addWidget(self.label)

        # Buttons
        btn_layout = QHBoxLayout()
        self.btn_pause = CustomButton("Stop Detection", self, size=(120, 45), callback=self.toggle_pause)
        self.btn_single = CustomButton("Single Send", self, size=(120, 45), callback=self.single_send)
        self.btn_multi = CustomButton("Continuous Send", self, size=(120, 45), callback=self.multi_send)
        self.btn_reset = CustomButton("System Reset", self, size=(120, 45), callback=self.reset)
        
        for btn in [self.btn_pause, self.btn_single, self.btn_multi, self.btn_reset]:
            btn_layout.addWidget(btn)
        left_layout.addLayout(btn_layout)

        # Info Panels
        self.detection_panel = InfoPanel(self, "Current objects detected...")
        self.log_panel = InfoPanel(self, "System logs...")
        self.statistics_panel = InfoPanel(self, "Waste classification statistics...")
        
        right_layout.addWidget(QLabel("Detection Details:"))
        right_layout.addWidget(self.detection_panel)
        right_layout.addWidget(QLabel("Operation Logs:"))
        right_layout.addWidget(self.log_panel)
        right_layout.addWidget(QLabel("Cumulative Statistics:"))
        right_layout.addWidget(self.statistics_panel)

        main_layout.addLayout(left_layout)
        main_layout.addLayout(right_layout)
        self.setLayout(main_layout)

    def bind_signals(self):
        self.log_signal.connect(self.safe_append_log)
        self.detection_signal.connect(self.safe_update_detection)
        self.stats_signal.connect(self.safe_update_stats)

    def start_detection_thread(self):
        self.log_signal.emit("⏳ Loading model, please wait...")
        self.detection_thread = DetectionThread(source=self.source)
        self.detection_thread.frame_ready.connect(self.update_frame)
        self.detection_thread.start()
        self.log_signal.emit("✅ Model loaded, starting detection")

    def toggle_pause(self):
        if not self.detection_thread:
            return
        self.detection_thread.toggle_pause()
        if self.detection_thread._paused:
            self.btn_pause.setText("Resume Detection")
            self.log_signal.emit("⏸ Detection paused, list frozen")
        else:
            self.btn_pause.setText("Stop Detection")
            self.log_signal.emit("▶ Detection resumed")

    # -------------------- UI Updates --------------------
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
        text = "【Cumulative Statistics】\n"
        for name, count in self.garbage_stats.items():
            text += f"{name}: {count}\n"
        self.statistics_panel.update_info(text)

    # -------------------- Control Logic --------------------
    def single_send(self):
        if not self._check_paused(): return
        try:
            self.send_worker.finished.disconnect() 
        except: 
            pass
            
        self.multi_sending = True
        self.btn_multi.setEnabled(False)
        self.log_signal.emit("🚀 Starting continuous mode (waiting for 0xAA signal)")
        self._send_next_item()

    def _single_send_finished(self):
        if self.current_objects:
            self.current_objects.pop(0)
        self.log_signal.emit(f"Remaining items: {len(self.current_objects)}")
        if not self.current_objects:
            self._resume_detection()

    def _handle_continue_send(self):
        """After receiving 0xAA: remove completed, send next"""
        if self.multi_sending:
            if self.current_objects:
                self.current_objects.pop(0) 
            self._send_next_item()
            
    def multi_send(self):
        if not self._check_paused(): return

        self.multi_sending = True
        self.btn_multi.setEnabled(False)
        self.log_signal.emit("🚀 Starting continuous mode (waiting for 0xAA signal)")
        self._send_next_item()

    def _send_next_item(self):
        if not self.current_objects:
            self.multi_sending = False
            self.btn_multi.setEnabled(True)
            self.log_signal.emit("🏁 All targets sent")
            self._resume_detection()
            return

        label, xyxy = self.current_objects[0]
        # Wait for AA signal before popping
        QTimer.singleShot(0, lambda: self.send_worker.send_one(label, xyxy))

    def _check_paused(self):
        if not (self.detection_thread and self.detection_thread._paused):
            self.log_signal.emit("⚠️ Please click [Stop Detection] to freeze target list first")
            return False
        if not self.current_objects:
            self.log_signal.emit("❌ No waste detected in current frame")
            return False
        return True

    def _resume_detection(self):
        if self.detection_thread and self.detection_thread._paused:
            self.detection_thread.toggle_pause()
            self.btn_pause.setText("Stop Detection")
            self.log_signal.emit("🔄 Resuming detection automatically")

    # -------------------- Frame Processing --------------------
    def update_frame(self, frame, simple_det, fps):
        display_frame = frame.copy()
        cv2.putText(display_frame, f"FPS: {fps:.1f}", (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
        
        h, w, ch = display_frame.shape
        bytes_per_line = ch * w
        qimg = QImage(display_frame.data, w, h, bytes_per_line, QImage.Format.Format_BGR888)
        self.label.setPixmap(QPixmap.fromImage(qimg))

        if not (self.detection_thread and self.detection_thread._paused):
            self._process_stable_objects(simple_det)
            
            info_text = ""
            for label, xyxy in self.current_objects:
                cx, cy = (xyxy[0] + xyxy[2]) // 2, (xyxy[1] + xyxy[3]) // 2
                info_text += f"[{label}] Coord:({cx},{cy})\n"
            self.detection_signal.emit(info_text)

    def _process_stable_objects(self, simple_det):
        """Simple stabilization logic"""
        curr_keys = set()
        new_objects = []
        
        for label, xyxy in simple_det:
            key = f"{label}_{xyxy[0]//10}_{xyxy[1]//10}" 
            curr_keys.add(key)
            
            self.object_counts[key] = self.object_counts.get(key, 0) + 1
            if self.object_counts[key] >= config.STABLE_THRESHOLD:
                new_objects.append((label, xyxy))
        
        for k in list(self.object_counts.keys()):
            if k not in curr_keys:
                self.object_counts[k] -= 1
                if self.object_counts[k] <= 0:
                    del self.object_counts[k]
                    
        self.current_objects = new_objects

    # -------------------- Serial Communication --------------------
    def init_serial(self):
        if self.serial_tool.connect(port=config.SERIAL_PORT, baudrate=config.BAUD_RATE, timeout=config.TIMEOUT):
            self.log_signal.emit(f"✅ Serial connected (Baudrate: {config.BAUD_RATE})")
            self.serial_tool.start_receive(self.serial_receive_handler)
        else:
            self.log_signal.emit("❌ Serial connection failed, check wires")

    def serial_receive_handler(self, data: bytes):
        for byte in data:
            if byte == 0xFF:
                # 计算端到端通信与系统响应延迟
                if hasattr(self.serial_tool, 'last_send_time') and self.serial_tool.last_send_time > 0:
                    recv_time = time.perf_counter()
                    latency_ms = (recv_time - self.serial_tool.last_send_time) * 1000
                    
                    self.log_signal.emit(f"⏱️ [通信延迟测试] 收到 0xFF, 端到端响应耗时: {latency_ms:.2f} ms")
                    
                    # 清零，防止重复计算
                    self.serial_tool.last_send_time = 0.0
                else:
                    self.log_signal.emit("📥 Received immediate ACK (0xFF)")
                    
            elif byte == 0xAA:
                self.log_signal.emit("📥 Received Task Completed signal (0xAA)")
                
                # 动作做完了，自动触发下一个目标的发送
                if self.multi_sending:
                    self.continue_send_signal.emit()
            else:
                pass

    def reset(self):
        self.log_signal.emit("🔄 System resetting...")
        self.current_objects = []
        self.object_counts = {}
        self.garbage_stats = {k: 0 for k in config.LABELS_TO_IDS}
        self.update_statistics_panel()
        
        try:
            if self.serial_tool.ser and self.serial_tool.ser.is_open:
                reset_frame = bytearray([
                    0xFF,          # Tag (复位标志)
                    0x00, 0x00,    # X = 0
                    0x00, 0x00,    # Y = 0
                    0x00, 0x00,    # Z = 0
                    90             # Angle = 90 (下位机默认松开角度)
                ])
                self.serial_tool.ser.write(reset_frame)
                self.log_signal.emit("📤 Sent Reset command: X=0, Y=0, Angle=90")
        except Exception as e:
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
    parser.add_argument('--source', type=str, default='0', help='Camera source: 0, 1 or video path')
    args = parser.parse_args()

    source = args.source
    if source.isnumeric():
        source = int(source)

    app = QApplication(sys.argv)

    win = MainWindow(source=source)
    win.show()
    sys.exit(app.exec())