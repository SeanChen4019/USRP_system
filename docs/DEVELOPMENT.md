# 二次开发地图

## 一次只修改一层

| 想改变什么 | 修改位置 | 是否重新编译 |
|---|---|---|
| 摄像头、视频分辨率/帧率/码率 | transmitter/config.ini | 否，重启 |
| 接收频率、端口、增益 | receiver/config.ini | 否，重启 |
| 两端采样率/频率 | 两份config.ini，保持一致 | 否，重启 |
| FFmpeg编码策略、关键帧间隔、采集设备类型 | transmitter/start.ps1 | 否，重启 |
| 播放器缓冲和显示策略 | receiver/start.ps1 | 否，重启 |
| 发送分包、统计、业务队列 | transmitter/transmitter_app.cpp | 是 |
| 接收校验、去重、会话策略 | receiver/receiver_app.cpp | 是 |
| UHD设备配置和IQ流 | shared/radio_device.cpp | 是 |
| 数据协议字段/CRC | shared/include/core.hpp | 是，两端一起更新 |
| OFDM帧、同步、均衡、调制 | shared/include/modem.hpp及core.hpp | 是，两端一起更新 |

## 推荐的学习路径

1. 不改C++，先让两份config.ini通过检查；看懂其中每一个实际使用的字段。
2. 跑通当前配置，记录发送/接收包数和业务速率。一次只改变码率，比较结果。
3. 在发射端`pack(packet)`前、接收端`player_output.send()`前设置断点，观察payload、seq、session。
4. 断点暂停会导致真实USRP流溢出/欠载，因此调试业务逻辑时使用显式`udp_test`测试；不要把暂停造成的溢出当作射频问题。
5. 理解一包1316字节MPEG-TS如何被包装、变成IQ、还原并送给播放器，再进入OFDM算法。

## 共用层为什么不复制到两边

接收端必须精确理解发射端的协议和波形。复制两套CRC、帧结构和FFT后，只改其中一套会产生隐蔽的不兼容。两端业务代码独立，共用层只有一份，是为了让修改可追踪、可同时验证。

## 当前工程边界

- C++负责通信业务、协议、OFDM和UHD；FFmpeg/FFplay负责成熟的视频编解码功能，不把这部分第三方代码包装成自研成果。
- 当前接收处理与采样读取是顺序执行，实际硬件性能必须看overflow；后续可将采集和解调分成有界队列的流水线。
- 现有帧是固定QPSK/无FEC。要做MCS和FEC，必须先设计双方可解出的PHY头和反馈机制。
- 目前发送视频没有应用级重传。不要在视频上无限等待重传，否则会把丢包变成不断增长的延迟。
- 尚未实现采样时钟偏差跟踪、长期独立时钟验收、自适应视频码率。
- 根目录新入口用于摄像头图传；`legacy`保留早期实验，以便回归测试，不需要日常阅读。

## 参考

DirectShow设备名与格式枚举、采集参数： https://ffmpeg.org/ffmpeg-devices.html#dshow
UHD官方设备接口： https://files.ettus.com/manual/classuhd_1_1usrp_1_1multi__usrp.html
