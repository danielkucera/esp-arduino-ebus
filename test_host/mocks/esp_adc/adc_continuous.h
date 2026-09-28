#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADC_ATTEN_DB_12 12
#define ADC_UNIT_1 1
#define ADC_CONV_SINGLE_UNIT_1 1
#define ADC_DIGI_OUTPUT_FORMAT_TYPE2 2
#define SOC_ADC_DIGI_MAX_BITWIDTH 12
#define SOC_ADC_SAMPLE_FREQ_THRES_LOW 600
#define SOC_ADC_SAMPLE_FREQ_THRES_HIGH 200000

typedef struct {
  size_t max_store_buf_size;
  size_t conv_frame_size;
  int flags;
} adc_continuous_handle_cfg_t;

typedef struct {
  uint8_t atten;
  uint8_t channel;
  uint8_t unit;
  uint8_t bit_width;
} adc_digi_pattern_config_t;

typedef struct {
  uint8_t pattern_num;
  adc_digi_pattern_config_t* adc_pattern;
  uint32_t sample_freq_hz;
  int conv_mode;
  int format;
} adc_continuous_config_t;

typedef struct {
  bool valid;
  uint8_t channel;
  uint8_t unit;
  uint16_t raw_data;
} adc_continuous_data_t;

typedef struct adc_continuous_handle_t_ {
  int dummy;
} adc_continuous_handle_t_;

typedef adc_continuous_handle_t_* adc_continuous_handle_t;

static inline esp_err_t adc_continuous_new_handle(
    const adc_continuous_handle_cfg_t* config,
    adc_continuous_handle_t* handle) {
  (void)config;
  if (handle == NULL) return ESP_FAIL;
  *handle = new adc_continuous_handle_t_();
  return ESP_OK;
}

static inline esp_err_t adc_continuous_deinit(adc_continuous_handle_t handle) {
  delete handle;
  return ESP_OK;
}

static inline esp_err_t adc_continuous_start(adc_continuous_handle_t handle) {
  (void)handle;
  return ESP_OK;
}

static inline esp_err_t adc_continuous_stop(adc_continuous_handle_t handle) {
  (void)handle;
  return ESP_OK;
}

static inline esp_err_t adc_continuous_config(
    adc_continuous_handle_t handle, const adc_continuous_config_t* config) {
  (void)handle;
  (void)config;
  return ESP_OK;
}

static inline esp_err_t adc_continuous_read(adc_continuous_handle_t handle,
                                            uint8_t* buffer, size_t length,
                                            uint32_t* out_bytes,
                                            uint32_t timeout_ms) {
  (void)handle;
  (void)timeout_ms;
  if (buffer == NULL || length == 0) return ESP_FAIL;
  if (out_bytes != NULL) *out_bytes = 0;
  static const uint8_t sample_block[16] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  const size_t copy =
      length < sizeof(sample_block) ? length : sizeof(sample_block);
  for (size_t i = 0; i < copy; ++i) {
    buffer[i] = sample_block[i];
  }
  if (out_bytes != NULL) *out_bytes = static_cast<uint32_t>(copy);
  return ESP_OK;
}

static inline esp_err_t adc_continuous_parse_data(
    adc_continuous_handle_t handle, const uint8_t* buffer, size_t bytes_read,
    adc_continuous_data_t* out, uint32_t* out_count) {
  (void)handle;
  if (buffer == NULL || out == NULL || out_count == NULL) return ESP_FAIL;
  const size_t count = bytes_read / sizeof(uint16_t);
  for (size_t i = 0; i < count && i < 32; ++i) {
    out[i].valid = true;
    out[i].channel = static_cast<uint8_t>(i % 2);
    out[i].unit = ADC_UNIT_1;
    out[i].raw_data = static_cast<uint16_t>(0x123 + i);
  }
  *out_count = static_cast<uint32_t>(count < 32 ? count : 32);
  return ESP_OK;
}

#ifdef __cplusplus
}
#endif
