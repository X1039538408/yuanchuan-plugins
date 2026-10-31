using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
namespace AESmartLayerSplit {
    internal static class UiSmoke {
        [STAThread]
        public static int Main(string[] args) {
            try { return Run(args); }
            catch (Exception ex) { Console.Error.WriteLine(ex.GetType().FullName); Console.Error.WriteLine(ex.Message); Console.Error.WriteLine(ex.StackTrace); if (ex.InnerException != null) { Console.Error.WriteLine(ex.InnerException.GetType().FullName); Console.Error.WriteLine(ex.InnerException.Message); } return 1; }
        }
        private static int Run(string[] args) {
            Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
            string root = Engine.Full(args[0]); Directory.CreateDirectory(root);
            string ae = Path.Combine(root, "Adobe After Effects Fixture");
            Directory.CreateDirectory(Path.Combine(ae, "Support Files"));
            File.WriteAllBytes(Path.Combine(ae, @"Support Files\AfterFX.exe"), new byte[0]);
            Options options = new Options(); options.TestMode = true; options.State = Path.Combine(root, "state"); options.Target = ae;
            using (MainForm form = new MainForm(options)) {
                form.ShowInTaskbar = false; form.StartPosition = FormStartPosition.Manual; form.Location = new Point(-20000, -20000);
                form.Show(); form.PerformLayout(); Application.DoEvents();
                Inspect(form); Capture(form, root, "installer-ui.png");
                form.Size = form.MinimumSize; form.PerformLayout(); Application.DoEvents(); Inspect(form); Capture(form, root, "installer-minimum-ui.png");
                Button install = FindButton(form, "安装(&I)"); Button uninstall = FindButton(form, "卸载(&U)");
                install.PerformClick(); Application.DoEvents();
                string plugin = Path.Combine(ae, @"Support Files\Scripts\AE 智能分层.jsx");
                if (!File.Exists(plugin) || Engine.Hash(File.ReadAllBytes(plugin)) != Engine.PayloadHash) throw new Exception("UI install failed");
                Capture(form, root, "installer-success-ui.png");
                File.WriteAllText(plugin, "user modified fixture"); uninstall.PerformClick(); Application.DoEvents();
                if (File.ReadAllText(plugin) != "user modified fixture") throw new Exception("UI modified file protection failed");
                if (FindButton(form, "关闭(&C)") == null || FindButton(form, "完成(&C)") != null) throw new Exception("Error UI must not report completion");
                Capture(form, root, "installer-error-ui.png");
                File.WriteAllBytes(plugin, Engine.Resource("payload.jsx")); uninstall.PerformClick(); Application.DoEvents();
                if (File.Exists(plugin)) throw new Exception("UI uninstall failed");
                Capture(form, root, "installer-uninstalled-ui.png");
                form.Close();
            }
            Console.WriteLine("Native UI smoke passed (fixture only, off-screen rendering)."); return 0;
        }
        private static Button FindButton(Control root, string text) {
            foreach (Control child in root.Controls) { if (child is Button && child.Text == text) return (Button)child; Button nested = FindButton(child, text); if (nested != null) return nested; }
            return null;
        }
        private static void Capture(Form form, string root, string name) {
            using (Bitmap bitmap = new Bitmap(form.Width, form.Height)) { form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size)); bitmap.Save(Path.Combine(root, name), System.Drawing.Imaging.ImageFormat.Png); }
        }
        private static void Inspect(Control root) {
            foreach (Control child in root.Controls) {
                if (child is TextBox && ((TextBox)child).Multiline && child.Height < 100) throw new Exception("Status area too short");
                if (child.Bounds.Right > root.ClientSize.Width + 2 || child.Bounds.Bottom > root.ClientSize.Height + 2) throw new Exception("Clipped control: " + child.Text);
                Inspect(child);
            }
        }
    }
}
