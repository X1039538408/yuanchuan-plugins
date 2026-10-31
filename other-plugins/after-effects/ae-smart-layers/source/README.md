# AE 智能分层 · 可重建源码

内核版本 portable14，安装发布包装 1.0.0。自有源码提供审阅、维护和复现，未另行声明开源许可；见 [许可说明](../LICENSE-NOTICE.md)。

## 布局
- 根目录3个完整JSX内容相同；正式入口为 [AE 智能分层.jsx](<AE 智能分层.jsx>)。
- [project](projects/AE图像自动分层/) 保存C#图像worker、字体模板、文字/宿主片段、嵌入构建器和测试。
- [installer](installer/) 保存.NET Framework 4安装/卸载C#及构建/隔离测试脚本。

## 构建脚本内核
在本目录运行（Python 3、Node.js用于开发，不是用户端依赖）：
```powershell
python 'projects/AE图像自动分层/build_worker.py' --check
python 'projects/AE图像自动分层/build_worker.py'
Copy-Item 'zexp_image_split_portable.jsx' 'AE 智能分层.jsx'
Copy-Item 'zexp_image_split_portable.jsx' 'zexp_image_split_core.jsx'
node 'projects/AE图像自动分层/tests/frontend.test.js'
python 'projects/AE图像自动分层/tests/worker_test.py' --public
```
图像测试需Pillow/numpy、Windows PowerShell 5.1及系统Arial/Times/黑体。`--public`运行96个人工合成案例和独立新进程；不会下载/嵌入字体，不分发私人参考图片。不要把这96项写成公开复现了全部101项。

## 构建EXE
使用系统.NET Framework编译器，无需下载构建器：
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File 'installer/build.ps1' -JsxPath 'AE 智能分层.jsx' -OutputDirectory 'installer/dist'
```
上面ExecutionPolicy只作用于该构建进程，不写系统策略。构建时JSX路径固定哈希检查；修改JSX需要在审阅后同步安装器的期望哈希，否则拒绝构建。独立卸载器先编译，再嵌入安装器。构建EXE未数字签名；时间戳可能令重建二进制哈希不同，需结合源码/嵌入payload验证，而不是宣称可复现字节完全相等。

安装/卸载测试入口以installer/内README及脚本为准；只用临时模拟AE目录，不要对真实AE路径运行破坏性测试。实际AE25排版、DPI与全部原生UI仍需真机验收。
