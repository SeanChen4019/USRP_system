# Historical prototype instructions

These instructions describe the earlier experiment CLI. Use the root README and transmitter/receiver folders for live video. Sources previously in src/ and include/ now live in legacy/src/ and shared/include/.

# StreamLink：C++ 实时通信实验系统

## 从 GitHub 获取并构建

```powershell
git clone https://github.com/SeanChen4019/USRP_system.git
cd USRP_system
# 在 Visual Studio 的 x64 Developer PowerShell 中运行：
.\scripts\build-windows.ps1
# 目标电脑安装 UHD C++ SDK 后，构建真实设备程序：
.\scripts\build-windows.ps1 -WithUhd -UhdRoot 'C:\Program Files\UHD'
```

Git 仓库包含源码、文档和测试，不包含本地 `build/`、`out/`、`tools/`、`release/`。因此下文提到的预编译程序、测试日志和随本地项目下载的 FFmpeg 不会随 `git clone` 下载；请先构建。视频演示需自行安装 FFmpeg/FFplay 并加入 PATH，或向脚本传入 `-FFmpeg`、`-FFplay` 完整路径。Windows 构建版本可从 https://www.gyan.dev/ffmpeg/builds/ 获取。

后续更新在项目目录运行 `git pull --ff-only`，然后重新构建。两台电脑分别使用 `streamlink_usrp tx` 和 `streamlink_usrp rx`；单台回环才使用 `duplex`。首次安装与两机接入见 [硬件说明](docs/HARDWARE.md)，Windows SDK 构建细节见 [Windows 迁移说明](docs/SINGLE_USRP_WINDOWS.md)。

**迁移到另一台Windows电脑、使用单台X310回环：优先阅读 [单机迁移说明](docs/SINGLE_USRP_WINDOWS.md)。** 已新增`duplex`单进程收发模式和通用Windows构建脚本；硬件模式仍待目标SDK编译/设备验证。

这是为 PC 开发、后续向 RK3588 迁移建立的 C++17 原型。当前完成了可运行的数据流、视频通路、OFDM 软件链路，以及可选 UHD 硬件桥接源码。

**状态必须区分：PC 数据链路和视频测试已通过；真实 X310 空口尚未联调；自动调制编码切换、FEC、自动视频码率闭环尚未实现。不能把本项目当前版本描述为已完成的工业级抗干扰通信系统。**

## 先看到效果

已编译程序：`build/streamlink.exe`，不依赖 Python 运行。Python 仅用于自动化验收脚本。视频编解码使用随项目下载的 FFmpeg，通信链路和 OFDM 用 C++ 实现。

在 PowerShell 中：

```powershell
cd 'E:\个人文件\实习\通信系统'
.\build\streamlink.exe selftest
.\scripts\benchmark.ps1
.\scripts\video-demo.ps1 -Seconds 60
```

第三条会打开视频窗口，用实时生成的 1080p30 动态测试图经过 H.264 编码、C++ 数据通路后播放。默认编码目标 6 Mbps。播放文件：

```powershell
.\scripts\video-demo.ps1 -Seconds 60 -InputFile 'E:\你的视频.mp4'
```

这两个演示使用本机 UDP 模拟链路，不经过 USRP。测试图和循环播放文件也不能证明摄像头到屏幕时延。

## 当前验证结果（2026-11-02）

| 测试 | 实测 |
|---|---|
| 20 Mbps 数据源→30 Mbps 模拟信道→接收端 | 实际发送平均约 19.85 Mbps；12,421,200 字节完整接收 |
| 重复包概率 20% | 有效数据完整接收；1176 个重复包被过滤 |
| 随机丢包概率 10% | 接收比例约 89.74%，无错误包被接纳 |
| 20 Mbps 数据源→8 Mbps 有限队列信道 | 超载时丢弃，接收比例约 39.15%；队列不无限增长 |
| 真实 OFDM 运算，10 MS/s 模拟时间，3 kHz CFO，30 dB SNR | 5 Mbps 输入的 3,123,600 字节完整恢复 |
| 1080p30、H.264、目标 6 Mbps | 经 C++ 链路连续解码 180 帧成功，使用无窗口解码验收 |

结果和原始日志分别在 `out/integration-20261102-211713/` 和 `out/video-smoke-20261102-211831/`。
这些短测试验证功能和初步吞吐，不等于完成 30 分钟稳定性、实际空口、射频干扰或端到端时延验收。

## 组成

```text
FFmpeg / 摄像头编码器
  → MPEG-TS/UDP (每个数据报≤1400字节)
  → streamlink ingress：帧头、会话、序号、CRC
  → 以下三种后端之一
      channel：带宽、时延、丢包、重复、有限队列模拟
      iq-channel：OFDM调制→频偏/噪声→捕获/均衡/解调
      streamlink_usrp tx → X310 → X310 → streamlink_usrp rx
  → streamlink rx：完整性校验、重复过滤、CSV、UDP输出
  → FFplay / 解码器 / RK3588解码显示
```

UDP 是应用和后端之间的接口。在 UHD 模式中，9001→9002 之间的业务数据通过 IQ 和射频传输；不应同时运行绕过射频的 `channel`。

| 文件 | 职责 |
|---|---|
| `src/main.cpp` | 命令行应用、数据源、接收、信道队列、统计 |
| `include/core.hpp` | 字节序协议、CRC、去重窗口、FFT、OFDM载荷内核 |
| `include/modem.hpp` | 无线帧、同步捕获、CFO、信道均衡、导频相位校正 |
| `include/net.hpp` | Windows/POSIX UDP 套接字封装 |
| `include/iq_channel.hpp` | 有真实基带运算的软件信道 |
| `src/usrp.cpp` | UHD收发和UDP↔IQ桥接；需SDK，尚未硬件验证 |
| `tests/` | 数据通路、基带和视频自动验收 |
| `docs/` | 指标定义、硬件接入、迁移与开发边界 |

## 构建

本机已经适配现有的 Clang 与 sysroot：

```powershell
.\scripts\build.ps1
```

通用 CMake（Windows 建议使用 Visual Studio 的开发者终端；Linux 使用 GCC/Clang）：

```text
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake --config Release -j
ctest --test-dir build/cmake -C Release --output-on-failure
```

Linux/RK3588 的程序名无 `.exe`。MSVC 需要与 UHD SDK 的 ABI 匹配；不要把 MSVC UHD 库直接交给 MinGW 链接。

## 单独运行各端

分别打开三个终端，先接收端，再信道，最后发送端：

```text
streamlink rx --listen 0.0.0.0:9002 --seconds 60 --csv rx.csv
streamlink channel --listen 0.0.0.0:9001 --to 127.0.0.1:9002 --mbps 20 --loss 0 --delay-ms 5 --queue-ms 100 --seconds 60
streamlink tx --to 127.0.0.1:9001 --mbps 12 --seconds 30 --payload 1200
```

替换第二条，测试基带链路：

```text
streamlink iq-channel --listen 127.0.0.1:9001 --to 127.0.0.1:9002 --rate 10000000 --cfo 3000 --snr 30 --seconds 60
```

`channel --mbps` 限制 StreamLink 数据报总速率（包括36字节协议开销），不代表物理层码率。`iq-channel --rate` 是基带采样率对应的最小帧间隔预算，实际处理也受CPU限制。

`rx` 默认锁定第一个有效会话。重启发送端后请重启接收端，避免混入上一次会话。可以指定 `--session <id>`。当前不对媒体流做乱序重排；序号用于去重，MPEG-TS通过自身连续计数识别丢失。

## 视频与摄像头

`video-demo.ps1` 的图案/文件演示不需要摄像头权限。真实摄像头需要先枚举设备：

```text
ffmpeg -list_devices true -f dshow -i dummy
```

随后把编码器输入换为 `-f dshow -i video="实际摄像头名称"`。摄像头实际支持的分辨率/帧率需验证。输出必须保留：

```text
-c:v libx264 -preset ultrafast -tune zerolatency -b:v 6M -maxrate 6M -bufsize 600k -g 30 -bf 0 -pix_fmt yuv420p -f mpegts "udp://127.0.0.1:5600?pkt_size=1316"
```

6 Mbps 是编码目标；封装、峰值和队列需要余量。`ingress` 拒绝超过1400字节的数据报，不会悄悄截断。

## 自动验收

```powershell
& 'C:\Python312（64bits）\python.exe' .\tests\integration.py
& 'C:\Python312（64bits）\python.exe' .\tests\video_smoke.py
```

UHD 接入步骤见 `docs/HARDWARE.md`。性能口径和下一阶段验收见 `docs/DESIGN.md`。
