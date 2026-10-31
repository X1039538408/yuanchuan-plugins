# yuanchuan 插件市场

一个用于集中分享、安装和维护 Codex 插件的仓库。每个插件拥有独立目录和使用说明，市场目录统一管理上架入口；插件更新发布到 `main` 后，使用者可以刷新市场获取新版。

## 插件目录

| 插件 | 简介 | 介绍与使用 |
| --- | --- | --- |
| Seedance 创意导演 | 从图片和脚本共创中文视频提示词，兼顾创意补全、意图继承与动作连续性。 | [详细介绍](docs/seedance-creative-director.md) · [使用与依赖](plugins/seedance-creative-director/README.md) |
| 视频套版 FrameFit | Windows 本机批量横竖转换与图片套版，支持预览、GPU／CPU 导出和处理功能自动更新。 | [详细介绍](docs/framefit.md) · [使用与依赖](plugins/framefit/README.md) |

各插件的功能、依赖、账号要求和许可请查看对应页面。

## After Effects 插件

| 插件 | 简介 | 下载与使用 |
| --- | --- | --- |
| 效果控制台 | Windows x64 原生插件，提供中文／拼音效果搜索、快捷槽和合成快照，当前为 1.0.19 测试版，附安装/卸载器与源码。 | [详细介绍](docs/xfx-native-console.md) · [一键安装器与说明](other-plugins/after-effects/xfx-native-console/README.md) |
| AE 智能分层 | 原生文字逐字可编辑，图片/预合成当前帧拆文字与花瓣等非文字；提供 EXE 安装/卸载、完整源码及图文教程，1.0.0 测试版。 | [详细介绍](docs/ae-smart-layers.md) · [安装包与图文教程](other-plugins/after-effects/ae-smart-layers/README.md) |

此类插件通过 EXE 安装到 After Effects，不通过下方 Codex 市场入口安装。

## 添加市场

在 Codex「插件 → 添加插件市场」中填写：

- **来源**：`X1039538408/yuanchuan-plugins`
- **Git 引用**：`main`
- **稀疏路径**：留空

添加后，在本市场选择需要的插件安装，并在新聊天中启用。

也可以使用 Codex CLI：

```sh
codex plugin marketplace add X1039538408/yuanchuan-plugins --ref main
```

## 获取更新

使用客户端提供的刷新／更新入口，或运行：

```sh
codex plugin marketplace upgrade yuanchuan-plugins
```

必要时重启客户端并新建聊天。市场跟踪 `main`，不承诺即时自动更新；固定到 tag 或提交后将不再跟随主分支。

FrameFit 的视频处理程序、面板及共享模板／预设另通过公开稳定版本清单，在准备新任务时检查并更新；正在处理的任务固定使用原版本。插件技能、名称和 MCP 接口仍通过上述市场更新。

## 贡献插件

欢迎提交新插件或改进已有插件。请先阅读 **[贡献指南](CONTRIBUTING.md)**，再通过 Pull Request 提交，由维护者审核合并。

- 一个插件放在 `plugins/<plugin-name>/`，独立维护版本、说明和依赖。
- 首页只保留市场说明和插件索引，详细介绍通过链接访问。
- 新插件需登记到市场目录，通过审核合并后才会上架。

[市场目录文件](.agents/plugins/marketplace.json) · [贡献指南](CONTRIBUTING.md) · [更新记录](CHANGELOG.md)
