"""Regenerate thin Code::Blocks projects; CMake remains the build definition."""
from pathlib import Path
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
units = sorted(p.relative_to(root).as_posix() for area in ("src", "apps", "tests")
               for p in (root/area).rglob("*") if p.suffix in (".cpp", ".hpp", ".inc", ".h"))
for platform in ("Windows", "Linux"):
    doc = ET.Element("CodeBlocks_project_file")
    ET.SubElement(doc, "FileVersion", major="1", minor="6")
    project = ET.SubElement(doc, "Project")
    ET.SubElement(project, "Option", title="Pentarnion Studio - " + platform)
    ET.SubElement(project, "Option", makefile="CMakeLists.txt")
    ET.SubElement(project, "Option", makefile_is_custom="1")
    ET.SubElement(project, "Option", compiler="gcc")
    build = ET.SubElement(project, "Build")
    for config in ("Release", "Debug"):
        lower = config.lower()
        target = ET.SubElement(build, "Target", title=config)
        output = f"build/{platform.lower()}-{lower}/bin/pentarnion-studio"
        if platform == "Windows": output += ".exe"
        ET.SubElement(target, "Option", output=output, prefix_auto="0", extension_auto="0")
        ET.SubElement(target, "Option", working_dir=f"build/{platform.lower()}-{lower}/bin")
        ET.SubElement(target, "Option", object_output=f"build/{platform.lower()}-{lower}")
        ET.SubElement(target, "Option", type="0")
        ET.SubElement(target, "Option", compiler="gcc")
        commands = ET.SubElement(target, "MakeCommands")
        if platform == "Windows":
            base = f'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(PROJECT_DIR)tools/build.ps1" -Configuration {config}'
        else:
            base = f'sh "$(PROJECT_DIR)tools/build.sh" {config}'
        for command, action in (("Build", "Build"), ("CompileFile", "Build"),
                                ("Clean", "Clean"), ("DistClean", "Clean"),
                                ("SilentBuild", "Build")):
            text = base + (" -Action " if platform == "Windows" else " ") + action
            ET.SubElement(commands, command, command=text)
        # Always delegate dependency/up-to-date decisions to Ninja.
        ET.SubElement(commands, "AskRebuildNeeded", command="cmd /c exit 1" if platform == "Windows" else "/bin/false")
    compiler = ET.SubElement(project, "Compiler")
    ET.SubElement(compiler, "Add", option="-std=c++20")
    for directory in ("src", "src/ide"):
        ET.SubElement(compiler, "Add", directory=directory)
    if platform == "Windows":
        for suffix in ("", "/QtCore", "/QtGui", "/QtWidgets"):
            ET.SubElement(compiler, "Add", directory="third-party/windows-deps/ucrt64/include/qt6" + suffix)
    for filename in units:
        unit = ET.SubElement(project, "Unit", filename=filename)
        # Navigation only: custom commands build the CMake target graph.
        ET.SubElement(unit, "Option", compile="0", link="0")
    ET.SubElement(project, "Extensions")
    ET.indent(doc, space="  ")
    data = '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE CodeBlocks_project_file>\n'
    data += ET.tostring(doc, encoding="unicode") + "\n"
    (root / f"Pentarnion-{platform}.cbp").write_text(data, encoding="utf-8")
print(f"Generated two Code::Blocks projects with {len(units)} navigation units each.")
