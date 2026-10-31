# 1.0.21 默认效果目录与注册清单

## 根因与全目录审计

AE的已注册效果清单不是“当前应向用户提供的新效果”清单。此前原生插件读取真实category后丢弃，全部分类成“效果”，只按matchName去重。结果旧项目兼容效果、隐藏转场、内部辅助控件也被暴露出来，既出现同名，又可能在点击时无法添加。

2026-10-29审计AE25.4x86快照：1,401行、1,401个不同精确matchName，**没有真正重复ID**；有68组同名、每组两个不同ID（49组BCC、17组Adobe、2组内部控件）。例如“变换”为`ADBE Geometry`（`_Obsolete`）与`ADBE Geometry2`（扭曲）；Photoshop投影为`ADBE PSL Drop Shadow`，分类`_Obsolete`，不是正常投影`ADBE Drop Shadow`。

## 修复后的默认目录策略

- 在原生枚举入口使用宿主原始category，不依赖翻译后的名字或截图列表。
- 精确category为`_Obsolete`的90项不进入默认可添加目录；空分类内部/辅助注册108项亦不进入（其中1项显示名也为空）。读取分类失败或ID/显示名为空时不展示不可靠元数据。
- 快照剩余1,203项，68组原有显示名冲突全部消失。**这是此快照的目录策略统计，不是1,203项逐项添加/渲染验收。**
- 117项`BCC Obsolete`、10项中文“过时”分类仍保留。它们是可见旧版分类，不能用包含Obsolete或“过时”的字符串规则整批删掉。
- 不屏蔽任意下划线分类（如`_Vendor`），不按显示名/中文译名合并不同稳定ID，不修改应用ID，不把旧收藏映射成“看起来同名”的新版。
- 原始分类保存在`CatalogItem.host_category`；`category`继续提供“效果/预设”UI筛选，不破坏现有过滤器。
- 所有1,401项注册记录与译名仍保存在只读清单/回归夹具，方便诊断和旧项目理解；并未删除插件、词库行或用户数据。

本地SDK明确区分PF的隐藏obsolete标记与仍可显示/添加的deprecated标记，但AEGP没有读取已安装效果PF全局标记的API。**本策略不能写成“Adobe保证所有空分类或所有下划线分类都不可用”**；仅采用精确隐藏桶与未分类辅助项的保守默认发现策略。

## 应用时二次检查

点击效果、快捷槽或缓存结果时，生成的JSX重新核对本次AE的`app.effects`精确matchName和目录分类，已卸载/隐藏/内部条目在创建undo组和修改图层前拒绝，并给出标识及刷新提示。不因为显示名相同就换一个效果。

每个选中图层仍单独通过`ADBE Effect Parade.canAddProperty(matchName)`预检；拒绝时区分没有效果属性组和当前图层/插件状态不支持，返回精确ID，保留部分成功及撤销组平衡行为。

目录可见性、图层可添加性、授权状态和真实渲染是不同问题。不能仅据一个摄像机/音频图层的canAdd失败就把某效果从所有场景全局删除。

## 验证证据与限制

- Core整份实际清单回归：1,401→1,203，精确隐藏/空分类规则、当前变换保留、Photoshop旧项不公开、正常投影保留、可见弃用效果保留，合成的同名不同有效ID和`_Vendor`仍可发现。
- Native UI整份词库加载、原名保留与目录数量回归；AE Script VM验证隐藏/内部/已卸载预检在undo/应用前拒绝，正常弃用类别保留、用户选择快照、部分成功、错误和注入边界。
- `audit-effect-catalog.cjs`输出所有被排除条目及原因，保留同名有效ID碰撞供人工核查；没有静默按名字去重。
- 新增只读`export-effect-availability.jsx`：仅找现有图层调用canAddProperty，不新增合成/图层、不应用效果、不变选择、不打开/保存工程。无可用图层或调用失败均输出null，不能冒充false。VM测试验证不访问变更API。
- 早期探针受AE“模式对话框正在等待回应”状态阻塞，`AfterFX -r`与文件选择器提交都不能当成执行成功。用户自行退出/重开后，隔离启动保护脚本检测到已有/脏工程并拒绝创建临时数据（状态记录为REFUSED），未修改该工程。没有终止任何AE，没有批量应用效果。
- **随后真实AE检查成功**：用户选中普通AV图层并手动运行只读检测脚本，取得AE25.4x86的1,401项完整报告。上下文为AVLayer、index=1、hasVideo=true、hasAudio=false、locked=false。全部1,401项canAddProperty返回true，false/unknown/调用错误均0；当前默认目录1,204项，同名冲突0，目录内拒绝项0。
- 最新注册集比早先快照增加`CROSSPHERE_Bokeh`、移除临时`Pseudo/MHAC PrCtrl NWX 10`，所以当前默认目录为1,204而不是离线历史夹具的1,203。未用固定总数驱动运行时过滤。
- **重要限制**：隐藏兼容项也返回canAdd=true，证明这个API只检查该父组允许添加，不足以认定条目应公开，也不保证真实addProperty、第三方授权或渲染成功。未对1,401项逐一实际应用/渲染；不能把报告写成“所有插件可用”。

## 自查命令

```powershell
node scripts/audit-effect-catalog.cjs artifacts/effect-inventory/ae-effects.json
node scripts/effect-catalog-tests.cjs
node scripts/effect-availability-tests.cjs
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-native-core-tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-ae-script-tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-native-ui-tests.ps1
```

需要进一步真实宿主检查时，在已有合成包含普通AV图层的工程中手动运行只读探针，报告写入`artifacts/effect-inventory/ae-effect-availability.json`。这不修改工程，但AE必须允许脚本写入诊断文件；脚本不更改这项首选项。
