# LibTV 多账号配置适配

## 目标

在本机保存多个独立 LibTV 登录会话，并在视频生成前选择其中一个。每个账号使用独立 `LIBTV_CONFIG_DIR`，不会覆盖其他账号的 `credentials.json`。

官方 LibTV 远程 MCP 目前只支持一个 OAuth 会话，不能通过 `account_user` 传入另一个独立登录账号。因此多账号选择必须走本机 LibTV CLI 配置目录；不能把团队账户切换误认为独立登录会话。

## 配置目录

- 根目录：`%USERPROFILE%\\.libtv-accounts`
- 每个 profile：`%USERPROFILE%\\.libtv-accounts\\<profile-id>\\credentials.json`
- 当前旧版单账号目录 `%USERPROFILE%\\.libtv\\credentials.json` 可以复制到 `primary` profile，原文件保留不动。
- profile id 只使用小写字母、数字、短横线和下划线；不要把 token、手机号或验证码写进 profile id。

## 登录新账号

为每个账号创建独立目录后，在该目录执行登录：

```powershell
$env:LIBTV_CONFIG_DIR = "$env:USERPROFILE\\.libtv-accounts\\account-2"
libtv login web --open
```

登录完成后读取账号信息并保存非敏感元数据：

```powershell
$env:LIBTV_CONFIG_DIR = "$env:USERPROFILE\\.libtv-accounts\\account-2"
libtv account info
```

不要读取、展示或写入 `usertoken`、验证码、Cookie 或完整凭据内容。

## 选择与执行

1. 枚举 profile 目录。
2. 对每个 profile 设置 `LIBTV_CONFIG_DIR`，调用 `libtv account info` 获取账号名、当前账户和套餐标识。
3. 生成账号选择卡，显示 profile 名、账号名、当前团队/个人账户、套餐标签和登录状态。
4. 余额或剩余积分只有在 CLI/API 明确返回时才显示；没有字段时显示“剩余积分：暂不可读取”。
5. 用户选择后，所有 `libtv project`、`libtv node`、`libtv model` 和生成命令必须在该 profile 的 `LIBTV_CONFIG_DIR` 下执行。
6. 不修改全局 `%USERPROFILE%\\.libtv`，不混用 profile 的项目绑定文件。

## 账号切换边界

- `libtv account use` 只切换同一登录身份下的个人/团队账户，不切换独立登录账号。
- 多账号 profile 切换由 `LIBTV_CONFIG_DIR` 完成。
- 官方远程 MCP 工具调用仍使用当前 OAuth 会话；如果任务必须使用所选 profile，改走本机 CLI，并在摘要中说明使用了 CLI profile。
- 不能把 CLI profile 选择结果传给远程 MCP 的 `projectId` 或 `account_user`，也不能声称远程 MCP 已切换账号。

## 断线与安全

- 每个 profile 的登录状态独立检查；一个账号失效不影响其他 profile。
- 登录失败时只报告 profile id 和错误类型，不展示凭据。
- 生成前记录 profile id、项目 UUID、模型和幂等键；恢复任务时继续使用同一个 profile 目录。
- profile 目录是敏感本地状态，不上传插件包、不写入日志、不复制到项目仓库。

## 视频生成控制器绑定

- 视频生成固定流程使用 `tools/libtv_video_flow.ps1`。
- 控制器每个动作都重新设置选中的 `LIBTV_CONFIG_DIR`，不依赖当前 shell 的全局状态。
- profile 登录状态可缓存 10 分钟；用户切换 profile 或主动刷新时立即重新检查。
- 项目列表、模型目录和 schema 的缓存只保存非敏感元数据。
- 控制器返回的 profile 失效、项目列表失败或 schema 错误必须停在当前阶段，不自动切换其他 profile。
