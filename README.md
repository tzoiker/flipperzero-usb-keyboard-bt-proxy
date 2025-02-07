# FlipperZero USB Keyboard BT Proxy

– Why???

– IDK.

But in reality, I had a mini-PC to configure, a laptop and no external keyboard.

## Table of Contents

* [About](#about)
  * [GUI](#about_gui)
  * [FAP](#about_fap)
* [Installation](#installation)
  * [GUI](#gui)
  * [FAP](#fap)
* [Usage](#usage)

## <a name="about"/> About

Laptop –> Bluetooth (BLE) –> FlipperZero –> USB (HID) –> Device.

### <a name="about_gui"/> GUI
Requires **python >= 3.10**.

Should work on MacOS (tested), Linux (not tested), Windows (not tested).

**PySide6** used for bluetooth client.
It's fat and an overkill, but my experience with [Bleak](https://bleak.readthedocs.io)
was unsatisfactory.

**PySDL2** is used to capture keyboard events. They map almost as is to HID.

### <a name="about_fap"/> FAP

[This](https://github.com/pragmaeuge/flipper_zero_bts_demo_app) repository
helped to figure stuff out.

## <a name="installation"/> Installation

### <a name="gui"/> GUI

```bash
pip install f0-usb-keyboard-bt-proxy
```

### <a name="fap"/> FAP

Build from source:
```bash
pip install ufbt
cd fap
ufbt launch
```

or
- Download .fap file from the releases;
- Place it to the SD-card under apps/Bluetooth.


## <a name="Usage"/> Usage

1. Connect your F0 to the device you need a keyboard for.
2. Open the `USB Keyboard BT Proxy` application on F0.
3. Press Start. It will show BT device name, e.g., `UsbKbBtP <F0 name>`.
4. Enable bluetooth on your computer.
5. Run `f0-usb-keyboard-bt-proxy --device-bt-name="UsbKbBtP <F0 name>"`.
6. Now you can type in the opened window, and it will, hopefully, work.
