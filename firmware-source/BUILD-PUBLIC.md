# 公开版构建说明

基于 [SLOOP 2.8.5 对应提交](https://github.com/isod89/sloop-fm1/tree/bac9c05ea7627449405aa10e48da21d75b6c8c56)。本目录含公开版应用和更新器源码。上游保留权利的图标图集没有放入本项目。构建时设置 `FELUCCA_ICONS=0`。

## 构建固件

工具链、SDK 版本及 Python 依赖参见 [BUILDING.md](BUILDING.md)。安装 Pillow 后从本目录执行：

```sh
FELUCCA_ICONS=0 ./build.sh
```

Windows 原生构建使用 [pi32v2 LLVM 23.1.6-1](https://github.com/jvitkauskas/pi32v2-llvm/releases/tag/v23.1.6-1)。将工具链展开后的 `pi32v2-llvm-23.1.6-1-windows-x64` 及名为 `sdk` 的 AC79 SDK 目录放在本目录旁。随后在本目录运行：

```powershell
$env:PYTHONUTF8 = '1'
python .\zh-build\build-sloop-native.py
```

公开包沿用 SLOOP 2.8.5 发布包里的更新加载程序。应用编译结果在 `build/felucca.fwsc`。从 [SLOOP 2.8.5 发布页](https://github.com/isod89/sloop-fm1/releases/tag/v2.8.5)取得标准 `.fwsc` 后，可用 `zh-build/package-sloop-zh.py` 依次传入上游包、当前构建包和输出路径。脚本验证上游包的摘要和引导区，并复用其更新加载程序。发布新二进制时需同步更新网站的 `docs/release.js`。

简体中文字体及词汇表来源为 `assets/fonts/sloop-zh-wqy12.hex` 与 `firmware/src/zh_terms.txt`。可用 `python tools/gen_zh.py` 重新生成对应头文件。字体授权参见 `assets/fonts/WenQuanYi-LICENSE.txt`，采样出处参见 `assets/samples-cc0/ATTRIBUTION.txt`。
