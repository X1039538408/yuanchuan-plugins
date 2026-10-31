# 1.0.22 恢复版：仅保留输入框体验修复

按用户最终要求，完整以本地验证过的1.0.22源码快照为基础，撤回1.0.23的全部首页、筛选、快捷槽改版及新增交互。版本仍标1.0.22，但这是**带输入框修复的本机重建版**，不是原1.0.22 AEX逐字节副本；用本轮实际安装SHA辨识。

## 保留的唯一生产逻辑修改

- 父窗口及原生EDIT的WM_SETCURSOR在输入区域统一返回I形文字光标，避免父箭头与子I-beam竞争。
- 完整搜索面优先HTCLIENT，窗口边缘拖动不抢输入区域；留白点击转到实际EDIT文本位置。
- 命中矩形使用layout提交的缓存，不在cursor解析时调用GetDC/GETFONT重算布局。

其余应用交互保持1.0.22：原筛选下拉、空搜索九槽带项目名横排、快捷槽原菜单、原数字键/绑定规则、结果hover行为、9个独立图层样式按钮。1.0.23的独立数字按钮、5筛选按钮、槽摘要与常驻绑定/执行/备注/清空、重复添加/新绑定状态均已撤回。个人settings/词库不清空或迁移。

## 源码与验证

原151文件快照逐项SHA校验后恢复；1.0.23新增SHORTCUT-UX.md及旧shortcut_ux_checks.h移入忽略的artifacts/input-only-restore-20261030/before，不删除历史。额外差异限输入修复、输入测试/运行入口与本说明/源码清单。原AE应用脚本、词典、搜索、预设、菜单与槽位底层保持旧快照。

新增input_cursor_checks.h对整个输入面执行1440组固定坐标父/EDIT光标消息，严格断言GetCursor为I-beam及不触发拖动。该坐标覆写仅存在XFX_CONSOLE_TEST编译内，不控制用户物理鼠标；不宣称AE内部静止悬停已经实测。

完整Native UI最终通过：旧首页结构/菜单、原快捷槽、filter/theme combo、输入/IME、绘制、设置开合、热键、生命周期等。先前整体套件的timer/hover/hidden-control偶发失败与测试窗口被外部焦点关闭有关；夹具在绘制检查时auto_close=false，专用focus-away用例显式打开并验证关闭。**只改临时测试配置，不改变生产默认或用户配置**。所有中间失败保留在artifacts/home-restore-20261030，最终通过日志与截图在artifacts/input-only-restore-20261030。

原生截图home-restored.png与search-restored.png是隔离测试目录里的示例项目条目，不是用户工程截图。Core/AE脚本84项/Support/配置回归另有日志。用户实际AE鼠标主题、多屏DPI驻留表现仍需安装后观察。
