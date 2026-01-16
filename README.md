\# 智能垃圾分类系统 (Smart Waste Classification System)



本项目包含从机械结构、电路设计、嵌入式固件到上位机代码。



\## 📂 目录结构说明



\* \*\*Software\*\*: 上位机代码 (Python + YOLOv5-Lite)，负责视觉识别与串口通讯。

\* \*\*Firmware\*\*: 下位机固件 (STM32F103 + FreeRTOS)，负责处理上位机信息，控制步进电机、舵机与 OLED日志打印。

\* \*\*Hardware\*\*: PCB 工程文件 (立创EDA专业版格式)。

\* \*\*Mechanical\*\*: 3D 机械结构设计 (SolidWorks 源文件 + 打印用 STL)。



\## 🚀 快速上手



\### 1. 软件端 (Software)

\* \*\*环境\*\*: Python 3.8+, 安装依赖: `pip install -r Software/requirements.txt`

\* \*\*运行\*\*: 

&nbsp;   1. 连接 USB 摄像头和串口工具。

&nbsp;   2. 进入目录: `cd Software`

&nbsp;   3. 运行主程序: `python gui.py`

\* \*\*注意\*\*: 权重文件已包含在 `Software/weights/best2.pt`，无需额外下载。



\### 2. 固件端 (Firmware)

\* \*\*环境\*\*: Keil uVision 5 (MDK-ARM).

\* \*\*编译\*\*: 打开 `Firmware/MDK-ARM/Project.uvprojx`，点击 "Rebuild" 即可重新生成 Hex 文件。

\* \*\*烧录\*\*: 使用 ST-Link Utility 或 Keil 直接下载。



\### 3. 硬件与机械

\* \*\*PCB\*\*: 使用立创EDA专业版打开 `Hardware/PCB.eprj2`。

\* \*\*3D\*\*: 使用 SolidWorks 2020 或更高版本打开 `Mechanical` 文件夹内的 `.SLDASM` 装配体。

