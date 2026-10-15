# 即梦对话视频生成适配

该适配器只处理旧版 `dreamina.exe` 的对话会话和异步任务。

1. 在选定账号的隔离 `conversationHome` 中检查登录状态。
2. 使用 `session list` 列出最近 6 个“即梦对话会话”，显示会话名、整数 session ID 和更新时间；默认 `0` 显示为“默认对话”。
3. 使用 `user_credit` 读取剩余积分。字段缺失时显示“剩余积分：暂不可读取”，不能用会员等级替代。
4. 根据素材选择命令：无素材 `text2video`；单图 `image2video`；多图连续故事 `multiframe2video`；混合图像、视频、音频 `multimodal2video`。
5. 每次运行前读取目标子命令的 `-h`，从当前帮助解析模型、比例、分辨率、时长和素材限制。
6. 最终确认后提交一次，必须取得 `submit_id` 才报告已提交；保存 session、账号和 submit_id。
7. 使用 `query_result --submit_id` 恢复原任务，不能换新 submit_id 重试。

对话 CLI 的参数不能套用画布 schema；不支持的组合直接拦截，不自动换模型、比例、时长或命令。
