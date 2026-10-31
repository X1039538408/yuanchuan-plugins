# 1.0.25 筛选居中及背景重绘

保持1.0.24首页、菜单图标、名称修复和输入修复。仅解决用户三图中的筛选对齐、收藏残影，以及效果/预设/最近列表底部漏背景。

## 实证与修复

- 原先label在整个combo内单独居中，arrow固定靠右；64×36实际内容ink为x21..57，整组偏右。改为文字advance＋5DIP间距＋8DIP箭头整体居中；保留原生ODS_COMBOBOXEDIT裁剪边界和DC状态，不改控件尺寸。修复后实际ink为x14..49，五个筛选项均通过中心容差，箭头笔画随DPI缩放。
- 原生filter CBN_SELCHANGE直接populate_results，只存在ListRedrawGuard。resize用SWP_NOREDRAW，子控件MoveWindow(FALSE)移动后，父窗口只有搜索区域被失效，旧footer像素没有保证重绘。新增一次覆盖完整populate过程的SearchRedrawGuard（置于列表身份切换前），内层ListRedrawGuard不会中途呈现，最终恢复所有可见子控件并同步整父子树重绘。已有外层search/settings事务保持单一owner。
- resize不复用旧尺寸像素（SWP_NOCOPYBITS），WM_SIZE只排队全父背景失效、不在布局中途同步呈现；圆角region缓存仅在SetWindowRgn成功后提交，失败保留下一次重试机会。没有添加layered窗口、颜色键或更改透明度。

## 回归方法与边界

新增native/tests/filter_surface_checks.h：先污染当前父窗口可见表面，再通过真实CBN_SELCHANGE切换效果→收藏→预设→收藏→最近→收藏，覆盖8→1→8及2→1→2、dark/light。检查布局完成后的真实DC像素（不调用PrintWindow或强制重画掩盖问题）、footer/gaps背景色、底部区域包含和高度。旧代码在收藏切换留下污染像素，断言红；修复后通过。截图直接BitBlt捕获实际表面，测试条目标为绘制测试，不是AE工程。

五筛选实际文字＋箭头ink整体居中；96/120/144/192为布局数学用例，不冒充这些DPI都有实际显示器测试。原有combo立即visible/print一致、偏移裁剪、焦点/键盘/展开关闭及尺寸回归保持。完整NativeUI增加此项回归，保留热键、IME、应用、菜单、目录中文、窗口生命周期和1440确定性输入光标消息检查。

测试窗口设置/收藏/最近均在隔离临时目录，用户词库、槽位和设置不变。原生窗口实测不等于已在真实AE宿主、多屏DPI、外部录屏环境完成验收；用户重开AE后仍需观察实际截图场景。
