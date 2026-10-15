# 来源与发行组成

- `server/core.py` 与 `assets/default*.png`：用户提供的 FrameFit 视频套版 v7 软件及附带源码，按本次插件集成请求复用。原桌面界面没有进入发行包；合成算法保持一致。
- `server/engine.py`、MCP 接入、更新器、面板和发布工具：本仓库的 FrameFit 插件集成代码，适用根目录 LICENSE。
- Python、Pillow、MCP SDK、PyInstaller 及其运行依赖：通过固定构建版本安装。发行包 `licenses/python-packages/` 保留实际构建依赖的许可及元数据；`licenses/build-environment.txt` 记录版本。
- FFmpeg/FFprobe：Windows Gyan 构建，遵循 GPLv3。自动构建使用 `tools/media-dependency.json` 固定下载地址与 SHA-256；原软件工具首次本地构建使用原附带许可。发行包保留所选构建的 README、许可和源码获取说明。
- 原 FFmpeg 源码版本信息：https://github.com/FFmpeg/FFmpeg/commit/239f2c733d 。Gyan 构建与对应源码信息：https://www.gyan.dev/ffmpeg/builds/ 。

GitHub 仓库不包含用户视频、私人模板、原软件设置、日志、凭据或本机绝对路径。共享模板与预设显式维护在 `assets/` 和 `shared-presets.json`；个人预设存于本机状态目录。
