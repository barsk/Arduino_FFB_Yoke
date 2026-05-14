# Settings tool (Python + PyQt6)

Setting up Python and PyQ6t is covered here:
https://www.pythonguis.com/pyqt6-tutorial/

This article covers how to package for Windows:
https://www.pythonguis.com/tutorials/packaging-pyqt6-applications-windows-pyinstaller/


The spec file was created by:<br>
```pyinstaller --windowed --name "Yoke Tool" --onefile --icon=SimInvent-black.ico --add-data="SimInvent-black.ico:." --add-data="mainwindow.ui:." yokeTool.py```

To create new binary (.exe), just run:<br>
```pyinstaller.exe '.\Yoke Tool.spec'```
