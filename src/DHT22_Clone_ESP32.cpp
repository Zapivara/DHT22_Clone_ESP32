/**
 * DHT22_Clone.cpp - Implementation
 * 
 * License: MIT
 */

#include "DHT22_Clone_ESP32.h"

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
#define RMT_MAX_BLOCKS 64
#else
#define RMT_MAX_BLOCKS 48
#endif

static bool IRAM_ATTR _rmt_rx_done(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *edata, void *udata) {
  BaseType_t w = pdFALSE;
  QueueHandle_t q = (QueueHandle_t)udata;
  xQueueSendFromISR(q, edata, &w);
  return w == pdTRUE;
}

DHT22Clone::DHT22Clone(uint8_t pin, DHT22Clone_Type type) 
  : _pin(pin), _type(type), _lastError(DHT22_OK), _temperature(0), _humidity(0) {}

DHT22Clone_Result DHT22Clone::read() {
  DHT22Clone_Result result = {0, 0, DHT22_OK, {0}};
  
  gpio_num_t dhtpin = static_cast<gpio_num_t>(_pin);
  rmt_channel_handle_t rx_channel = NULL;
  rmt_symbol_word_t symbols[RMT_MAX_BLOCKS];
  rmt_rx_done_event_data_t rx_data;

  rmt_receive_config_t rx_config = {
    .signal_range_min_ns = 3000,
    .signal_range_max_ns = 150000,
  };

  rmt_rx_channel_config_t rx_ch_conf = {
    .gpio_num = dhtpin,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 1000000,
    .mem_block_symbols = RMT_MAX_BLOCKS,
  };

  // Create RMT RX channel
  if (rmt_new_rx_channel(&rx_ch_conf, &rx_channel) != ESP_OK) {
    result.error = DHT22_DRIVER;
    _lastError = result.error;
    return result;
  }

  QueueHandle_t rx_queue = xQueueCreate(1, sizeof(rx_data));
  assert(rx_queue);

  rmt_rx_event_callbacks_t cbs = {
    .on_recv_done = _rmt_rx_done,
  };
  rmt_rx_register_event_callbacks(rx_channel, &cbs, rx_queue);

  // Send start signal: pull LOW for 2ms, then release
  gpio_set_level(dhtpin, 1);
  gpio_pullup_dis(dhtpin);
  gpio_pulldown_dis(dhtpin);
  gpio_set_direction(dhtpin, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_intr_type(dhtpin, GPIO_INTR_DISABLE);
  gpio_set_level(dhtpin, 0);
  vTaskDelay(2 / portTICK_PERIOD_MS);
  gpio_set_level(dhtpin, 1);

  // Enable RMT and start receiving
  if (rmt_enable(rx_channel) || rmt_receive(rx_channel, symbols, sizeof(symbols), &rx_config) != ESP_OK) {
    vQueueDelete(rx_queue);
    rmt_del_channel(rx_channel);
    result.error = DHT22_DRIVER;
    _lastError = result.error;
    return result;
  }

  if (xQueueReceive(rx_queue, &rx_data, pdMS_TO_TICKS(100)) == pdPASS) {
    size_t len = rx_data.num_symbols;
    rmt_symbol_word_t *cur = rx_data.received_symbols;
    uint32_t pulse = cur[0].duration0 + cur[0].duration1;

    if (len < 41) {
      result.error = DHT22_UNDERFLOW;
    } else if (len > 42) {
      result.error = DHT22_OVERFLOW;
    } else if (pulse < 130 || pulse > 180) {
      result.error = DHT22_NACK;
    } else {
      // Decode 40 bits (5 bytes)
      uint8_t data[5] = {0};
      for (uint8_t i = 0; i < 40; i++) {
        pulse = cur[i + 1].duration0 + cur[i + 1].duration1;
        if (pulse > 55 && pulse < 145) {
          data[i / 8] <<= 1;
          if (pulse > 110) {
            data[i / 8] |= 1;
          }
        } else {
          result.error = DHT22_BAD_DATA;
        }
      }

      memcpy(result.raw, data, 5);

      if (result.error == DHT22_OK) {
        // Verify checksum
        uint8_t checksum = data[0] + data[1] + data[2] + data[3];
        if (data[4] == checksum) {
          // Parse humidity (bytes 0-1, always unsigned)
          result.humidity = ((data[0] << 8) | data[1]) * 0.1f;

          // Parse temperature (bytes 2-3, encoding varies)
          result.temperature = parseTemperature(data[2], data[3]);
        } else {
          result.error = DHT22_CHECKSUM;
        }
      }
    }
  } else {
    result.error = DHT22_TIMEOUT;
  }

  // Cleanup
  gpio_set_level(dhtpin, 1);
  vQueueDelete(rx_queue);
  rmt_disable(rx_channel);
  rmt_del_channel(rx_channel);

  _lastError = result.error;
  if (result.error == DHT22_OK) {
    _temperature = result.temperature;
    _humidity = result.humidity;
  }

  return result;
}

float DHT22Clone::parseTemperature(uint8_t byte2, uint8_t byte3) {
  uint16_t raw = (byte2 << 8) | byte3;

  switch (_type) {
    case DHT22_CLONE:
      // Clone: always two's complement
      return ((int16_t)raw) * 0.1f;

    case DHT22_ORIGINAL:
      // Original: bit 15 = sign, bits 14-0 = value
      if (raw & 0x8000) {
        return -((raw & 0x7FFF) * 0.1f);
      }
      return raw * 0.1f;

    case DHT22_AUTO:
    default:
      // Auto-detect:
      // Clone sensors use 0xFF in byte2 for small negative temps
      // (two's complement: -0.1 = 0xFFFF, -25.6 = 0xFF00)
      // Original sensors use 0x80-0xFE range for sign bit encoding
      // (sign bit: -0.1 = 0x8001, -25.6 = 0x8100)
      //
      // Key insight: if byte2 == 0xFF, it's almost certainly a clone
      // because original encoding 0xFF** would mean -(0x7F** * 0.1) 
      // = -3276+ °C which is impossible for DHT22 range (-40 to +80)
      //
      // For byte2 in 0x80-0xFE range, try original encoding first,
      // and if result is out of range, fall back to two's complement
      
      if (byte2 == 0xFF) {
        // Clone encoding (two's complement)
        return ((int16_t)raw) * 0.1f;
      } else if (raw & 0x8000) {
        // Try original encoding first
        float temp = -((raw & 0x7FFF) * 0.1f);
        if (temp >= -40.0f) {
          return temp;
        }
        // Out of range, must be clone
        return ((int16_t)raw) * 0.1f;
      }
      // Positive temperature (same for both encodings)
      return raw * 0.1f;
  }
}

const char* DHT22Clone::errorToString(uint8_t error) {
  switch (error) {
    case DHT22_OK:        return "OK";
    case DHT22_DRIVER:    return "RMT driver error";
    case DHT22_TIMEOUT:   return "Sensor timeout";
    case DHT22_NACK:      return "Invalid ACK";
    case DHT22_BAD_DATA:  return "Bad data pulse";
    case DHT22_CHECKSUM:  return "Checksum error";
    case DHT22_UNDERFLOW: return "Too few bits";
    case DHT22_OVERFLOW:  return "Too many bits";
    default:              return "Unknown error";
  }
}
