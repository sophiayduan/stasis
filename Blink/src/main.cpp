#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <FastLED.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/i2s.h"
#include "Arena_Hall_1.h"

// --- Digit audio files ---
// To add a digit: uncomment its line and fill in the array entries below
// #include "digit_0.h"
// #include "digit_1.h"
// #include "digit_2.h"
// #include "digit_3.h"
// #include "digit_4.h"
// #include "digit_5.h"
// #include "digit_6.h"
// #include "digit_7.h"
// #include "digit_8.h"
// #include "digit_9.h"

const int16_t* digitAudio[10]     = { nullptr, nullptr, nullptr, nullptr, nullptr,
                                      nullptr, nullptr, nullptr, nullptr, nullptr };
const uint32_t digitAudioSize[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

const char* ssid = "bingus";
const char* password = "dinosaur399";
WebSocketsServer webSocket = WebSocketsServer(81);

// --- I2S ---
const int I2S_LRC  = 14;
const int I2S_BCLK = 12;
const int I2S_DATA = 13;
const uint32_t total_samples = 99684;

// --- FastLED ---
#define NUM_STRIPS  3
#define NUM_LEDS    17
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

CRGB leds[NUM_STRIPS][NUM_LEDS];

const int buttons[NUM_STRIPS] = {4, 32, 21};

int score = 0;
const int interval = 1000;
int timeout = 3000;
long gameStart = 0;
bool gameStarted = false;
unsigned long previousMillis = 0;
unsigned long lastPress[NUM_STRIPS]  = {0, 0, 0};
unsigned long ledOnTime[NUM_STRIPS]  = {0, 0, 0};
bool ledActive[NUM_STRIPS]           = {false, false, false};
bool reached[NUM_STRIPS]             = {false, false, false};
const long cooldown = 300;
int ran = 0;

// --- Audio tasks ---
TaskHandle_t audioTaskHandle = NULL;
volatile bool audioPlaying   = false;
volatile bool stopAudioFlag  = false;

const int digitFreq[10] = {523, 587, 659, 698, 784, 880, 988, 1047, 1175, 1319};
int digitQueue[20];
int digitQueueLen = 0;
TaskHandle_t digitTaskHandle = NULL;

void setStrip(int index, bool on) {
  for (int j = 0; j < NUM_LEDS; j++) {
    leds[index][j] = on ? CRGB::Blue : CRGB::Black;
  }
  FastLED.show();
  ledActive[index] = on;
}

void playAudio(const int16_t* audioArray, uint32_t totalSize) {
  const uint32_t headerOffset = 78;
  i2s_start(I2S_NUM_0);
  size_t bytes_written;

  const int BUF_SIZE = 512;
  int16_t buf[BUF_SIZE * 2];
  int bufIdx = 0;

  for (uint32_t i = headerOffset; i < totalSize - 1; i += 2) {
    if (stopAudioFlag) break;
    uint8_t lo = (uint8_t)(audioArray[i]);
    uint8_t hi = (uint8_t)(audioArray[i + 1]);
    int16_t sample = (int16_t)((hi << 8) | lo);
    sample = (int16_t)constrain((int32_t)sample * 8, -32768, 32767);
    buf[bufIdx++] = sample;
    buf[bufIdx++] = sample;
    if (bufIdx >= BUF_SIZE * 2) {
      i2s_write(I2S_NUM_0, buf, sizeof(buf), &bytes_written, portMAX_DELAY);
      bufIdx = 0;
    }
  }
  if (bufIdx > 0) {
    i2s_write(I2S_NUM_0, buf, bufIdx * sizeof(int16_t), &bytes_written, portMAX_DELAY);
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_stop(I2S_NUM_0);
}

void playTest(int frequency, int durationMs) {
  i2s_start(I2S_NUM_0);
  size_t bytes_written;
  const int buffer_size = 128;
  int16_t dummy_samples[buffer_size];
  int period = 44100 / frequency;
  if (period == 0) return;
  for (int i = 0; i < buffer_size; i++) {
    dummy_samples[i] = ((i % period) < (period / 2)) ? 2000 : -2000;
  }
  int loops = (44100 * durationMs / 1000) / buffer_size;
  for (int l = 0; l < loops; l++) {
    i2s_write(I2S_NUM_0, dummy_samples, sizeof(dummy_samples), &bytes_written, portMAX_DELAY);
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_stop(I2S_NUM_0);
}

void audioTask(void* parameter) {
  audioPlaying = true;
  playAudio(Arena_Hall_1_, total_samples);
  audioPlaying = false;
  audioTaskHandle = NULL;
  vTaskDelete(NULL);
}

void playDigit(int d) {
  if (d < 0 || d > 9) return;
  if (digitAudio[d] != nullptr)
    playAudio(digitAudio[d], digitAudioSize[d]);
  else
    playTest(digitFreq[d], 200);
}

void digitTask(void* parameter) {
  if (audioPlaying) {
    stopAudioFlag = true;
    while (audioPlaying) vTaskDelay(10 / portTICK_PERIOD_MS);
    stopAudioFlag = false;
  }
  for (int i = 0; i < digitQueueLen; i++) {
    playDigit(digitQueue[i]);
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
  digitQueueLen = 0;
  digitTaskHandle = NULL;
  vTaskDelete(NULL);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_TEXT) {
    String message = String((char*)(payload));
    if (message.startsWith("CARD:") && gameStarted && digitTaskHandle == NULL) {
      String digits = message.substring(5);
      digitQueueLen = 0;
      for (int i = 0; i < (int)digits.length() && digitQueueLen < 20; i++) {
        char c = digits.charAt(i);
        if (c >= '0' && c <= '9') digitQueue[digitQueueLen++] = c - '0';
      }
      if (digitQueueLen > 0)
        xTaskCreatePinnedToCore(digitTask, "digits", 4096, NULL, 5, &digitTaskHandle, 0);
    } else if (message == "START_GAME" && !gameStarted) {
      score = 0;
      Serial.println("Game started from website");
      for (int i = 0; i < NUM_STRIPS; i++) {
        reached[i] = false;
        ledOnTime[i] = 0;
        lastPress[i] = 0;
        setStrip(i, false);
      }
      gameStart = millis();
      timeout = 3000;
      previousMillis = millis();
      gameStarted = true;
      ran = random(0, NUM_STRIPS);
      xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 5, &audioTaskHandle, 0);
    }
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);

  FastLED.addLeds<LED_TYPE, 5,  COLOR_ORDER>(leds[0], NUM_LEDS);
  FastLED.addLeds<LED_TYPE, 33, COLOR_ORDER>(leds[1], NUM_LEDS);
  FastLED.addLeds<LED_TYPE, 2,  COLOR_ORDER>(leds[2], NUM_LEDS);
  FastLED.setBrightness(80);
  FastLED.clear();
  FastLED.show();

  for (int i = 0; i < NUM_STRIPS; i++) {
    pinMode(buttons[i], INPUT_PULLUP);
  }

  randomSeed(analogRead(0));

  i2s_config_t i2s_config = {
      .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate          = 44100,
      .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count        = 8,
      .dma_buf_len          = 1024,
      .use_apll             = true
  };
  i2s_pin_config_t pin_config = {
      .bck_io_num   = I2S_BCLK,
      .ws_io_num    = I2S_LRC,
      .data_out_num = I2S_DATA,
      .data_in_num  = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  playTest(880, 1000);
  delay(200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println(WiFi.localIP());
}

void loop() {
  webSocket.loop();

  if (gameStarted) {
    unsigned long currentMillis = millis();

    // --- Game timer events ---
    if (currentMillis - gameStart >= 30000 && !reached[0]) {
      Serial.println("YOU LOSE");
      reached[0] = true;
      gameStarted = false;
      for (int i = 0; i < NUM_STRIPS; i++) setStrip(i, false);
      webSocket.broadcastTXT("GAME_OVER");

    } else if (currentMillis - gameStart >= 20000 && score < 5 && !reached[1]) {
      Serial.println("SPEED UP");
      reached[1] = true;
      timeout = 1000;
      webSocket.broadcastTXT("SPEED_UP");

    } else if (currentMillis - gameStart >= 10000 && score < 5 && !reached[2]) {
      Serial.println("START READING");
      reached[2] = true;
      timeout = 2000;
      webSocket.broadcastTXT("START_READING");
    }

    // --- Light a random strip every interval ---
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;
      ran = random(0, NUM_STRIPS);
      setStrip(ran, true);
      ledOnTime[ran] = currentMillis;
    }

    // --- Timeout: turn off strip and penalize ---
    for (int i = 0; i < NUM_STRIPS; i++) {
      if (ledActive[i] && currentMillis - ledOnTime[i] >= timeout) {
        setStrip(i, false);
        score--;
        Serial.print("Missed, Score: ");
        Serial.println(score);
      }
    }

    // --- Button press: check and score ---
    for (int i = 0; i < NUM_STRIPS; i++) {
      if (digitalRead(buttons[i]) == LOW && currentMillis - lastPress[i] >= cooldown) {
        lastPress[i] = currentMillis;
        if (ledActive[i]) {
          setStrip(i, false);
          score++;
          Serial.print("Score: ");
          Serial.println(score);
        }
      }
    }
  }
}
