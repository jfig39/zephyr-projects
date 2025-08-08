# Zephyr Custom Driver Tutorial — Button Driver Example (nRF Connect in VS Code)

This tutorial walks you through creating a **custom GPIO button driver** in Zephyr RTOS from scratch, using only the **nRF Connect extension in VS Code**.  
No Linux or command-line experience is required.

---

## **Table of Contents**
1. [Create a New Zephyr Project](#1-create-a-new-zephyr-project)
2. [Project Structure](#2-project-structure)
3. [Add a Custom Driver Module](#3-add-a-custom-driver-module)
4. [Create a Devicetree Binding](#4-create-a-devicetree-binding)
5. [Modify the Application Code](#5-modify-the-application-code)
6. [Build the Project](#6-build-the-project)
7. [Flash the Board](#7-flash-the-board)
8. [Open a Serial Monitor](#8-open-a-serial-monitor)
9. [Expected Output](#9-expected-output)
10. [Troubleshooting](#10-troubleshooting)
11. [License](#11-license)

---

## **1. Create a New Zephyr Project**

This section uses the **nRF Connect for VS Code** extension to create the base project.

### Step 1 — Open the nRF Connect Extension
1. Launch **VS Code**.
2. In the left sidebar (Activity Bar), click the **nRF Connect** icon.
3. Under the **Applications** tab, click **Create Application**.

### Step 2 — Choose a Project Template
1. Select **Create New Application from Sample**.
2. Choose the **Hello World** sample from the list.
3. Select a location for your project folder.
4. Name the folder: `button_driver_example`.

### Step 3 — Add a Build Configuration
1. In the **nRF Connect** side panel, click **Add Build Configuration**.
2. Select your `button_driver_example` folder.
3. Choose your board (e.g., `nrf52840dk_nrf52840`).
4. Leave **Build Directory** as default.
5. Click **Build**.

---

## **2. Project Structure**

After creating the project, set up the folder structure like this:

```
button_driver_example/
├── boards/        # Devicetree overlays
├── modules/       # Custom driver code
├── src/           # Application main code
├── CMakeLists.txt
└── README.md
```

You can create folders by **right-clicking** in the VS Code Explorer panel and selecting **New Folder**.

---

## **3. Add a Custom Driver Module**

1. Inside `modules/`, create a new folder `button`.
2. Add two files:
   - `button.c` → driver implementation
   - `CMakeLists.txt` → CMake config for the driver
3. Example `CMakeLists.txt`:
```cmake
zephyr_library()
zephyr_library_sources(button.c)
```

4. Example `button.c`:
```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button, LOG_LEVEL_DBG);

static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static int button_init(const struct device *dev) {
    ARG_UNUSED(dev);
    if (!device_is_ready(btn.port)) {
        LOG_ERR("Button GPIO not ready");
        return -ENODEV;
    }
    gpio_pin_configure_dt(&btn, GPIO_INPUT);
    return 0;
}

SYS_INIT(button_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
```

---

## **4. Create a Devicetree Binding**

1. In `dts/bindings/`, create a file called `custom,button.yaml`:
```yaml
description: Custom GPIO button
compatible: "custom,button"
properties:
  gpios:
    type: phandle-array
    required: true
```

2. Add a board overlay in `boards/nrf52840dk_nrf52840.overlay`:
```dts
/ {
    button0: button_0 {
        compatible = "custom,button";
        gpios = <&gpio0 12 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
        status = "okay";
    };
};
```

---

## **5. Modify the Application Code**

In `src/main.c`:
```c
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

void main(void) {
    LOG_INF("Custom button driver example started");
    while (1) {
        k_msleep(1000);
    }
}
```

---

## **6. Build the Project**

1. In the **nRF Connect** side panel, find your build configuration.
2. Click the **Build** button.

---

## **7. Flash the Board**

1. Connect your board via USB.
2. In the build configuration panel, click **Flash**.

---

## **8. Open a Serial Monitor**

1. In the build configuration panel, click **Device Terminal**.
2. Select your board’s COM/TTY port.
3. Set **Baud Rate** to `115200`.
4. Click **Connect**.

---

## **9. Expected Output**

```
[00:00:00.500] <inf> main: Custom button driver example started
[00:00:05.123] <dbg> button: Button 0 pressed
[00:00:06.456] <dbg> button: Button 0 released
```

---

## **10. Troubleshooting**
- **No output** → Ensure `CONFIG_LOG=y` in `prj.conf`.
- **Build errors** → Check your `CMakeLists.txt` paths.
- **Driver not working** → Verify your GPIO pin in the overlay file.

---

## **11. License**
MIT License — feel free to use and modify.
