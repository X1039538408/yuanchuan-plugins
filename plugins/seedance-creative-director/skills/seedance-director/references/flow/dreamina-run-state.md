# 即梦运行状态

状态目录：

```text
%USERPROFILE%\.seedance-director\dreamina-runs\<runId>.json
```

每条记录至少保存：`provider`、`adapter`、`profileId`、`containerType`、`containerId`、`model`、`mode`、参数摘要、素材绑定、`nodeId`、`operationRef`、`submitId`、阶段和时间戳。

禁止保存 token、Cookie、device code、credit token、完整凭据和签名下载 URL。`resume` 必须验证当前账号和适配器与原记录一致；“继续”只恢复原任务，不重新提交。
