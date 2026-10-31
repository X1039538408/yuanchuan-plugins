using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace AESmartLayerSplit {
    internal sealed class Options {
        public bool Install, Uninstall, Quiet, TestMode;
        public string Target, State = Engine.DefaultState;
        public static Options Parse(string[] args) {
            Options o = new Options(); bool customState = false;
            HashSet<string> seen = new HashSet<string>(StringComparer.Ordinal);
            for (int i = 0; i < args.Length; i++) {
                string arg = args[i];
                if (!seen.Add(arg)) throw new InstallException("重复参数：" + arg);
                switch (arg) {
                    case "--install": o.Install = true; break;
                    case "--uninstall": o.Uninstall = true; break;
                    case "--quiet": o.Quiet = true; break;
                    case "--test-mode": o.TestMode = true; break;
                    case "--target": if (++i >= args.Length) throw new InstallException("--target 缺少目录。"); o.Target = args[i]; break;
                    case "--state-dir": if (++i >= args.Length) throw new InstallException("--state-dir 缺少目录。"); o.State = Engine.Full(args[i]); customState = true; break;
                    default: throw new InstallException("未知参数：" + arg);
                }
            }
            if (customState && !o.TestMode) throw new InstallException("--state-dir 只允许与明确的 --test-mode 一起用于隔离测试。");
            if (o.TestMode && !customState) throw new InstallException("测试模式必须显式指定 --state-dir，避免写入真实卸载记录。");
            if (o.Install && o.Uninstall) throw new InstallException("不能同时安装和卸载。");
            if (o.Quiet && (!o.Install && !o.Uninstall || String.IsNullOrWhiteSpace(o.Target))) throw new InstallException("静默操作必须明确 --install 或 --uninstall，并显式提供 --target <AE根目录或Support Files>。");
#if UNINSTALLER
            if (o.Install) throw new InstallException("此程序仅供卸载，请运行本版本的安装器安装。");
#endif
            return o;
        }
    }
    internal static class Program {
        [STAThread]
        public static int Main(string[] args) {
            bool quietRequested = Array.IndexOf(args, "--quiet") >= 0;
            try {
                Options options = Options.Parse(args);
                if (options.Quiet) {
                    Result result = options.Install ? Engine.Install(options.Target, options.State) : Engine.Uninstall(options.Target, options.State);
                    Console.WriteLine(result.Message); return 0;
                }
                Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new MainForm(options)); return 0;
            } catch (Exception ex) {
                string message = "操作未完成：" + ex.Message + "\r\n如果目标位于 Program Files 且访问被拒绝，请右键以管理员身份运行后重试。";
                Console.Error.WriteLine(message);
                if (!quietRequested) MessageBox.Show(message, "AE 智能分层 — 未完成", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }
    }
    internal sealed class MainForm : Form {
        private readonly Options options;
        private readonly ComboBox versions = new ComboBox();
        private readonly TextBox path = new TextBox();
        private readonly Button browse = new Button();
        private readonly Button install = new Button();
        private readonly Button uninstall = new Button();
        private readonly Button close = new Button();
        private readonly TextBox status = new TextBox();
        private readonly Label stateTitle = new Label();
        private readonly Label destination = new Label();
        private readonly Label detected = new Label();
        private bool busy;
        public MainForm(Options options) {
            this.options = options;
#if UNINSTALLER
            Text = "AE 智能分层 — 卸载";
#else
            Text = "AE 智能分层 — 安装与卸载";
#endif
            Font = new Font("Microsoft YaHei UI", 9F);
            AutoScaleMode = AutoScaleMode.Dpi;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(750, 700); MinimumSize = new Size(690, 720);
            BackColor = SystemColors.Window;
            TableLayoutPanel layout = new TableLayoutPanel(); layout.Dock = DockStyle.Fill; layout.Padding = new Padding(24); layout.ColumnCount = 3;
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 16)); layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 94));
            layout.RowCount = 13;
            float[] heights = { 38, 28, 30, 34, 26, 34, 55, 53, 28, 0, 16, 38, 24 };
            for (int i = 0; i < heights.Length; i++) layout.RowStyles.Add(new RowStyle(i == 9 ? SizeType.Percent : SizeType.Absolute, i == 9 ? 100 : heights[i]));
            Controls.Add(layout);
            Label title = new Label(); title.Text = "AE 智能分层"; title.Font = new Font(Font.FontFamily, 17F, FontStyle.Bold); title.AutoSize = true; title.Dock = DockStyle.Fill;
            layout.Controls.Add(title, 0, 0); layout.SetColumnSpan(title, 3);
            Label sub = new Label(); sub.Text = "便携脚本 · " + Engine.ProductVersion + " · 无需下载依赖"; sub.AutoSize = true; sub.ForeColor = SystemColors.GrayText;
            layout.Controls.Add(sub, 0, 1); layout.SetColumnSpan(sub, 3);
            detected.Text = "检测到的 AE 版本（多版本请明确选择）"; detected.AutoSize = true; detected.Anchor = AnchorStyles.Left | AnchorStyles.Bottom;
            layout.Controls.Add(detected, 0, 2); layout.SetColumnSpan(detected, 3);
            versions.DropDownStyle = ComboBoxStyle.DropDownList; versions.Dock = DockStyle.Fill; versions.AccessibleName = "检测到的 AE 版本";
            versions.SelectedIndexChanged += delegate { Target selected = versions.SelectedItem as Target; if (selected != null) path.Text = selected.Support; };
            layout.Controls.Add(versions, 0, 3); layout.SetColumnSpan(versions, 3);
            Label pathLabel = new Label(); pathLabel.Text = "AE 根目录或 Support Files（必须有 AfterFX.exe）"; pathLabel.AutoSize = true; pathLabel.Anchor = AnchorStyles.Left | AnchorStyles.Bottom;
            layout.Controls.Add(pathLabel, 0, 4); layout.SetColumnSpan(pathLabel, 3);
            path.Dock = DockStyle.Fill; path.AccessibleName = "AE 安装目录"; path.TextChanged += delegate { UpdateTarget(); };
            layout.Controls.Add(path, 0, 5);
            browse.Text = "浏览(&B)…"; browse.Dock = DockStyle.Fill; browse.Click += Browse;
            layout.Controls.Add(browse, 2, 5);
            destination.Text = "尚未选择目标。不会猜测或新建 AE 安装路径。"; destination.Dock = DockStyle.Fill; destination.AutoEllipsis = true; destination.Padding = new Padding(0, 8, 0, 0);
            layout.Controls.Add(destination, 0, 6); layout.SetColumnSpan(destination, 3);
            Label help = new Label(); help.Text = "仅安装到 Support Files\\Scripts，不放入 ScriptUI Panels。\r\n安装后可从“文件 > 脚本”或“运行脚本文件”执行；菜单可能下次启动 AE 后出现。"; help.Dock = DockStyle.Fill;
            layout.Controls.Add(help, 0, 7); layout.SetColumnSpan(help, 3);
            stateTitle.Text = "准备就绪"; stateTitle.AutoSize = true; stateTitle.Font = new Font(Font, FontStyle.Bold); stateTitle.Dock = DockStyle.Fill;
            layout.Controls.Add(stateTitle, 0, 8); layout.SetColumnSpan(stateTitle, 3);
            status.Multiline = true; status.ReadOnly = true; status.ScrollBars = ScrollBars.Vertical; status.Dock = DockStyle.Fill; status.BackColor = SystemColors.Control; status.BorderStyle = BorderStyle.FixedSingle; status.AccessibleName = "操作状态和恢复方法";
            status.Text = "不会关闭、重启 AE 或更改安全首选项。\r\n卸载只删除与内置原始 SHA-256 匹配的本插件，修改内容与其他文件均保留。";
            layout.Controls.Add(status, 0, 9); layout.SetColumnSpan(status, 3);
            FlowLayoutPanel buttons = new FlowLayoutPanel(); buttons.Dock = DockStyle.Fill; buttons.Margin = Padding.Empty; buttons.FlowDirection = FlowDirection.RightToLeft; buttons.WrapContents = false;
            close.Text = "关闭(&C)"; close.Size = new Size(94, 32); close.Click += delegate { Close(); };
            uninstall.Text = "卸载(&U)"; uninstall.Size = new Size(112, 32); uninstall.Click += delegate { Run(false); };
            install.Text = "安装(&I)"; install.Size = new Size(112, 32); install.Click += delegate { Run(true); };
            buttons.Controls.Add(close); buttons.Controls.Add(uninstall);
#if UNINSTALLER
            install.Visible = false; AcceptButton = uninstall;
#else
            buttons.Controls.Add(install); AcceptButton = install;
#endif
            CancelButton = close; close.DialogResult = DialogResult.Cancel;
            layout.Controls.Add(buttons, 0, 11); layout.SetColumnSpan(buttons, 3);
            Label foot = new Label(); foot.Text = "权限不足时请右键以管理员身份运行；本工具不会自动提权。"; foot.AutoSize = true; foot.ForeColor = SystemColors.GrayText; foot.Dock = DockStyle.Fill;
            layout.Controls.Add(foot, 0, 12); layout.SetColumnSpan(foot, 3);
            string warning; List<Target> targets = Engine.Discover(options.State, options.TestMode, out warning);
            foreach (Target target in targets) versions.Items.Add(target);
            if (targets.Count == 1) versions.SelectedIndex = 0;
            else if (targets.Count == 0) { detected.Text = "未检测到 AE：请浏览选择实际安装目录"; status.Text += "\r\n没有找到有效版本，不会写入任何猜测路径。"; }
            else { detected.Text = "检测到 " + targets.Count + " 个 AE 版本：请选择目标"; }
            if (!String.IsNullOrEmpty(options.Target)) path.Text = options.Target;
            if (!String.IsNullOrEmpty(warning)) { stateTitle.Text = "卸载记录需要检查"; status.Text = warning; }
            if (options.Uninstall) { AcceptButton = uninstall; uninstall.Select(); }
            UpdateTarget();
        }
        private void UpdateTarget() {
            bool valid = false;
            try { string support = Engine.ValidateTarget(path.Text); destination.Text = "目标文件：" + Path.Combine(Path.Combine(support, "Scripts"), Engine.PluginName); valid = true; }
            catch (Exception ex) { destination.Text = String.IsNullOrWhiteSpace(path.Text) ? "尚未选择目标。不会猜测或新建 AE 安装路径。" : ex.Message; }
            install.Enabled = valid && !busy; uninstall.Enabled = valid && !busy;
        }
        private void Browse(object sender, EventArgs e) {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog()) {
                dialog.Description = "选择 AE 根目录或 Support Files；目录内必须存在 AfterFX.exe。"; dialog.ShowNewFolderButton = false;
                if (Directory.Exists(path.Text)) dialog.SelectedPath = path.Text;
                if (dialog.ShowDialog(this) == DialogResult.OK) { versions.SelectedIndex = -1; path.Text = dialog.SelectedPath; }
            }
        }
        private void Run(bool installing) {
            if (busy) return;
            busy = true; UpdateTarget(); path.Enabled = false; versions.Enabled = false; browse.Enabled = false; close.Enabled = false;
            stateTitle.Text = installing ? "正在安装…" : "正在卸载…"; status.Text = "正在验证目标、脚本哈希和卸载记录，请稍候。"; UseWaitCursor = true; Refresh();
            try {
                Result result = installing ? Engine.Install(path.Text, options.State) : Engine.Uninstall(path.Text, options.State);
                stateTitle.Text = installing ? "安装完成" : "卸载检查完成"; stateTitle.ForeColor = SystemColors.ControlText; status.Text = result.Message; close.Text = "完成(&C)";
            } catch (Exception ex) {
                stateTitle.Text = "操作未完成"; stateTitle.ForeColor = Color.Firebrick; close.Text = "关闭(&C)";
                status.Text = ex.Message + "\r\n权限不足时请关闭工具，右键以管理员身份运行后重试。" + (ex.Message.Contains(Engine.Manual) ? "" : "\r\n" + Engine.Manual);
            } finally { busy = false; UseWaitCursor = false; path.Enabled = true; versions.Enabled = true; browse.Enabled = true; close.Enabled = true; UpdateTarget(); }
        }
    }
}
