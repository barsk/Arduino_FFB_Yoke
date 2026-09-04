# Replicates Joystick_::Joystick_() runtime descriptor builder VERBATIM for the
# real ctor args used in Arduino_FFB_Yoke.ino:
#   Joystick(JOYSTICK_DEFAULT_REPORT_ID=0x01, JOYSTICK_TYPE_JOYSTICK=0x04,
#            buttonCount=12, hatSwitchCount=1, X=true, Y=true, Z=false)
# Source of truth: src/src/Joystick.cpp lines 110-361 (constructor).

joystickType   = 0x04
_buttonCount   = 12
_hatSwitchCount = 1
includeXAxis, includeYAxis, includeZAxis = True, True, False

buttonsInLastByte = _buttonCount % 8
buttonPaddingBits = (8 - buttonsInLastByte) if buttonsInLastByte > 0 else 0
axisCount = int(includeXAxis) + int(includeYAxis) + int(includeZAxis)

d = []

# USAGE_PAGE (Generic Desktop) / USAGE / COLLECTION (Application) / USAGE (Pointer) / REPORT_ID (1)
d += [0x05, 0x01,  0x09, joystickType,  0xa1, 0x01,  0x09, 0x01,  0x85, 0x01]

if _buttonCount > 0:
    d += [0x05, 0x09]              # USAGE_PAGE (Button)
    d += [0x19, 0x01]             # USAGE_MINIMUM (Button 1)
    d += [0x29, _buttonCount]     # USAGE_MAXIMUM
    d += [0x15, 0x00]             # LOGICAL_MINIMUM (0)
    d += [0x25, 0x01]             # LOGICAL_MAXIMUM (1)
    d += [0x75, 0x01]             # REPORT_SIZE (1)
    d += [0x95, _buttonCount]     # REPORT_COUNT
    d += [0x55, 0x00]             # UNIT_EXPONENT (0)
    d += [0x65, 0x00]             # UNIT (None)
    d += [0x81, 0x02]             # INPUT (Data,Var,Abs)
    if buttonPaddingBits > 0:
        d += [0x75, 0x01]
        d += [0x95, buttonPaddingBits]
        d += [0x81, 0x03]         # INPUT (Const,Var,Abs)

if axisCount > 0 or _hatSwitchCount > 0:
    d += [0x05, 0x01]            # USAGE_PAGE (Generic Desktop)

if _hatSwitchCount > 0:
    d += [0x09, 0x39]           # USAGE (Hat switch)
    d += [0x15, 0x00]
    d += [0x25, 0x07]
    d += [0x35, 0x00]
    d += [0x46, 0x3B, 0x01]     # PHYSICAL_MAXIMUM (315)
    d += [0x65, 0x14]           # UNIT (Eng Rot:Angular Pos)
    d += [0x75, 0x04]
    d += [0x95, 0x01]
    d += [0x81, 0x02]
    if _hatSwitchCount > 1:
        d += [0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00,
              0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x02]
    else:
        d += [0x75, 0x01]       # REPORT_SIZE (1)
        d += [0x95, 0x04]       # REPORT_COUNT (4)
        d += [0x81, 0x03]       # INPUT (Const,Var,Abs)

if axisCount > 0:
    d += [0x09, 0x01]           # USAGE (Pointer)
    d += [0x16, 0x01, 0x80]     # LOGICAL_MINIMUM (-32767)
    d += [0x26, 0xFF, 0x7F]     # LOGICAL_MAXIMUM (32767)
    d += [0x75, 0x10]           # REPORT_SIZE (16)
    d += [0x95, axisCount]      # REPORT_COUNT
    d += [0xA1, 0x00]           # COLLECTION (Physical)
    if includeXAxis: d += [0x09, 0x30]
    if includeYAxis: d += [0x09, 0x31]
    if includeZAxis: d += [0x09, 0x32]
    d += [0x81, 0x02]           # INPUT (Data,Var,Abs)
    d += [0xc0]                 # END_COLLECTION (Physical)

# ctor emits NOTHING further (no Application 0xc0 - the FFB descriptor's trailing
# second 0xC0 closes it).

print(f"// {len(d)} bytes")
line = []
for i, b in enumerate(d):
    line.append(f"0x{b:02X},")
    if len(line) == 12:
        print("  " + " ".join(line))
        line = []
if line:
    print("  " + " ".join(line))
