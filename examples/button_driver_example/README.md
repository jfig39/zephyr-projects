# Button Driver Example

This project demonstrates how to create and use a **custom GPIO button driver** in Zephyr RTOS.  
It uses a devicetree binding for `"custom,button"` and a driver that polls the GPIO pin state.

## Requirements
- **nRF Connect SDK** (tested with v3.0.2)
- **Zephyr RTOS** installed and configured
- **nRF52840-DK** (or compatible board with a GPIO button)

## Hardware Setup
- Connect a push button between the configured GPIO pin and ground.
- Example devicetree configuration (for `P0.12`):
  ```dts
  button_0: custom_button_0 {
      compatible = "custom,button";
      gpios = <&gpio0 12 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
      status = "okay";
  };
