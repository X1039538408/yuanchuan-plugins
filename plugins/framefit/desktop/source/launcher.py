"""Stable Windows entry point; starts the version selected by the verified updater."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(sys.executable).resolve().parent
    current = root / "current.json"
    if current.is_file():
        data = json.loads(current.read_text(encoding="utf-8"))
        selected_value = Path(data.get("versionDirectory", ""))
        selected = (selected_value if selected_value.is_absolute() else root / selected_value).resolve()
        versions = (root / "versions").resolve()
        if versions not in selected.parents or not selected.is_dir():
            raise RuntimeError("FrameFit 当前版本目录无效，请运行修复安装。")
    else:
        selected = root / "versions" / "1.1.0"
    app = selected / "FrameFitApp" / "FrameFitApp.exe"
    if not app.is_file():
        raise RuntimeError(f"FrameFit 程序文件缺失：{app}")
    environment = dict(os.environ, FRAMEFIT_INSTALL_ROOT=str(root), FRAMEFIT_BIN_DIR=str(selected / "bin"),
                       PYINSTALLER_RESET_ENVIRONMENT='1')
    subprocess.Popen([str(app), *sys.argv[1:]], cwd=str(selected), env=environment, close_fds=True)


if __name__ == "__main__":
    main()
