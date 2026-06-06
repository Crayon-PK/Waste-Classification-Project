import serial
import time
import threading
from serial.tools import list_ports
from typing import Optional, Callable
import time

class SerialTool:
    def __init__(self):
        self.ser: Optional[serial.Serial] = None
        self.receive_thread: Optional[threading.Thread] = None
        self.is_running: bool = False
        self.receive_callback: Optional[Callable[[bytes], None]] = None  # 回调传完整数据帧
        self.last_send_time: float = 0.0
        

    def connect(self, port: Optional[str] = None, baudrate: int = 9600, timeout: float = 0.05) -> bool:
        try:
            if not port:
                ports = list_ports.comports()
                if not ports:
                    return False
                port = ports[0].device

            self.ser = serial.Serial(port=port, baudrate=baudrate, timeout=timeout)
            
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            # ==========================================
            
            return True
        except Exception as e:
            print(f"串口连接失败的真正原因：{e}")  # 把这句加上
            return False

    def send_frame(self, tag: int, x: int, y: int) -> bool:
        """
        发送完整数据帧
        帧结构：[Tag][XH][XL][YH][YL][ZH][ZL][Angle]
        """
        z = 0     # Z轴固定数据
        angle = 150  # Angle固定数据
        if not (self.ser and self.ser.is_open):
            return False

        try:
            frame = bytearray([
                tag & 0xFF,
                (x >> 8) & 0xFF, x & 0xFF,
                (y >> 8) & 0xFF, y & 0xFF,
                (z >> 8) & 0xFF, z & 0xFF,
                angle & 0xFF
            ])
            self.ser.write(frame)
            self.last_send_time = time.perf_counter()
            return True
        except Exception:
            return False

    def start_receive(self, callback: Callable[[bytes], None]):
        """启动接收线程，收到数据时回调传递"""
        if not self.ser or not self.ser.is_open:
            return
        self.receive_callback = callback
        self.is_running = True
        self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
        self.receive_thread.start()

    def _receive_loop(self):
        """后台接收循环：只处理下位机回的单字节 ACK (0xFF) 或其他调试数据"""
        while self.is_running and self.ser and self.ser.is_open:
            try:
                if self.ser.in_waiting > 0:
                    data = self.ser.read(1)  # 读1字节
                    if self.receive_callback:
                        self.receive_callback(data)
                time.sleep(0.001)
            except Exception:
                break

    def close(self):
        """关闭串口"""
        self.is_running = False
        if self.receive_thread and self.receive_thread.is_alive():
            self.receive_thread.join(timeout=1.0)
        if self.ser and self.ser.is_open:
            self.ser.close()
            self.ser = None

