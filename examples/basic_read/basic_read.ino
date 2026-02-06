/**
 * Basic DHT22 reading example
 * 
 * Works with both original and clone/counterfeit DHT22 sensors.
 * Correctly reads negative temperatures from clone sensors that use
 * two's complement encoding instead of the standard sign-bit encoding.
 * 
 * Wiring:
 *   DHT22 VCC  -> 3.3V
 *   DHT22 DATA -> GPIO14 (or any free GPIO)
 *   DHT22 GND  -> GND
 *   Optional: 4.7kΩ pull-up resistor between DATA and 3.3V
 */

#include <DHT22_Clone_ESP32.h>

#define DHT_PIN 14

// Auto-detect encoding (recommended)
DHT22Clone dht(DHT_PIN);

// Or force a specific encoding:
// DHT22Clone dht(DHT_PIN, DHT22_CLONE);     // Force two's complement
// DHT22Clone dht(DHT_PIN, DHT22_ORIGINAL);   // Force sign-bit encoding

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("DHT22 Clone Library - Basic Example");
  Serial.println("====================================");
}

void loop() {
  DHT22Clone_Result result = dht.read();

  if (result.error == DHT22_OK) {
    Serial.printf("Temperature: %.1f °C\n", result.temperature);
    Serial.printf("Humidity:    %.1f %%\n", result.humidity);
    Serial.printf("Raw bytes:   [%02X %02X %02X %02X %02X]\n",
      result.raw[0], result.raw[1], result.raw[2], result.raw[3], result.raw[4]);
  } else {
    Serial.printf("Error: %s (code %d)\n", 
      DHT22Clone::errorToString(result.error), result.error);
  }

  Serial.println();
  delay(3000);  // DHT22 needs at least 2s between reads
}
