# 双端重构验证记录（2026-11-03）

## 已执行

- Windows MSVC Release构建：video_transmitter、video_receiver、共用video_radio库及旧协议/基带测试程序。
- 发射端和接收端的独立build.ps1分别构建成功。
- 协议、CRC、重复过滤、OFDM、频偏/噪声/轻度多径与分块IQ输入的已有回归测试通过。
- 新端点配置检查：未知字段、未配置射频参数明确失败。
- UTF-8 BOM配置、中文及空格路径可用。
- 40个1316字节数据报经过新发射端→新接收端，内容完全一致。
- 1500字节超大输入被拒绝，后续合法数据继续传输。
- 停止文件触发正常退出，避免平时直接强制终止UHD程序。
- 新transmitter/start.ps1真实启动FFmpeg编码器，显式测试图→新双端程序→FFmpeg解码器成功解码180帧1920×1080、30fps视频，编码目标6 Mbps。

## 测试条件与限制

端点与视频测试使用明确的`backend=udp_test`，视频源为显式`-TestPattern`。这些结果验证软件连接关系和编码/解码通路，**不证明摄像头采集、真实空口吞吐或时延**。

本开发机没有可用的UHD C++ SDK和已连接的X310，因此`shared/radio_device.cpp`的UHD编译分支及射频运行仍须在目标电脑验证。非UHD构建在`backend=usrp`下会报错，不会回退到网络转发。

尚未验收：两台内部时钟设备的长期稳定性、采样时钟偏差、摄像头到屏幕时延、FEC/MCS自适应。

复现：

```powershell
.\scripts\build-windows.ps1
python tests/endpoints.py
python tests/live_video_smoke.py
```
