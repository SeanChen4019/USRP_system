# USRP 实时摄像头图传（Windows / C++17）

现在只按两端理解和使用：**发射端 `transmitter/`，接收端 `receiver/`**。使用两台Windows电脑、两台X310；每端只运行一个启动脚本。

```text
发送PC摄像头 → FFmpeg/H.264 → video_transmitter → UHD → X310①
                                                       ↓ 空口
接收PC播放器 ← FFplay/H.264 ← video_receiver   ← UHD ← X310②
```

USRP两端使用各自内部时钟。`127.0.0.1`上的UDP只是本机编码器与C++程序之间的接口；正式`backend=usrp`不会通过电脑网络绕过无线链路。

**验证状态：新双端程序已通过MSVC编译及无硬件软件测试。摄像头和实际USRP空口必须在你的设备上验证。当前仍是固定QPSK、无FEC的实验波形，尚无自适应MCS、视频码率闭环和采样时钟偏差跟踪。**

## 目录只看这几个

```text
transmitter/                  发射端：你在发送电脑上主要改这里
  config.ini                  摄像头、视频和发射射频配置
  start.ps1                   一键启动摄像头和C++发送程序
  list-cameras.ps1            查看摄像头名称/支持格式
  main.cpp                    读取配置、启动发射应用
  transmitter_app.cpp         数据包 → OFDM → USRP 的主流程
receiver/                     接收端：你在接收电脑上主要改这里
  config.ini                  接收射频和播放器配置
  start.ps1                   一键启动C++接收程序和播放器
  main.cpp                    读取配置、启动接收应用
  receiver_app.cpp            USRP → OFDM解调 → 视频数据包的主流程
shared/                       两端共用，不复制两份算法
  radio_device.cpp            UHD设备配置、IQ收发、内部时钟
  include/modem.hpp           同步、频偏补偿、均衡、解调
  include/core.hpp            协议、CRC、FFT/QPSK基础函数
  include/app_config.hpp      配置读取和检查
  launch.ps1                  进程启动、错误日志、退出清理
scripts/build-windows.ps1     编译入口
legacy/                       旧实验命令，日常图传不用看
```

两端独立开发、生成不同exe，共用同一份协议和波形，防止修改后不兼容。复制/克隆时保留整个仓库，不要只复制其中一个文件夹。

## 第一步：两台电脑更新和编译

安装Visual Studio C++桌面开发、CMake、匹配设备的UHD x64 C++ SDK。FFmpeg/FFplay需加入PATH，或在各端配置里填写exe完整路径。

在Developer PowerShell中，进入仓库根目录：

```powershell
git pull --ff-only
.\scripts\build-windows.ps1 -WithUhd -UhdRoot 'C:\Program Files\UHD'
```

成功后生成 `build/video_transmitter.exe` 和 `build/video_receiver.exe`。修改C++源码后重新运行这条构建命令。**修改config.ini不需要重新编译，只需停止再启动。**

没有UHD的开发电脑可省略`-WithUhd`编译、测试，但这种程序会明确拒绝`backend=usrp`，不会悄悄改走UDP。

## 第二步：只改各自config.ini

### 发送电脑

先列出摄像头（不会录制）：

```powershell
.\transmitter\list-cameras.ps1
```

将视频设备的精确名称填入 `transmitter/config.ini` 的 `camera_name`。再列出该摄像头支持的格式：

```powershell
.\transmitter\list-cameras.ps1 -Formats
```

### 两端必查的参数

| 配置项 | 填什么 |
|---|---|
| `backend` | 正式图传保留 `usrp` |
| `device_args` | 此电脑连接的USRP，例如 `addr=192.168.10.2` |
| `frequency_hz` | 两块子板都支持的实际测试中心频率，单位Hz，两端必须相同；0不能启动 |
| `sample_rate` | 两端必须相同，默认10000000，即10 MS/s |
| `antenna` | 根据该子板probe结果填写发送/接收端口，不能猜测 |
| `gain_db` | 对应端的增益，起始0只是配置值，不等于0 dBm发射功率 |
| `channel` | 默认逻辑通道0；仅在你确认通道映射后修改 |
| `uhd_bin` | 对应电脑UHD的bin目录 |

发射端视频参数：默认1280×720、30fps、3Mbps，用于先跑通。摄像头支持1080p30、链路稳定后，可以设置 `width=1920`、`height=1080`、`fps=30`、`bitrate_mbps=6`。分辨率和帧率必须是摄像头支持的采集组合。

`ffmpeg`/`ffplay`可填完整exe路径；路径本身不要再加引号。`video_port`通常不用改。`test_link_address`只给明确的UDP软件测试使用，正式空口模式忽略它。

## 第三步：先检查配置，再启动

两台电脑分别执行（此操作不会打开摄像头或USRP）：

```powershell
# 发送电脑
.\transmitter\start.ps1 -CheckOnly
# 接收电脑
.\receiver\start.ps1 -CheckOnly
```

检查通过后，**先启动接收端**：

```powershell
.\receiver\start.ps1
```

然后在**发送电脑**启动：

```powershell
.\transmitter\start.ps1
```

发射端使用你配置的真实摄像头；接收端打开视频窗口。默认持续运行，按Ctrl+C停止。终端每秒显示业务速率、包数、无效包和射频错误计数。日志位于 `out/transmitter-时间/` 或 `out/receiver-时间/`。

如果重启了发射端，也重新启动接收端；当前接收端锁定首个会话，避免旧任务混入。

## 出错看哪里

| 现象 | 检查 |
|---|---|
| 尚未填写频率/天线/摄像头 | 按错误文字修改对应config.ini |
| `UHD not compiled` | 重新使用 `-WithUhd` 编译 |
| 没有生成exe | 看构建时第一条编译错误，不运行旧exe冒充新版本 |
| 摄像头打不开 | `camera.err.log`；设备名称、格式、Windows相机权限、是否被其他应用占用 |
| 找不到USRP/版本不匹配 | `link.err.log`；IP、网口、UHD与FPGA兼容 |
| 收不到CRC有效包 | 两端实际频率/采样率、端口、射频接线/天线、增益和overflow |
| 收到数据但没有画面 | `player.err.log`；码流丢失/等待关键帧、播放器与视频端口 |
| 运行一段时间变差 | 当前仍需验证独立采样时钟下的长期稳定性，不能只加缓存掩盖 |

首次仍建议适当衰减的有线射频回环排除环境因素，再换成天线空口；发射频率、功率按实验室许可条件设置。**本程序没有把单台USRP同频全双工作为前提。**

## 新手从哪里读代码

先读 `transmitter/transmitter_app.cpp` 和 `receiver/receiver_app.cpp`，每个文件只负责一侧流程；不要从FFT开始。更多修改指引见 [二次开发地图](docs/DEVELOPMENT.md)。

## 开发自检（不冒充空口测试）

```powershell
.\build\streamlink.exe selftest
python tests/endpoints.py
python tests/live_video_smoke.py
```

第一项回归已有协议/OFDM；第二项验证新双端程序和停止机制；第三项通过显式`udp_test`和测试图验证视频链路。测试不会偷偷调用摄像头，也不能证明射频性能。

旧的四进程实验教程保存在 `docs/LEGACY_README.md`。它不再是实时摄像头图传的操作入口。
