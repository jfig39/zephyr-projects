# Zephyr Custom Driver Tutorial — Button Driver Example

This project demonstrates how to create and use a **custom GPIO button driver** in Zephyr RTOS.  
It uses a devicetree binding for `"custom,button"` and a driver that polls the GPIO pin state.

---

## Table of Contents
- [Zephyr Custom Driver Tutorial — Button Driver Example](#zephyr-custom-driver-tutorial--button-driver-example)
  - [Table of Contents](#table-of-contents)
  - [Overview](#overview)
  - [Requirements](#requirements)
  - [Hardware Setup](#hardware-setup)
  - [Project Layout](#project-layout)
  - [Build / Flash / Run](#build--flash--run)
  - [Expected Output](#expected-output)
  - [Gotchas \& Troubleshooting](#gotchas--troubleshooting)
  - [License](#license)

---

## Overview
In this tutorial you will:
- Create a custom driver module
- Define devicetree bindings
- Integrate the driver into an application
- Build, flash, and run the application on hardware

---

## Requirements
- **nRF Connect SDK** (tested with v3.0.2)
- **Zephyr RTOS** installed and configured
- **nRF52840-DK** (or another Zephyr-supported board)
- Push button connected to the configured GPIO

---

## Hardware Setup
Connect a push button between the configured GPIO pin and ground.  

Example devicetree configuration (for `P0.12`):
```dts
button_0: custom_button_0 {
    compatible = "custom,button";
    gpios = <&gpio0 12 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
    status = "okay";
};
```

---

## Project Layout
```bash
examples/button_driver_example/
├── boards/        # Devicetree overlays for supported boards
├── modules/       # Custom driver code
├── src/           # Application main code using the driver
├── CMakeLists.txt # CMake build configuration
└── README.md      # This documentation
```

---

## Build / Flash / Run
From the root of the repository:

```bash
west build -b nrf52840dk_nrf52840 examples/button_driver_example
```

Optional clean build:
```bash
west build -t pristine
```

Flash the board:
```bash
west flash
```

Open a serial monitor:
```bash
screen /dev/ttyACM0 115200
```
(Replace `/dev/ttyACM0` with your actual device path.)

---

## Expected Output
```plaintext
[00:00:05.123] <dbg> button: Button 0 pressed
[00:00:06.456] <dbg> button: Button 0 released
```

---

## Gotchas & Troubleshooting
- **Property "pin" is required** → Use `gpios = <...>` not `pin`.
- **No output** → Ensure `CONFIG_LOG` is enabled in `prj.conf`.
- **Submodule build errors** → Remove hidden `.git` folders from example subdirectories.
- **Push errors** → If you get `Permission denied (publickey)`, set up SSH keys with GitHub.

---

## License
MIT License — feel free to use and modify.
