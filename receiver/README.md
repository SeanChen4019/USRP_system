# 接收端

这个文件夹负责：**USRP采样 → 同步/解调 → 校验和去重 → H.264视频显示**。

在仓库根目录使用：

先单独编译本端：`./receiver/build.ps1 -UhdRoot 'C:\Program Files\UHD'`。无UHD的开发机显式加`-SoftwareOnly`，只能做软件测试。

1. 修改本文件夹的 `config.ini`：设备IP、接收端口、增益；频率和采样率与发射端一致。
2. `./receiver/start.ps1 -CheckOnly`：只检查，不打开硬件。
3. `./receiver/start.ps1`：先打开播放器，再打开USRP接收。
4. 等待发送端启动摄像头。关闭播放器或Ctrl+C会停止本端。

## 读代码顺序

- `main.cpp`：配置和入口。
- `receiver_app.cpp`：从`radio->receive()`取得IQ，交给`RadioDecoder`；恢复出的数据包经过CRC和去重，才送给本机播放器。
- `start.ps1`：播放器参数和启动入口。
- `../shared/include/modem.hpp`：以后改同步、均衡或频偏补偿时看这里。

接收端不需要配置摄像头或知道发射电脑IP。正式模式的业务数据来自射频链路。`127.0.0.1:5601`只是本机播放器的输入。

当前接收端锁定首个会话；发送端重启后，请同步重启接收端。无FEC的链路丢包可能造成花屏/等待下一个关键帧，不能把播放器缓存当成抗干扰能力。
