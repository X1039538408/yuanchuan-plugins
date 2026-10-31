# 安装（Windows）

[返回首页](<README.md>) · [卸载](<UNINSTALL.md>) · [使用](<USAGE.md>)

## 安装前

- 面向 Windows / AE25；原生文字需要 AE24.3+ 的 `CharacterRange` API。**当前没有 AE25 真机验收记录**。
- 图像/预合成分析需要系统 Windows PowerShell 5.1 和 Windows Script Host；纯原生文字批次不启动 PowerShell，但仍使用临时目录。
- 先自行保存工程，停止正在运行的分层任务。安装器不会保存工程，不强制关闭或重启 AE。
- 安装目录如果受系统保护，可能需要管理员权限；按系统提示处理，不关闭安全防护。

## 方法一：EXE 安装

1. 从本仓库对应发布包取得 `AESmartLayers-Setup-1.0.0.exe`，双击运行。
2. 按界面选择要安装的 **AE 根目录或 Support Files 目录**。安装器通过 `AfterFX.exe` 确认 AE 位置，避免选错工程目录、`Plug-ins` 或 `ScriptUI Panels`。
3. 核对目标路径后确认安装：

   ```text
   <AE根目录>\Support Files\Scripts\AE 智能分层.jsx
   ```

4. 阅读完成提示；若已有同名脚本且内容不同，先备份并核对，不要随意覆盖自己维护的文件。
5. 打开 AE，在 **文件 → 脚本 → 运行脚本文件…** 中选择上述 JSX。不依赖脚本菜单即时刷新，无需为此强制重启。

![安装流程示意，非AE真机截图](<tutorial/images/install-flow.png>)

## 方法二：免安装，直接运行 JSX

1. 在[源码目录](<source/README.md>)找到 `AE 智能分层.jsx`，保存到方便访问的位置。
2. 在 AE 当前合成中先选择符合要求的源层，再用 **文件 → 脚本 → 运行脚本文件…** 打开它。
3. 也可自行将该 JSX 复制到 AE 的 `Support Files/Scripts` 后用同一入口运行。只复制自包含 JSX 即可，不必把 C#、Python 或测试源码放入 AE。

“portable14”指脚本自包含，不表示免除 Windows/AE 权限或系统组件依赖。安装只负责放置脚本，不是 `.aex` 二进制插件安装，也没有必须停靠的开始面板。

## 同名冲突、权限与卸载入口

- 安装器内置固定脚本哈希，只接受本版JSX。目标已有相同内容时幂等更新记录；已有同名不同内容时**拒绝覆盖**，请先备份并自行移走冲突文件。
- 权限不足时明确失败，按提示关闭工具后右键“以管理员身份运行”重试；不自动提权。若使用另一个Windows账号执行，卸载记录归该执行账号。
- 安装成功后，在当前账号 `%LocalAppData%\AE Smart Layer Split\` 保留 `manifest.xml` 与 `uninstaller.exe`。也可使用发布包的[独立卸载器](<AESmartLayers-Uninstall-1.0.0.exe>)或再次打开安装器点击“卸载”。
- 发布包把构建得到的“AE 智能分层.exe”和“uninstaller.exe”分别命名为ASCII版本文件名，内容不变；安装到用户状态目录的卸载入口仍叫 `uninstaller.exe`。

## AE 文件操作权限

脚本和安装器**不会自动更改 AE 安全偏好**。若 AE 提示文件操作受限，请用户核实后，在 AE **编辑 → 首选项 → 脚本和表达式**中检查“允许脚本写入文件和访问网络”（英文界面：Allow Scripts to Write Files and Access Network）；不同版本名称可能略有差异。遵循所在组织的安全策略。

安装后先在副本上使用[自绘测试输入](<tutorial/images/petals-input.png>)或少量静态点文字试运行；完整条件与取消行为见[使用说明](<USAGE.md>)。
