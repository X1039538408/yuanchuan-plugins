# AE 智能分层

Windows After Effects 脚本 · **portable14-native-text** · 发布包 **1.0.0**

把选中的图片、预合成画面或受支持的 AE 原生文字拆成独立图层。无需参数面板或开始按钮，可混选批量处理，显示可取消进度。

> **验证边界：图像 worker 101 项回归及 AE 宿主 mock 通过；尚无 AE25 真机排版或 ScriptUI 视觉测试。** 请先在工程副本上试用，不承诺任意字体、素材或宿主异常下无损。

## 下载

- [一键安装 EXE](<AESmartLayers-Setup-1.0.0.exe>) · [独立卸载 EXE](<AESmartLayers-Uninstall-1.0.0.exe>)
- [免安装完整 JSX](<AE 智能分层.jsx>) · [源码 ZIP](<AESmartLayers-1.0.0-source.zip>) · [SHA-256 清单](<SHA256SUMS.txt>)

EXE 使用系统 .NET Framework 4.x / WinForms，面向 Windows x64；不捆绑或下载运行时。**尚未数字签名**，Windows 可能提示未知发布者。请核对仓库来源和SHA-256并遵循组织安全策略，不要关闭系统防护。Github若打开文件详情页，请使用其 Download raw file 下载，不要把HTML页面另存为EXE。

## 先看这里

- [安装](<INSTALL.md>) / [卸载](<UNINSTALL.md>)
- [使用与完整限制](<USAGE.md>)
- [图文教程](<tutorial/README.md>)：安装、运行、多选、进度、结果与卸载示意
- [验证记录](<VERIFICATION.md>) / [源码与构建说明](<source/README.md>)

## 能做什么

| 输入 | 输出 | 关键边界 |
| --- | --- | --- |
| AE 原生文字 | 真正单字/受支持字符组合的可编辑文字层 | 仅静态、水平、从左到右的点文字；需 AE24.3+ `CharacterRange`，面向 AE25 |
| 图片 | `[文字]`、`[非文字]` 或 `[类型待确认]` 的图像蒙版层 | 不是 OCR；“文字”分类不会变成可编辑字 |
| 预合成 | 当前对应源帧的图像蒙版层 | 不展开内部图层、不跨帧跟踪、不自动冻帧 |

分离花瓣、图标等非文字前景可参与分区；**粘连、重叠、遮挡不保证按实例拆开**，复杂背景也不保证可抠。

## 三步开始

1. 运行 `AESmartLayers-Setup-1.0.0.exe`，定位 AE 根目录或 `Support Files` 并按界面完成安装。也可不安装，直接运行发布包中的自包含 JSX，见[安装说明](<INSTALL.md>)。
2. **先自行保存工程**；打开目标合成、停在目标帧，选择 1–20 个符合要求的源层。
3. AE 中选择 **文件 → 脚本 → 运行脚本文件…**，打开 `AE 智能分层.jsx`。脚本自动开始；整批成功后源层隐藏保留、输出全部选中。

源须无父级、静态二维、基本变换无关键帧/表达式、Position 未分离；正在提供轨道遮罩的源会被拒绝。每批最多 **20 源 / 120 输出 / 12000 轮廓点**；图片/预合成每源最多 40 区，原生文字每源最多 120 个可见字符。

## 图文预览

![使用流程示意，非AE真机截图](<tutorial/images/usage-flow.png>)

![合成夹具实际worker结果，非用户素材、非AE截图](<tutorial/images/synthetic-worker-regions.png>)

上图由自绘几何花瓣及 `THE` 合成测试夹具重建，右侧取自实际嵌入 worker 的区域结果；不是用户实物，也不是 AE 运行截图。可下载[6花瓣输入](<tutorial/images/petals-input.png>)与[混合输入](<tutorial/images/petals-mixed-input.png>)自行试验。

## 安全与环境

- 仅 Windows。图像路径需 Windows PowerShell 5.1、Windows Script Host 和 AE 脚本文件操作权限；纯原生文字批次不启动 PowerShell，但仍用临时目录。
- 不自动更改 AE 安全偏好，不强制重启 AE，不自动保存工程。文件权限被阻止时由用户核实后手动设置，勿关闭系统安全防护。
- 处理期间不要改源、源合成、时间映射、蒙版/效果，不要 Undo/Redo 或切换工程。
- 取消/关窗会请求整批清理；同步测量、快照和写层不能即时打断。不可恢复宿主异常仍需人工检查。
- 本教程不附用户提供的真实 `SECOND` / `THE THIRD` 图片；其发布授权未确认。

## 发布包内容

安装器 `AESmartLayers-Setup-1.0.0.exe`、卸载器 `AESmartLayers-Uninstall-1.0.0.exe`、本套中文文档与[源码目录](<source/README.md>)。安装脚本目标为 `Support Files/Scripts/AE 智能分层.jsx`，不是 `Plug-ins` 或 `ScriptUI Panels`。
