# DHT22_Clone_ESP32

**Arduino library for ESP32 that correctly reads negative temperatures from clone/counterfeit DHT22 sensors.**

## Who needs this library?

If you bought your DHT22 from **AliExpress, eBay, or similar marketplaces** 
and get readings like **-3276.7°C** in freezing weather — you likely have 
a clone sensor. This library fixes that.

### How original vs clone encoding works

The DHT22 sends temperature as a 16-bit value in bytes 2-3 of its 5-byte response.

| Temperature | Original (sign bit) | Clone (two's complement) |
|-------------|--------------------:|-------------------------:|
| +25.6°C     | `0x0100`            | `0x0100`                 |
| +0.4°C      | `0x0004`            | `0x0004`                 |
| -0.1°C      | `0x8001`            | `0xFFFF`                 |
| -0.4°C      | `0x8004`            | `0xFFFC`                 |
| -2.5°C      | `0x8019`            | `0xFFE7`                 |
| -10.0°C     | `0x8064`            | `0xFF9C`                 |

Positive temperatures are encoded identically. The difference only appears below 0°C.

### How to identify a clone

If your DHT22 returns raw bytes like `FF FC` or `FF E7` for temperature (bytes 2-3), you have a clone sensor. Original sensors would show `80 04` or `80 19` for the same temperatures.

This library includes raw byte output so you can check.

## Features

- ✅ **Auto-detects** clone vs original encoding (or force a specific mode)
- ✅ Uses ESP32 **hardware RMT peripheral** — immune to WiFi/BLE interrupt timing issues
- ✅ Works on **ESP32, ESP32-S2, ESP32-S3, ESP32-C3**
- ✅ Clean API with error codes and raw byte access for debugging
- ✅ No external dependencies beyond ESP-IDF (included in Arduino ESP32 core)

## Installation

### Arduino IDE

1. Download this repository as ZIP
2. Arduino IDE → Sketch → Include Library → Add .ZIP Library
3. Select the downloaded ZIP

### PlatformIO

```ini
lib_deps = 
    https://github.com/YOUR_USERNAME/DHT22_Clone_ESP32.git
```

## Wiring

```
DHT22 Pin 1 (VCC)  → 3.3V
DHT22 Pin 2 (DATA) → GPIO14 (or any free GPIO)
DHT22 Pin 4 (GND)  → GND

Optional but recommended: 4.7kΩ resistor between DATA and 3.3V
```

> **Note:** The ESP32 internal pull-up (~45kΩ) is often too weak for reliable DHT22 communication, especially at low temperatures. An external 4.7kΩ pull-up resistor significantly improves stability.

## Quick Start

```cpp
#include <DHT22_Clone.h>

DHT22Clone dht(14);  // GPIO14

void setup() {
  Serial.begin(115200);
}

void loop() {
  DHT22Clone_Result result = dht.read();

  if (result.error == DHT22_OK) {
    Serial.printf("Temp: %.1f°C  Hum: %.1f%%\n", 
      result.temperature, result.humidity);
  } else {
    Serial.printf("Error: %s\n", DHT22Clone::errorToString(result.error));
  }

  delay(3000);
}
```

## API Reference

### Constructor

```cpp
DHT22Clone dht(pin);                    // Auto-detect encoding (recommended)
DHT22Clone dht(pin, DHT22_CLONE);       // Force two's complement
DHT22Clone dht(pin, DHT22_ORIGINAL);    // Force sign-bit encoding
```

### Reading

```cpp
DHT22Clone_Result result = dht.read();

result.temperature  // float, °C
result.humidity     // float, %
result.error        // uint8_t, 0 = OK
result.raw[0..4]    // raw bytes from sensor
```

### Convenience methods

```cpp
dht.getTemperature()   // last successful temperature
dht.getHumidity()      // last successful humidity
dht.getLastError()     // last error code
```

### Error codes

| Code | Name              | Meaning                                    |
|------|-------------------|--------------------------------------------|
| 0    | `DHT22_OK`        | Success                                    |
| 1    | `DHT22_DRIVER`    | RMT peripheral error                       |
| 2    | `DHT22_TIMEOUT`   | Sensor did not respond                     |
| 3    | `DHT22_NACK`      | Invalid acknowledgment pulse               |
| 4    | `DHT22_BAD_DATA`  | Unexpected pulse timing                    |
| 5    | `DHT22_CHECKSUM`  | Data integrity check failed                |
| 6    | `DHT22_UNDERFLOW` | Received fewer than 40 bits                |
| 7    | `DHT22_OVERFLOW`  | Received more than 40 bits                 |

## Auto-Detection Logic

When using `DHT22_AUTO` (default), the library detects clone encoding as follows:

1. **Byte 2 == `0xFF`** → Clone (two's complement). Original encoding with `0xFF` would imply temperatures below -3200°C, which is impossible.
2. **Bit 15 set, byte 2 ≠ `0xFF`** → Try original encoding. If the result is within DHT22 range (-40°C to +80°C), use it. Otherwise fall back to two's complement.
3. **Bit 15 not set** → Positive temperature, both encodings produce the same result.

## Troubleshooting

| Symptom | Likely cause | Solution |
|---------|-------------|----------|
| `-3276.7°C` or similar | Clone sensor + library without clone support | Use this library ✅ |
| `0.0°C` always | Wrong GPIO pin or sensor not connected | Check wiring |
| `DHT22_TIMEOUT` errors | Sensor not responding | Check VCC/GND, add 4.7kΩ pull-up |
| `DHT22_DRIVER` errors | RMT channel conflict (e.g., with camera) | Try reading before initializing camera |
| Intermittent errors | Reading too fast | Use `delay(3000)` or longer between reads |

## Credits

RMT-based reading approach inspired by [dhtESP32-rmt](https://github.com/htmltiger/dhtESP32-rmt) by junkfix/htmltiger.

Clone sensor encoding discovery and fix developed through extensive debugging with real clone hardware.

## License

MIT
