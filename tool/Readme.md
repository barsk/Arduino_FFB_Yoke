# Settings tool (Python + PyQt6)

Setting up Python and PyQt6 is covered here:
https://www.pythonguis.com/pyqt6-tutorial/

This article covers how to package for Windows:
https://www.pythonguis.com/tutorials/packaging-pyqt6-applications-windows-pyinstaller/


## Running from source (virtual environment)

The tool needs Python 3.10 or newer (tested with 3.13) and the packages in
`requirements.txt` (PyQt6 + pyserial). Keep them in a project virtual
environment instead of the system Python:

```bat
:: from the project root (the folder containing platformio.ini)
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r tool\requirements.txt
.venv\Scripts\python.exe tool\yokeTool.py
```

If `python` is not on PATH, use `py` instead. No need to "activate" the
environment - calling `.venv\Scripts\python.exe` directly is enough.

`.venv/` is in `.gitignore`, so recreate it on each machine.

In VS Code / PlatformIO IDE, `.venv` at the project root is picked up
automatically (`python.defaultInterpreterPath` in `.vscode/settings.json`),
so the "Run Python File" button (top-right ▶) runs the tool with the right
interpreter. If the status bar shows another interpreter, use
Ctrl+Shift+P → "Python: Select Interpreter" and pick the `.venv` entry.

Do not use PlatformIO's internal Python (`~/.platformio/penv`) for this tool -
that environment belongs to PlatformIO itself.


## Packaging for Windows

The spec file was created by:
```
pyinstaller --windowed --name "Yoke Tool" --onefile --icon=SimInvent-black.ico --add-data="SimInvent-black.ico:." --add-data="mainwindow.ui:." yokeTool.py
```

To create a new binary (.exe), run PyInstaller from the tool's venv:
```bat
.venv\Scripts\python.exe -m pip install pyinstaller
cd tool
..\.venv\Scripts\pyinstaller.exe ".\Yoke Tool.spec"
```
