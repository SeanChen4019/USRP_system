# 发射端

这个文件夹负责：**Windows摄像头 → H.264编码 → 成帧 → OFDM → USRP发射**。

在仓库根目录使用：

先单独编译本端：`./transmitter/build.ps1 -UhdRoot 'C:\Program Files\UHD'`。它会构建发送程序和共用代码，不要求构建接收程序。无UHD的开发机显式加`-SoftwareOnly`，只能做软件测试。

1. `./transmitter/list-cameras.ps1`：找摄像头名称。
2. 修改本文件夹的 `config.ini`：摄像头名称、支持的分辨率/帧率、频率、子板发射端口、设备IP。
3. `./transmitter/start.ps1 -CheckOnly`：只检查，不打开硬件。
4. 确认另一台电脑接收端启动后，`./transmitter/start.ps1`。
5. Ctrl+C停止；源码改过要重新编译，配置改过只需重启。

## 读代码顺序

- `main.cpp`：读取配置、检查参数、调用应用。只有入口逻辑。
- `transmitter_app.cpp`：从本机编码器读一包视频，调用`pack()`组包，再调用`radio_encode()`生成IQ，最后`radio->send()`发给USRP。
- `start.ps1`：启动前检查、选择真实摄像头、构建FFmpeg编码参数。要改摄像头采集/编码策略，先改这里。
- `../shared/radio_device.cpp`：设备IP、频率、采样率、增益、天线、内部时钟和UHD发送逻辑。

默认不是发送测试图。只有显式传入`-TestPattern`才使用测试图，它仅用于没有摄像头的开发测试。

`payload_mbps`是送入发送链路的业务速率，不代表接收端成功吞吐。真正送达多少看接收端。
