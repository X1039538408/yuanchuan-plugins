using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Xml;
using Microsoft.Win32;

namespace AESmartLayerSplit {
    internal sealed class InstallException : Exception {
        public InstallException(string message) : base(message) { }
    }
    internal sealed class Target {
        public string Support;
        public string Label;
        public override string ToString() { return Label + " — " + Support; }
    }
    internal sealed class Result {
        public string Message;
        public Result(string message) { Message = message; }
    }
    internal static class Engine {
        public const string PluginName = "AE 智能分层.jsx";
        public const string PayloadHash = "2EB5FA596DFF774F50AA4029AAAAE8B3907F540475F6642E9825B52BBAAE279D";
        public const string ProductVersion = "1.0.0 / portable14";
        public const string Manual = "手动恢复：关闭本工具，在确认的 AE Support Files\\Scripts 中仅检查“AE 智能分层.jsx”；备份您修改过的内容后自行删除该文件。不要删除 Scripts 或其他脚本。记录损坏时，先备份本用户 LocalAppData\\AE Smart Layer Split\\manifest.xml，再移走损坏记录并对每个已安装版本重新安装以重建记录。";
        public static string DefaultState { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AE Smart Layer Split"); } }
        public static string Hash(byte[] bytes) {
            using (SHA256 sha = SHA256.Create()) { return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", ""); }
        }
        public static byte[] Resource(string name) {
            using (Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name)) {
                if (stream == null) throw new InstallException("安装包资源缺失：" + name);
                using (MemoryStream output = new MemoryStream()) { stream.CopyTo(output); return output.ToArray(); }
            }
        }
        public static string Full(string path) {
            if (String.IsNullOrWhiteSpace(path) || !Path.IsPathRooted(path)) throw new InstallException("请选择 AE 的绝对目录路径。");
            string full = Path.GetFullPath(path);
            string root = Path.GetPathRoot(full);
            return full.Length > root.Length ? full.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) : full;
        }
        // Inspect every existing component, including the leaf. Never follow a junction or symlink.
        public static void NoReparse(string path) {
            string current = Full(path);
            while (!String.IsNullOrEmpty(current)) {
                try {
                    FileAttributes attr = File.GetAttributes(current);
                    if ((attr & FileAttributes.ReparsePoint) != 0) throw new InstallException("拒绝符号链接或联接路径：" + current);
                } catch (FileNotFoundException) { } catch (DirectoryNotFoundException) { }
                DirectoryInfo parent = Directory.GetParent(current);
                if (parent == null) break;
                current = parent.FullName;
            }
        }
        public static string ValidateTarget(string input) {
            string chosen = Full(input);
            NoReparse(chosen);
            string support = String.Equals(Path.GetFileName(chosen), "Support Files", StringComparison.OrdinalIgnoreCase) ? chosen : Path.Combine(chosen, "Support Files");
            NoReparse(support);
            string afterFX = Path.Combine(support, "AfterFX.exe");
            NoReparse(afterFX);
            if (!Directory.Exists(support) || !File.Exists(afterFX)) throw new InstallException("不是有效的 AE 目录：必须包含 Support Files\\AfterFX.exe。请选 AE 根目录或 Support Files。");
            string scripts = Path.Combine(support, "Scripts");
            NoReparse(scripts);
            NoReparse(Path.Combine(scripts, PluginName));
            return Full(support);
        }
        private static string Plugin(string support) { return Path.Combine(Path.Combine(support, "Scripts"), PluginName); }
        private static string StatePath(string state, string leaf) { return Path.Combine(Full(state), leaf); }
        private static void ValidateState(string state) {
            NoReparse(state);
            NoReparse(StatePath(state, "manifest.xml"));
            NoReparse(StatePath(state, "uninstaller.exe"));
        }
        private static byte[] ReadOptional(string path) {
            NoReparse(path);
            return File.Exists(path) ? File.ReadAllBytes(path) : null;
        }
        // Same-directory temporary file + atomic Replace, never a truncate-in-place write.
        internal static void AtomicWrite(string path, byte[] bytes) {
            NoReparse(path);
            string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            string backup = path + "." + Guid.NewGuid().ToString("N") + ".bak";
            bool replaced = false;
            try {
                using (FileStream stream = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { stream.Write(bytes, 0, bytes.Length); stream.Flush(true); }
                NoReparse(path);
                if (File.Exists(path)) { File.Replace(temp, path, backup); replaced = true; }
                else File.Move(temp, path);
            } finally {
                // Only the two unique files created by this operation may be removed.
                try { if (File.Exists(temp)) File.Delete(temp); } catch (IOException) { } catch (UnauthorizedAccessException) { }
                // A cleanup failure after commit must not masquerade as an uncommitted write.
                // The backup can safely remain as recovery evidence.
                try { if (replaced && File.Exists(backup)) File.Delete(backup); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            }
        }
        private static void Restore(string path, byte[] previous) {
            NoReparse(path);
            if (previous == null) { if (File.Exists(path)) File.Delete(path); }
            else AtomicWrite(path, previous);
        }
        internal static List<string> ReadManifest(string state) {
            ValidateState(state);
            byte[] data = ReadOptional(StatePath(state, "manifest.xml"));
            List<string> targets = new List<string>();
            if (data == null) return targets;
            try {
                if (data.Length > 65536) throw new Exception("记录过大");
                XmlReaderSettings settings = new XmlReaderSettings();
                settings.DtdProcessing = DtdProcessing.Prohibit;
                settings.XmlResolver = null;
                XmlDocument doc = new XmlDocument(); doc.XmlResolver = null;
                using (MemoryStream stream = new MemoryStream(data)) using (XmlReader reader = XmlReader.Create(stream, settings)) { doc.Load(reader); }
                XmlElement root = doc.DocumentElement;
                if (root == null || root.Name != "aeSmartLayerSplit" || root.GetAttribute("schema") != "1" || root.GetAttribute("payloadSha256") != PayloadHash || root.Attributes.Count != 2) throw new Exception("格式或哈希不匹配");
                foreach (XmlNode node in root.ChildNodes) {
                    if (node.NodeType == XmlNodeType.Whitespace || node.NodeType == XmlNodeType.Comment) continue;
                    XmlElement entry = node as XmlElement;
                    if (entry == null || entry.Name != "target" || entry.Attributes.Count != 1 || entry.HasChildNodes) throw new Exception("未知记录字段");
                    string support = Full(entry.GetAttribute("support"));
                    if (!String.Equals(Path.GetFileName(support), "Support Files", StringComparison.OrdinalIgnoreCase)) throw new Exception("目标必须是 Support Files");
                    // Manifest is a discovery hint, never deletion authority. Live targets are validated again at operation time.
                    NoReparse(support);
                    if (targets.Exists(delegate(string item) { return String.Equals(item, support, StringComparison.OrdinalIgnoreCase); })) throw new Exception("重复目标");
                    targets.Add(support);
                    if (targets.Count > 100) throw new Exception("记录过多");
                }
                return targets;
            } catch (Exception ex) {
                throw new InstallException("卸载记录无效，未更改插件。" + ex.Message + "\r\n" + Manual);
            }
        }
        private static byte[] ManifestBytes(List<string> targets) {
            using (MemoryStream stream = new MemoryStream()) {
                XmlWriterSettings settings = new XmlWriterSettings(); settings.Encoding = new UTF8Encoding(false); settings.Indent = true;
                using (XmlWriter writer = XmlWriter.Create(stream, settings)) {
                    writer.WriteStartElement("aeSmartLayerSplit"); writer.WriteAttributeString("schema", "1"); writer.WriteAttributeString("payloadSha256", PayloadHash);
                    foreach (string support in targets) { writer.WriteStartElement("target"); writer.WriteAttributeString("support", support); writer.WriteEndElement(); }
                    writer.WriteEndElement();
                }
                return stream.ToArray();
            }
        }
        private static Mutex LockState(string state) {
            string key = Hash(Encoding.UTF8.GetBytes(Full(state).ToUpperInvariant()));
            Mutex mutex = new Mutex(false, "Local\\AESmartLayerSplit-" + key);
            bool acquired;
            try { acquired = mutex.WaitOne(0); } catch (AbandonedMutexException) { acquired = true; }
            if (!acquired) { mutex.Dispose(); throw new InstallException("另一个安装或卸载操作正在进行，请稍后再试。"); }
            return mutex;
        }
        public static Result Install(string input, string state) {
            using (Mutex mutex = LockState(state)) {
                try { return InstallLocked(input, state); } finally { mutex.ReleaseMutex(); }
            }
        }
        private static Result InstallLocked(string input, string state) {
            string support = ValidateTarget(input);
            ValidateState(state);
            List<string> targets = ReadManifest(state);
            string plugin = Plugin(support);
            byte[] payload = Resource("payload.jsx");
            if (Hash(payload) != PayloadHash) throw new InstallException("安装包脚本哈希错误，安装已停止。");
            byte[] uninstaller = Resource("uninstaller.exe");
            byte[] existing = ReadOptional(plugin);
            if (existing != null && Hash(existing) != PayloadHash) throw new InstallException("同名脚本内容不同，已保留，拒绝覆盖：" + plugin + "\r\n请自行备份并移走该文件后再安装。");
            Directory.CreateDirectory(Full(state)); ValidateState(state);
            string manifestPath = StatePath(state, "manifest.xml");
            string uninstallPath = StatePath(state, "uninstaller.exe");
            byte[] oldManifest = ReadOptional(manifestPath), oldUninstaller = ReadOptional(uninstallPath);
            bool wrotePlugin = false, wroteUninstaller = false, wroteManifest = false;
            try {
                if (existing == null) {
                    string scripts = Path.GetDirectoryName(plugin);
                    Directory.CreateDirectory(scripts); ValidateTarget(support);
                    // CreateNew rejects races with a newly created same-name file.
                    using (FileStream file = new FileStream(plugin, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { wrotePlugin = true; file.Write(payload, 0, payload.Length); file.Flush(true); }
                }
                ValidateState(state);
                if (oldUninstaller == null || Hash(oldUninstaller) != Hash(uninstaller)) { AtomicWrite(uninstallPath, uninstaller); wroteUninstaller = true; }
                if (!targets.Exists(delegate(string item) { return String.Equals(item, support, StringComparison.OrdinalIgnoreCase); })) targets.Add(support);
                AtomicWrite(manifestPath, ManifestBytes(targets)); wroteManifest = true;
                if (Hash(File.ReadAllBytes(plugin)) != PayloadHash) throw new IOException("写入后验证失败");
                return new Result((existing == null ? "安装完成。" : "已安装相同版本，记录已更新。") + "\r\n目标：" + plugin + "\r\n在 AE 中使用“文件 > 脚本”或“运行脚本文件”。菜单可能在下次启动 AE 后出现；本工具不会重启 AE。\r\n卸载程序：" + uninstallPath);
            } catch (Exception ex) {
                List<string> failures = new List<string>();
                if (wroteManifest) TryRestore(manifestPath, oldManifest, failures);
                if (wroteUninstaller) TryRestore(uninstallPath, oldUninstaller, failures);
                if (wrotePlugin) {
                    try {
                        ValidateTarget(support);
                        if (File.Exists(plugin)) {
                            // A partial write owned by this operation can only be removed when its bytes remain an exact payload prefix.
                            byte[] current = File.ReadAllBytes(plugin);
                            bool ours = current.Length <= payload.Length;
                            for (int i = 0; ours && i < current.Length; i++) if (current[i] != payload[i]) ours = false;
                            if (!ours) throw new IOException("脚本已被其他程序修改，保留该文件");
                            File.Delete(plugin);
                        }
                    } catch (Exception rollback) { failures.Add("脚本回滚失败：" + rollback.Message); }
                }
                throw Failure("安装失败", ex, failures);
            }
        }
        private static void TryRestore(string path, byte[] previous, List<string> failures) {
            try { Restore(path, previous); } catch (Exception ex) { failures.Add("记录回滚失败：" + path + "：" + ex.Message); }
        }
        private static InstallException Failure(string action, Exception ex, List<string> failures) {
            return new InstallException(action + "：" + ex.Message + "\r\n" + (failures.Count == 0 ? "已回滚本次文件更改。" : String.Join("\r\n", failures.ToArray())) + "\r\n若为权限问题，请右键“以管理员身份运行”后重试；不会自动提权。\r\n" + Manual);
        }
        public static Result Uninstall(string input, string state) {
            using (Mutex mutex = LockState(state)) {
                try { return UninstallLocked(input, state); } finally { mutex.ReleaseMutex(); }
            }
        }
        private static Result UninstallLocked(string input, string state) {
            string support = ValidateTarget(input); ValidateState(state);
            List<string> targets = ReadManifest(state);
            string plugin = Plugin(support);
            byte[] existing = ReadOptional(plugin);
            if (existing != null && Hash(existing) != PayloadHash) throw new InstallException("脚本已修改：为保护您的内容，未卸载，也未移除记录。\r\n" + plugin + "\r\n" + Manual);
            string manifestPath = StatePath(state, "manifest.xml");
            byte[] oldManifest = ReadOptional(manifestPath);
            targets.RemoveAll(delegate(string item) { return String.Equals(item, support, StringComparison.OrdinalIgnoreCase); });
            bool wroteManifest = false, removedPlugin = false;
            try {
                // Commit records first; a locked/corrupt state cannot leave the plugin removed.
                if (oldManifest != null) { AtomicWrite(manifestPath, ManifestBytes(targets)); wroteManifest = true; }
                ValidateTarget(support);
                if (existing != null) {
                    // Recheck the actual file immediately before deletion, not the manifest's hash.
                    if (Hash(File.ReadAllBytes(plugin)) != PayloadHash) throw new IOException("脚本在卸载过程中发生变化，已保留");
                    File.Delete(plugin); removedPlugin = true;
                }
                return new Result((existing == null ? "此目标中未发现插件，无需删除。" : "卸载完成：仅删除了原始版本的 AE 智能分层.jsx。") + (oldManifest == null ? "\r\n未找到卸载记录；本次依据显式选择的有效 AE 目录和内置原始哈希执行。" : "") + "\r\n其他脚本与目录均保留；本用户的卸载程序与记录保留，方便管理其他 AE 版本。若已全部卸载，可手动删除 LocalAppData\\AE Smart Layer Split 内的 uninstaller.exe 和 manifest.xml。\r\n不会关闭或重启 AE。");
            } catch (Exception ex) {
                List<string> failures = new List<string>();
                if (removedPlugin) {
                    try { ValidateTarget(support); if (File.Exists(plugin)) throw new IOException("文件已由其他程序创建，不能覆盖"); using (FileStream file = new FileStream(plugin, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { file.Write(existing, 0, existing.Length); file.Flush(true); } }
                    catch (Exception rollback) { failures.Add("脚本回滚失败：" + rollback.Message); }
                }
                if (wroteManifest) TryRestore(manifestPath, oldManifest, failures);
                throw Failure("卸载失败", ex, failures);
            }
        }
        private static void AddCandidate(List<Target> list, string path) {
            try {
                string support = ValidateTarget(path);
                if (list.Exists(delegate(Target item) { return String.Equals(item.Support, support, StringComparison.OrdinalIgnoreCase); })) return;
                string root = Directory.GetParent(support).FullName;
                string version = Path.GetFileName(root);
                try {
                    string productVersion = System.Diagnostics.FileVersionInfo.GetVersionInfo(Path.Combine(support, "AfterFX.exe")).ProductVersion;
                    if (!String.IsNullOrEmpty(productVersion)) version += " (" + productVersion + ")";
                } catch { }
                list.Add(new Target { Support = support, Label = version });
            } catch { /* Invalid discovery hints are ignored; no file changes. */ }
        }
        public static List<Target> Discover(string state, bool testMode, out string warning) {
            List<Target> targets = new List<Target>(); warning = "";
            try { foreach (string support in ReadManifest(state)) AddCandidate(targets, support); } catch (Exception ex) { warning = ex.Message; }
            if (testMode) return targets; // Tests never enumerate real installations or registry.
            string[] bases = { Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86) };
            foreach (string basePath in bases) {
                if (String.IsNullOrEmpty(basePath)) continue;
                string adobe = Path.Combine(basePath, "Adobe");
                try { NoReparse(adobe); if (Directory.Exists(adobe)) foreach (string dir in Directory.GetDirectories(adobe, "Adobe After Effects*")) AddCandidate(targets, dir); } catch { }
            }
            foreach (RegistryView view in new RegistryView[] { RegistryView.Registry64, RegistryView.Registry32 }) {
                foreach (RegistryHive hive in new RegistryHive[] { RegistryHive.LocalMachine, RegistryHive.CurrentUser }) {
                    try {
                        using (RegistryKey registry = RegistryKey.OpenBaseKey(hive, view)) {
                            using (RegistryKey ae = registry.OpenSubKey(@"SOFTWARE\Adobe\After Effects")) {
                                if (ae != null) foreach (string name in ae.GetSubKeyNames()) using (RegistryKey version = ae.OpenSubKey(name)) {
                                    if (version == null) continue;
                                    foreach (string value in new string[] { "InstallPath", "InstallLocation", "Path" }) { string path = version.GetValue(value) as string; if (!String.IsNullOrEmpty(path)) AddCandidate(targets, path); }
                                }
                            }
                            using (RegistryKey uninstall = registry.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall")) {
                                if (uninstall != null) foreach (string name in uninstall.GetSubKeyNames()) using (RegistryKey product = uninstall.OpenSubKey(name)) {
                                    if (product == null) continue;
                                    string display = product.GetValue("DisplayName") as string, path = product.GetValue("InstallLocation") as string;
                                    if (display != null && display.IndexOf("After Effects", StringComparison.OrdinalIgnoreCase) >= 0 && !String.IsNullOrEmpty(path)) AddCandidate(targets, path);
                                }
                            }
                        }
                    } catch { }
                }
            }
            return targets;
        }
    }
}
