# 原生效果控制台

Windows x64 AEGP 插件，使用 Win32 浮窗、GDI+ 图片处理和 AEGP ExecuteScript。plugin/ 是宿主集成，src/ 是独立可测的搜索、脚本生成、设置及图片处理，tests/ 是测试。

installer/XFX 中文控制台.jsx 提供 ScriptUI Panel/palette；extendscript_bridge.* 实现 Adobe TaggedData C ABI 和有限命令队列。AE IdleHook 消费请求、回传真实结果。实际 AEX 的 LoadLibrary 检查不初始化宿主，宿主未就绪时必须拒绝操作；不能将此检查当作真实 AE 集成验收。

1.0.17 的相机按钮打开 PNG 保存对话框，默认当前合成名，可修改文件名和位置；确认后才按完整分辨率渲染保存，取消不生成文件。设置中不再提供分辨率开关。快照图库、固定截图位置和清缓存入口已移除，旧版图片保留。PNG/JPG/剪贴板导出使用临时图片源，结束后清理。

快捷槽弹出菜单保留纵向顺序，主界面固定显示 1–9 九个槽位并从左到右排列，保留数字执行和自定义标签功能。

结果列表的蓝色选框跟随鼠标移动；左键应用当前插件并关闭控制台。右键菜单支持“置顶排序/取消置顶排序”，置顶顺序写入配置并优先显示。

每条结果右侧提供置顶、收藏和添加到下一个快捷槽按钮；鼠标停留在控件上会弹出功能说明。

搜索为空且显示快捷槽时，快捷槽结果固定显示 1–9 并按从左到右排列。窗口只有在鼠标长按边缘后才会进入拖动移动状态。

当前 SDK 已在 vendor/ae-sdk。使用 MSVC 2022 和 Windows SDK，运行 scripts/check-ae-sdk.ps1 与 scripts/build-native-plugin.ps1。输出 artifacts/native-plugin/AEGP/XFXNativeConsole.aex。

Release 静态链接 C++ 运行库，用户无需 .NET。安装方法与真实验证范围见根 README。
