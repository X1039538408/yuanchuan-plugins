# 即梦双 CLI 账号适配

## 账号结构

导演插件使用本机即梦账号路由器。每个账号同时维护两种 CLI 的独立身份：

- `canvasProfile`：`dreamina-canvas.exe --profile` 使用的画布登录态；
- `conversationHome`：`dreamina.exe` 使用的独立 `USERPROFILE/HOME/APPDATA/LOCALAPPDATA`；
- 两者的地区字段分别保存；
- 画布登录成功不等于对话 CLI 登录成功。

当前本机入口是 `%USERPROFILE%\bin\dreamina-accounts.ps1`。账号索引位于
`%USERPROFILE%\.dreamina-accounts\profiles.json`。不要读取或复制 credential、Cookie、token、验证码或 device code。

## 选择规则

新任务必须先选择一个账号；同一任务的容器、素材、模型、生成和恢复沿用该账号。不要默认切到上次账号，不要把画布账号自动当成对话账号。

账号列表至少显示：

- 账号槽位和备注；
- 画布登录状态与服务端 userId；
- 对话 CLI 登录状态与 userId；
- 对话 CLI 剩余积分（可读时）；
- “余额暂不可读取”或具体错误。

套餐等级、会员等级和总额度不能冒充剩余积分。一个账号失败不影响其他账号。

## 命令边界

画布调用统一通过 `-Adapter canvas`，由路由器注入 `--profile`、`--region`、JSON 输出和独立状态目录。对话调用统一通过 `-Adapter conversation`，由路由器在隔离用户目录中启动 `dreamina.exe`，不接受画布 profile 参数。

任务恢复必须绑定原适配器、账号和容器。不能用画布 `projectId` 查询对话 `submit_id`，也不能用对话 `sessionId` 查询画布 `operationRef`。

## 登录

对话 CLI 的登录和画布 CLI 的登录分别执行。首次使用对话适配器时，如果账号没有对话登录态，展示官方 OAuth 授权地址并等待用户完成；授权完成后再次执行状态验证。不要用退出再登录模拟账号切换。
