# HA Device Framework

A PlatformIO private library for ESP8266 and ESP32 Home Assistant devices. It provides:

- Wi-Fi provisioning through WiFiManager
- A configuration portal for device name and MQTT broker, username, and password
- Validated EEPROM persistence compatible with Crystalised's existing settings
- mDNS configuration URLs and ArduinoHA connection lifecycle

The project contains a compiling example in `src/main.cpp`. Project-specific entities, hardware control, and web controls remain in the application.

## Use from another PlatformIO project

For a checkout beside an application, point PlatformIO at the local `lib` directory:

```ini
lib_deps =
    file://D:\Projects\HADeviceFramework\lib
```

Create a `ha_device::Framework` with device metadata, register application web
routes and additional mDNS services before `begin()`, then call `loop()` every
iteration. Access ArduinoHA through `framework.device()` and `framework.mqtt()`.
The framework intentionally does not store application state. In particular,
Crystalised LED state and startup defaults are not read or written by it.
