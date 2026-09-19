# tool/

Everything that runs on the PC rather than on the yoke, split by who it is for.

| Folder | For | Contents |
|---|---|---|
| [`settings/`](settings_tool/) | **users** | The yoke SettingsTool - Used for configuring and fine-tuning the yoke. |
| [`other/`](other/) | **builders & users** | Diagnostic tools. Currently SimInvent's **`FFBTestTool.exe`**, which diagnoses position, travel, endstop and motor wiring errors. It tests all FFB effects directly over DirectInput for diagnosing any wrong behaviour. |
| [`internal/`](internal/) | **firmware developers** | Development helpers for the firmware itself. Currently `gen_joydesc.py`, which regenerates the frozen PROGMEM HID report descriptor in `src/src/Joystick.cpp`. Nothing here is needed to use or configure a yoke. |
