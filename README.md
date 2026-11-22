# FM-1 虹彩金属固件

[打开在线刷机器](https://colinxio.github.io/fm1-liquid-metal/) · [刷机风险与恢复指南](https://colinxio.github.io/fm1-liquid-metal/recovery.html)

这是基于 [SLOOP 2.8.5](https://github.com/isod89/sloop-fm1/releases/tag/v2.8.5) 的非官方简体中文改版。包含虹彩金属背景、按键和第四鼓轨的壁纸联动、呼吸灯，以及梨形开机动画。公开版不含上游保留权利的图标图集，相关界面使用文本布局。

## 在线安装

在 macOS 或 Windows 用新版 Chrome 或 Edge 打开刷机链接。停止设备播放和录音，备份数据，用 USB 数据线直连一台 M-VAVE FM-1，然后点击「连接并刷入」并授予 MIDI 权限。页面会从本站读取、校验固件，并检查重启后的应用版本。

刷机有数据丢失或设备暂时无法启动的可能，请先阅读恢复指南。网页只支持这里发布的 FM-1 更新包。原厂回退和完全无 USB 枚举的硬件恢复请按指南处理。

## 固件源码

`firmware-source/` 是本次公开版的对应源码，含编译、打包脚本和授权说明。请参阅 [公开版构建说明](firmware-source/BUILD-PUBLIC.md)。刷机页复用上游 GPL-3.0-only 的 Web MIDI 更新协议代码。保留 [许可证](LICENSE) 及各文件的版权声明。

部署源为 `main` 分支的 `/docs`。静态页面、固件、版本清单均在该目录。
