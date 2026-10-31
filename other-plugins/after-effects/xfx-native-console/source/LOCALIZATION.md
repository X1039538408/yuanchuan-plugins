# 1.0.20 中文标签覆盖与边界

基准日期：2026-10-29。来自本机 AE 25.4x86 的只读 `app.effects` 导出，共 **1,401 个不同 matchName**。回归夹具仅保留效果标识、显示名和类别，不含工程、媒体、设备路径、账号或用户设置。

## 本次修复

- 1.0.19 随包词典对这份清单仅495项有中文显示名，906项缺中文。此前“词典有660行”不是外部效果已覆盖的证据。
- 逐项按实际稳定ID补齐906项：新增904行；原有 EXtractoR、IDentifier 两行由仅保留产品名改为“产品名 + 中文功能”，保留英文原名与旧中文搜索别名。最终词典1,564行，包含本机未注册的历史/兼容条目。
- 覆盖 Sapphire 287项、BCC/Boris FX 323项、BCC+ 174项、Universe 105项，以及 Trapcode、其他 Red Giant、Video Copilot、独立厂商、旧转场和未分类效果。系列归类仍是启发式，不作为厂商身份认证。
- 上述1,401项现在都有匹配中文标签；这表示**此清单的中文显示/搜索覆盖**，不是全球全部版本效果覆盖、官方翻译或逐个渲染验收。
- 中文名保留品牌，原始英文/效果 matchName 不改；原名、中文、全拼与首字母搜索并存。已废弃与旧版条目不删除，ML/VR/3D/版本限定保留。
- 默认词库与编译内置表从同一TSV生成；`generate-effect-translations.cjs --check` 检查一致性。用户词库的覆盖优先级保持不变。
- 扩大词典后，内置译名键索引一次构建，不再对每个效果反复规范化扫描全表；不承诺未经测量的真实AE加速倍数。
- 修复稳定ID专用译名导致 `Lux`、`Tao` 等合法短英文名被当乱码替换的问题。

## 仍待确认的3项

| 稳定标识 | 当前中文标签处理 | 尚未确认 |
| --- | --- | --- |
| `ADBE TimeMachine` | TimeMachine 时间机器（功能待确认） | ADBE前缀不能证明属于Adobe；同名产品资料不能证明ID对应关系，不猜测具体功能或厂商 |
| `omino_test` | omino test 测试工具（用途待确认） | 安装版本的具体测试行为 |
| `Pseudo/MHAC PrCtrl NWX 10` | 保留原标识，附伪效果控件（用途待确认） | 原显示名/类别皆空；所属脚本、控件展开及实际用途 |

其他中文词条也属于便于搜索和理解的非官方描述性译名，抽象视觉命名和历史版本仍需用户在AE中核对；不以“匹配了汉字”证明翻译质量已由厂商审定。

## 回归范围

- `native/tests/data/ae-25.4-effects.txt` 是独立实际清单，不由词典推导期望结果。Native Core遍历全部1,401项，验证中文标签、拼音派生、原始应用标识保留，并跨清单抽样验证中文/全拼/首字母可检索到原ID。
- Native UI测试用完整随包TSV走实际原生词库加载与选择路径，遍历1,401项，并检查原始非空显示名不被改写；另测用户译名优先、BCC+空格兼容与未知效果保持原名。
- 导出、审计与生成器测试覆盖Unicode、写入失败、空清单、未知分类、冲突优先级、BOM/CRLF、引号和重复ID等边界。
- 测试窗口和静态词典审计不等于真实宿主内对1,401项逐个应用/渲染，亦不涉及效果参数面板、插件自带窗口、预设名或菜单的全局汉化。

## 维护

编辑 `installer/XFXNativeConsole.aliases.tsv`，随后运行：

```powershell
node scripts/generate-effect-translations.cjs
node scripts/generate-effect-translations.cjs --check
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-native-core-tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-native-ui-tests.ps1
```

新装/升级外部插件后请重新只读导出清单并审计，不能把本次清单覆盖率外推到其他机器。未知项继续保留原名，用户词库不应被升级覆盖。
