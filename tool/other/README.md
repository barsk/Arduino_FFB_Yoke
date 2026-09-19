# Other tools

Tools that are useful when building, wiring or diagnosing the yoke, but are not part
of the firmware and not part of the settings tool.

| File | What it is |
|---|---|
| `FFBTestTool.exe` | SimInvent's test tool for FFB effects. Sends individual DirectInput effects to the yoke, one at a time, with a live force preview. The tool for verifying the physical build and wiring, and for seeing how each effect behaves before putting a sim in front of it. |

## Source for `FFBTestTool.exe`

`FFBTestTool.exe` is free software under the **GNU General Public License v3 or
later** - the same licence as this firmware. The complete corresponding source is at:

**https://github.com/barsk/FFBTestTool**

The binary records the commit it was built from in its version resource, so the
matching source is always identifiable:

```bat
powershell -c "(Get-Item FFBTestTool.exe).VersionInfo.ProductVersion"
```

Rebuild it from that repository if you would rather not trust a binary someone else
compiled, or if the one here is out of date.
