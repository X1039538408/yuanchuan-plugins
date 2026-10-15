# 视频运行状态文件

## 保存位置

```text
%USERPROFILE%\\.seedance-director\\video-runs\\<runId>.json
```

## 字段

```json
{
  "schemaVersion": 1,
  "runId": "...",
  "stage": "awaiting_final_confirmation",
  "profile": "account-3",
  "projectUuid": "...",
  "promptSha256": "...",
  "idempotencyKey": "seedance-...",
  "assetRefs": [],
  "model": "Seedance 2.5",
  "modelKey": "star-video2.5",
  "schemaHash": "...",
  "schemaFetchedAt": "...",
  "modeType": "singleImage2video",
  "parameters": {},
  "taskId": null,
  "nodeKey": null,
  "lastNode": null,
  "error": null,
  "createdAt": "...",
  "updatedAt": "..."
}
```

不保存凭据内容。用户说“继续”时读取该状态文件，根据 `stage` 和已有 `taskId` / `nodeKey` 恢复。缓存仍放在 `%USERPROFILE%\\.seedance-director\\video-flow\\cache`，与运行状态分开。
