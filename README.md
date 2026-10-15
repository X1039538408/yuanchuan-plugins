# yuanchuan 插件市场

用于通过 GitHub 分享 Codex 插件。首个插件为 **Seedance 创意导演 0.1.24**。

## 添加市场

仓库发布后，在 Codex「插件 → 添加插件市场」中填写：

- 来源：`X1039538408/yuanchuan-plugins`
- Git 引用：`main`
- 稀疏路径：留空（插件与目录文件均需获取）

也可以使用 Codex CLI：

```sh
codex plugin marketplace add X1039538408/yuanchuan-plugins --ref main
```

添加市场后，在该市场内安装「Seedance 创意导演」，在新聊天中选择插件并提供参考图和脚本。

## 使用条件

- 提示词共创使用宿主模型；图片理解取决于宿主能力。
- 本地配置面板需要 Node.js，视频控制器需要 PowerShell 7（`pwsh`）。
- LibTV 或即梦视频生成需要各自的 CLI／插件和使用者自己的账号授权，可能消耗对应平台积分。
- 即梦多账号面板还需要使用者配置 `%USERPROFILE%/bin/dreamina-accounts.ps1`；本仓库不捆绑账号路由器、CLI 安装程序或登录资料。
- 未准备这些视频依赖时，可以先使用提示词共创功能。详细说明见插件内 README。

## 获取更新

本市场跟踪 `main`。维护者推送新版本后，使用者可通过客户端提供的刷新／更新入口获取，或运行：

```sh
codex plugin marketplace upgrade yuanchuan-plugins
```

更新后必要时重启客户端并新建聊天。客户端何时自动检查更新取决于版本；此仓库不承诺即时推送或强制自动更新。将 Git 引用固定为 tag 或提交会停止跟随 `main`。

## 维护发布

1. 修改 `plugins/seedance-creative-director` 中的发布源码，不编辑 Codex 安装缓存。
2. 在 `plugin.json` 和 `.codex-plugin/plugin.json` 中同步递增版本号。
3. 从插件目录运行 `python tools/validate_package.py`（需 PyYAML）和 `node server/tests/mcp_contract.test.mjs`。
4. 更新 CHANGELOG，审查待提交文件后提交并推送到 `main`。
5. 用另一份安装验证市场刷新和插件更新；结构检查不能代替实际客户端验收。

新增插件时，在 `.agents/plugins/marketplace.json` 的 `plugins` 数组添加条目，路径从仓库根目录计算。

插件的上游来源与许可见各插件目录下的 SOURCES.md 和 LICENSE。
