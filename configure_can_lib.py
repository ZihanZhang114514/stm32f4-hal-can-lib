#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CAN 库自动配置工具
================================================
用法：
    直接运行脚本（库文件与脚本同目录），或使用 PyInstaller 打包出的单文件 exe
    （库文件已内嵌，放到任何位置双击即可运行，无需 Python 环境）。

    运行后在 GUI 中选择 CubeMX 工程里的 MDK-ARM 文件夹，
    点击“确认配置”，程序会自动：
        1. 复制 can_lib.h  -> <工程根>/Core/Inc/
        2. 复制 can_lib.c  -> <工程根>/Core/Src/
        3. 修改 .uvprojx（把 can_lib.c 加入 Application/User/Core 组、
           为 C 编译器补充 --c99 选项）
        4. 复制 README.md -> MDK-ARM/

    仅对所选的一个工程生效，可重复运行（幂等，重复配置不会出错）。
"""

import os
import re
import shutil
import sys
import tkinter as tk
from tkinter import filedialog, messagebox
from tkinter.scrolledtext import ScrolledText

# ---- 资源目录：库文件所在位置 ----
# PyInstaller 打包时用 --add-data 把库文件打进 exe，运行时解压到 sys._MEIPASS；
# 未打包直接运行时，库文件与脚本同目录。
if getattr(sys, "frozen", False):
    BASE_DIR = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(sys.executable)))
else:
    BASE_DIR = os.path.dirname(os.path.abspath(__file__))

LIB_FILES = ("can_lib.h", "can_lib.c", "README.md")

# 要插入 uvprojx 的 can_lib.c 文件条目（与 CubeMX 生成的缩进保持一致）
CAN_LIB_FILE_ENTRY = (
    "            <File>\n"
    "              <FileName>can_lib.c</FileName>\n"
    "              <FileType>1</FileType>\n"
    "              <FilePath>../Core/Src/can_lib.c</FilePath>\n"
    "            </File>"
)


def read_text(path):
    """读取文本文件，返回 (内容, 编码)。自动识别 UTF-8 BOM。"""
    with open(path, "rb") as f:
        data = f.read()
    if data.startswith(b"\xef\xbb\xbf"):
        return data.decode("utf-8-sig"), "utf-8-sig"
    for enc in ("utf-8", "gbk"):
        try:
            return data.decode(enc), enc
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace"), "utf-8"


def find_uvprojx(mdk_arm_dir):
    """在 MDK-ARM 目录下查找 .uvprojx 文件名。"""
    files = [f for f in os.listdir(mdk_arm_dir) if f.lower().endswith(".uvprojx")]
    return files[0] if files else None


def patch_uvprojx(uvprojx_path):
    """修改 uvprojx：加入 can_lib.c、为 C 编译器补充 --c99。返回动作日志列表。"""
    content, encoding = read_text(uvprojx_path)
    actions = []

    # 1) 把 can_lib.c 加入 Application/User/Core 组
    if "<FilePath>../Core/Src/can_lib.c</FilePath>" in content:
        actions.append("[跳过] can_lib.c 已在工程中")
    else:
        marker = "<FilePath>../Core/Src/main.c</FilePath>"
        idx = content.find(marker)
        if idx == -1:
            raise RuntimeError("uvprojx 中未找到 main.c 条目，无法定位插入位置")
        end = content.find("</File>", idx)
        if end == -1:
            raise RuntimeError("uvprojx 中 main.c 条目格式异常")
        insert_at = end + len("</File>")
        content = content[:insert_at] + "\n" + CAN_LIB_FILE_ENTRY + content[insert_at:]
        actions.append("[完成] 已在 Application/User/Core 组加入 can_lib.c")

    # 2) 为 C 编译器补充 --c99
    #    C 编译器的 <MiscControls> 后紧跟 <Define>USE_HAL_DRIVER...（汇编器的 Define 为空）
    pattern = re.compile(
        r"<MiscControls>([^<]*)</MiscControls>\s*<Define>USE_HAL_DRIVER"
    )
    m = pattern.search(content)
    if m:
        controls = m.group(1)
        if "--c99" in controls:
            actions.append("[跳过] --c99 编译选项已存在")
        else:
            new_controls = (controls + " " if controls.strip() else "") + "--c99"
            content = content[: m.start(1)] + new_controls + content[m.end(1):]
            actions.append("[完成] 已为 C 编译器补充 --c99 选项")
    else:
        actions.append("[提示] 未定位到 C 编译器 MiscControls（可能使用 AC6，无需 --c99）")

    with open(uvprojx_path, "w", encoding=encoding) as f:
        f.write(content)

    return actions


def do_configure(mdk_arm_dir, log=print):
    """执行完整配置流程。log 为回调函数，用于输出进度。"""
    mdk_arm_dir = os.path.normpath(mdk_arm_dir)
    if not os.path.isdir(mdk_arm_dir):
        raise RuntimeError("所选路径不是有效目录")

    missing = [f for f in LIB_FILES if not os.path.isfile(os.path.join(BASE_DIR, f))]
    if missing:
        raise RuntimeError(
            "以下文件未与脚本放在同一目录，请先补齐：\n  " + "\n  ".join(missing)
        )

    uvprojx_name = find_uvprojx(mdk_arm_dir)
    if uvprojx_name is None:
        raise RuntimeError("所选目录下未找到 .uvprojx 文件，请确认选择的是 MDK-ARM 文件夹")

    project_root = os.path.dirname(mdk_arm_dir)
    inc_dir = os.path.join(project_root, "Core", "Inc")
    src_dir = os.path.join(project_root, "Core", "Src")

    os.makedirs(inc_dir, exist_ok=True)
    os.makedirs(src_dir, exist_ok=True)

    # 复制库文件
    shutil.copy2(os.path.join(BASE_DIR, "can_lib.h"), os.path.join(inc_dir, "can_lib.h"))
    log("[完成] 复制 can_lib.h  ->  Core/Inc/")
    shutil.copy2(os.path.join(BASE_DIR, "can_lib.c"), os.path.join(src_dir, "can_lib.c"))
    log("[完成] 复制 can_lib.c  ->  Core/Src/")
    shutil.copy2(os.path.join(BASE_DIR, "README.md"), os.path.join(mdk_arm_dir, "README.md"))
    log("[完成] 复制 README.md ->  MDK-ARM/")

    # 修改 uvprojx
    uvprojx_path = os.path.join(mdk_arm_dir, uvprojx_name)
    for action in patch_uvprojx(uvprojx_path):
        log(action)

    log("[完成] 工程配置完成！")


class App:
    """tkinter GUI"""

    def __init__(self, root):
        self.root = root
        root.title("CAN 库自动配置工具")
        root.resizable(False, False)

        pad = {"padx": 12, "pady": 6}

        tk.Label(
            root,
            text="请选择 CubeMX 工程中的 MDK-ARM 文件夹\n"
                 "点击“确认配置”后自动为该项目配置 CAN 库",
            justify="left",
        ).grid(row=0, column=0, columnspan=2, sticky="w", **pad)

        # 库文件状态
        status = self._lib_status_text()
        self.lib_status_var = tk.StringVar(value=status)
        tk.Label(root, textvariable=self.lib_status_var, fg="#0a7d32", justify="left").grid(
            row=1, column=0, columnspan=2, sticky="w", **pad
        )

        # 路径选择
        tk.Label(root, text="MDK-ARM 路径：").grid(row=2, column=0, sticky="w", **pad)
        self.path_var = tk.StringVar()
        path_entry = tk.Entry(root, textvariable=self.path_var, width=48)
        path_entry.grid(row=2, column=1, sticky="we", **pad)
        tk.Button(root, text="浏览…", command=self.on_browse).grid(
            row=2, column=2, sticky="w", **pad
        )

        # 确认按钮
        tk.Button(root, text="确认配置", command=self.on_confirm, width=20, height=2).grid(
            row=3, column=0, columnspan=3, **pad
        )

        # 日志区
        self.log_box = ScrolledText(root, width=72, height=12, state="disabled")
        self.log_box.grid(row=4, column=0, columnspan=3, **pad)

    def _lib_status_text(self):
        found = [f for f in LIB_FILES if os.path.isfile(os.path.join(BASE_DIR, f))]
        missing = [f for f in LIB_FILES if f not in found]
        if missing:
            return "库文件缺失：" + "、".join(missing) + "（请与脚本放同一目录）"
        return "库文件已就绪：can_lib.h / can_lib.c / README.md"

    def log(self, msg):
        self.log_box.configure(state="normal")
        self.log_box.insert("end", msg + "\n")
        self.log_box.see("end")
        self.log_box.configure(state="disabled")

    def on_browse(self):
        path = filedialog.askdirectory(title="选择 MDK-ARM 文件夹")
        if path:
            self.path_var.set(path)

    def on_confirm(self):
        path = self.path_var.get().strip()
        if not path:
            messagebox.showwarning("提示", "请先选择 MDK-ARM 文件夹")
            return
        try:
            self.log_box.configure(state="normal")
            self.log_box.delete("1.0", "end")
            self.log_box.configure(state="disabled")
            do_configure(path, log=self.log)
            messagebox.showinfo("配置成功", "配置完成！\n\n库已配置到工程：\n" + path)
        except Exception as exc:  # noqa: BLE001
            self.log("[错误] " + str(exc))
            messagebox.showerror("配置失败", str(exc))


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
