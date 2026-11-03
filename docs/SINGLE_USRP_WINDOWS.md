# 单台X310：Windows迁移与射频回环

## 可以先开发，后连接设备

当前电脑不需要连接USRP。复制源码包到实验室Windows电脑，在那里配置UHD、编译硬件程序，再调试射频链路。先做完软件不要求先完成硬件联调；但真实射频性能必须实测才能确认。

单台X310是否能同时收发取决于射频子板。例如UBX支持全双工，通常使用TX/RX输出和RX2输入。接收专用子板不能承担发送功能。因此仅知道主机是X310还不能确认射频端口配置。

单台回环可验证：完整编码/调制→DAC/射频TX→电缆或空间→射频RX/ADC→同步/均衡/解调→业务接收。
它不验证两个独立节点的采样时钟偏差、独立参考时钟/本振条件、远距离覆盖和真实外场多径。两个独立节点验证可以放在后续阶段，不是当前开发的前置条件。

## 包里有什么

- `src`、`include`：所有当前版本C++源码。
- `CMakeLists.txt`：可选择启用UHD，无隐藏的旧MATLAB依赖。
- `build/streamlink.exe`：已测试的Windows软件链路程序。
- `scripts/build-windows.ps1`：不依赖原开发机E盘工具链路径的构建入口。
- `scripts/single-usrp.ps1`：单设备射频回环启动和统计。
- `scripts/probe-usrp.ps1`：硬件信息采集。
- `tests`：自动验收；Python仅用于运行测试，不是业务链路依赖。
- `MANIFEST.json`：包内文件的SHA256。

当前源码提供QPSK OFDM、同步/CFO/均衡、UDP业务桥接、CRC和统计。**FEC、自动MCS和视频码率自适应仍未实现，不能把“所有当前源码都在包里”理解为最初设想的全部功能已经完成。**

## 目标Windows电脑环境

1. Visual Studio 2022或兼容的新版本，勾选“使用C++的桌面开发”、Windows SDK和CMake。
2. Ettus UHD的x64 C++ SDK和运行时：需要头文件、链接库、DLL，不能只有Python包或只有NI LabVIEW驱动。先检查现有安装和版本，不盲目升级已有实验室驱动/FPGA。
3. 使用该SDK兼容的x64 MSVC工具链。UHD的bin目录加入当前终端PATH；根据SDK要求安装对应VC运行时及依赖DLL。
4. 可选FFmpeg/FFplay用于视频；若复制整个原项目，也可一并复制`tools/ffmpeg`。

打开“Developer PowerShell for VS”，解压后进入项目根目录，例如：

```powershell
cd 'D:\StreamLink'
.\scripts\build-windows.ps1 -WithUhd -UhdRoot 'C:\Program Files\UHD'
```

UHD安装路径不同就修改参数。脚本只构建和运行软件自检，不会自动发射。成功后应有`build/streamlink.exe`与`build/streamlink_usrp.exe`。迁移包不会伪装提供一个尚未经过真实UHD SDK编译的硬件exe。

## 先探测，再选择端口

```powershell
$env:PATH='C:\Program Files\UHD\bin;'+$env:PATH
uhd_config_info --version
.\scripts\probe-usrp.ps1 -DeviceArgs 'addr=192.168.10.2'
.\build\streamlink_usrp.exe probe --args addr=192.168.10.2
```

示例IP不是对你实验室设备IP的断言。PC网卡和设备须按实验室已有网络配置通信。探测失败先处理IP/驱动/镜像兼容，不运行视频掩盖底层问题。

## 单设备接线

```text
Windows PC
  ├─ streamlink tx（业务数据）→ UDP 9001
  ├─ streamlink_usrp duplex（一个设备句柄，两个收发线程）
  │    ├─ UHD TX → X310 TX/RX
  │    │                 ↓ 衰减器＋射频线缆
  │    └─ UHD RX ← X310 RX2
  └─ UDP 9002 → streamlink rx（CRC、去重、业务吞吐）
```

端口名称以子板为准。根据TX功率和RX最大允许输入选择足够衰减，不能不加衰减直接把发射输出接到接收输入；增益数字也不能直接当作dBm。

不依赖设备内部泄漏做主测试，也不把“同频同时收发”当作已经解决同频全双工自干扰消除。回环链路的隔离/衰减是受控实验条件。

## 启动回环

先确定子板支持的实验频率、端口及衰减。下面的频率变量必须填写实际值，不能原样留空：

```powershell
$freqHz = [double](Read-Host '输入已确认的测试中心频率，单位Hz')
.\scripts\single-usrp.ps1 -FrequencyHz $freqHz `
  -DeviceArgs 'addr=192.168.10.2' -UhdRoot 'C:\Program Files\UHD' `
  -TxAntenna 'TX/RX' -RxAntenna 'RX2' `
  -Rate 1000000 -Mbps 0.5 -TxGain 0 -RxGain 0 -Seconds 30
```

输出在`out/single-usrp-时间/`：radio.log、radio.err、tx.csv、rx.csv和summary.json。
当前双工模式默认同一逻辑通道0。若子板映射不同，不要盲目选择端口；先核对probe后调整代码/配置。

不要对同一台USRP分别开两个`streamlink_usrp tx/rx`进程。`duplex`模式只打开一次设备，设备配置在线程启动前完成；RX先启动，TX/RX分别使用自己的streamer。异常会通知另一线程停止并等待它退出。

## 分级验收

1. 1 MS/s，0.5 Mbps业务；先确认收到CRC有效包。
2. 提升至5 MS/s，业务速率逐步加到3～4 Mbps。
3. 提升至10 MS/s，再测试6～9 Mbps业务。10 MS/s、sc16单方向IQ约320 Mbps，双向分别占用以太网的发送与接收方向；实际能力要观察UHD的overflow/underflow。
4. 业务链路稳定后，用视频入口替换`streamlink tx`，将`streamlink rx`加上`--out 127.0.0.1:5601`。**不要同时运行软件`channel`，否则视频可能绕过USRP。**
5. 核对射频因果：在正确负载终止/接线规范下改变RX频率或关闭TX，接收有效业务应中断；恢复后应重新捕获。
6. 完成长时间连续运行，再测真实摄像头到显示的时延。

## 已验证与待验证

已验证：数据协议、OFDM软件回环、线程异常协调、PC UDP吞吐和视频编码/解码。
尚未验证：新duplex源码在目标UHD SDK下的编译、单台X310实际同时收发、真实空口性能、目标Windows的驱动/镜像兼容。迁移后首次构建错误和radio.err是下一步定位依据。

参考：
- UHD streamer线程约束：https://github.com/EttusResearch/uhd/blob/master/host/include/uhd/stream.hpp
- UBX端口与全双工说明：https://kb.ettus.com/UBX
