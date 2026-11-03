# X310接入与RK3588迁移

## 当前已知

- 用户共有三台X310；子板型号未知。
- 本机探测：Realtek Gaming 2.5GbE网卡，当前链路协商为1 Gbps。
- 本次没有成功连接/探测X310。未更改网卡IP、设备IP、FPGA镜像或射频配置。
- UHD工具不在PATH，可选 `src/usrp.cpp` 未在已安装UHD SDK下编译验证。核心程序不依赖UHD。

## 第一步：识别设备

安装/配置与X310镜像兼容的Ettus UHD主机驱动，在终端确认 `uhd_usrp_probe` 可调用。

```powershell
.\scripts\probe-usrp.ps1 -DeviceArgs 'addr=192.168.10.2'
```

把 `out/hardware-probe.txt` 中的子板名称、频率范围、设备序列号和版本用于配置。三台设备需要不同IP，PC网卡与之同子网；具体值以实验室现有配置为准，不要盲目复制默认IP覆盖现有设备。

## 第二步：单独测IQ吞吐

使用UHD自带 `benchmark_rate`，先分别RX和TX测试1、5、10 MS/s，再运行项目。验收无持续overflow/underflow。参数以安装版本的 `--help` 为准。

sc16单通道原始IQ：5 MS/s=160 Mbps，10 MS/s=320 Mbps，20 MS/s=640 Mbps，均未计以太网开销。网线协商速率不是业务Goodput；千兆连接不意味着必须把业务速率限制到几kbps。

## 第三步：构建UHD桥接

Linux或配好UHD的Windows开发者终端：

```text
cmake -S . -B build/uhd -DSTREAMLINK_UHD=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/uhd --config Release -j
```

必要时通过 `CMAKE_PREFIX_PATH` 指向UHD安装目录。Windows优先使用匹配SDK的MSVC工具链。桥接使用官方 `multi_usrp`、`fc32/sc16` streamer接口，但**当前仅有源码，尚未通过真实SDK编译和设备联调**。

## 第四步：两机有线衰减回环

选择两块子板均支持的频率。TX与RX之间必须按输出功率及RX额定输入选择足够衰减，不能直接连接高功率发射输出和接收输入。先最低适当增益、小速率验证。以下FREQ_HZ必须替换为实际设置：

```text
# 接收PC：先启动业务接收，再启动RF接收
streamlink rx --listen 127.0.0.1:9002 --seconds 60 --csv radio-rx.csv
streamlink_usrp rx --args addr=RX_IP --rate 1000000 --freq FREQ_HZ --gain 0 --seconds 60 --to 127.0.0.1:9002

# 发射PC：先启动RF发送，再启动业务源
streamlink_usrp tx --args addr=TX_IP --rate 1000000 --freq FREQ_HZ --gain 0 --seconds 60 --listen 127.0.0.1:9001
streamlink tx --to 127.0.0.1:9001 --mbps 0.5 --seconds 30
```

必要时加 `--antenna`，具体端口名称看设备probe。两端必须核对打印的actual_rate相同。由1 MS/s逐步提升到5/10 MS/s，先观察CRC、接收overflow和发送异步错误事件。

当前QPSK帧固定9856个采样，最大1400字节应用数据，理想业务上限为：

- 1 MS/s：1.136 Mbps。
- 5 MS/s：5.682 Mbps。
- 10 MS/s：11.364 Mbps。

视频每包1316字节时，10 MS/s理想约10.682 Mbps，尚未扣软件间隙、丢包或重传。5 MS/s不够稳定承载6 Mbps视频。当前波形不含FEC、不是Wi-Fi协议、没有已验证的20 Mbps空口模式。

两端共用参考时钟与独立时钟须分别验收。同步算法的粗频偏无模糊范围约为±Fs/128；大频偏、采样时钟偏差、深衰落/强多径的能力尚未验证。

## RK3588迁移

1. 在板上Linux先原生CMake编译 `streamlink`，运行selftest和有线UDP基准。
2. 保持PC运行USRP基带，把RK3588作为视频业务源，通过相同UDP接口互通。
3. 用适配板卡系统的Rockchip MPP编码器替换PC的libx264；协议接口不变。
4. 测量编码器延迟、CPU、温度、内存和长期稳定性。
5. 再决定是否将基带迁移到板卡：先比较完整调制/同步/解码的实时处理率，不以NPU TOPS推断基带能力。
6. 如果板卡CPU不能稳定实时处理，把性能热点优化为SIMD/并行流水线，或评估RFNoC/FPGA；不以不断增加缓存掩盖处理不足。

参考：
- https://files.ettus.com/manual/page_usrp_x3x0.html
- https://github.com/EttusResearch/uhd/blob/master/host/include/uhd/usrp/multi_usrp.hpp
- https://github.com/EttusResearch/uhd/blob/master/host/examples/tx_samples_from_file.cpp
- https://github.com/rockchip-linux/mpp
