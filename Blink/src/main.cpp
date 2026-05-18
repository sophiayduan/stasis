#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/i2s.h"
#include "Arena_Hall_1.h"

const char* ssid = "iPhone";
const char* password = "aaaaaaaa";

WebSocketsServer webSocket = WebSocketsServer(81);


const int I2S_LRC = 14;
const int I2S_BCLK = 12;
const int I2S_DATA = 13;
const uint32_t total_samples = 99684;

const int buttons[3] = {4,32,21};
const int leds[3] = {5,33,2};
int score = 0;
const int interval = 1000;
int timeout = 3000;
long gameStart = 0;
bool gameStarted = false;
bool ledOn[3] = {false, false, false};
unsigned long ran_go = 0;
unsigned long previousMillis = 0;
unsigned long lastPress[3] = {0,0,0};
unsigned long ledOnTime[3] = {0,0,0};


bool reached[3] = {false,false, false};
const long cooldown = 300;
int ran = random(0, 3);
void playAudio(const int16_t* audioArray, uint32_t totalSize) {
  const uint32_t headerOffset = 78;
  i2s_start(I2S_NUM_0);
  size_t bytes_written;

  const int BUF_SIZE = 512;
  int16_t buf[BUF_SIZE * 2];
  int bufIdx = 0;

  for (uint32_t i = headerOffset; i < totalSize - 1; i += 2) {
    uint8_t lo = (uint8_t)(audioArray[i]);
    uint8_t hi = (uint8_t)(audioArray[i + 1]);
    int16_t sample = (int16_t)((hi << 8) | lo);
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
void playTest(int frequency, int durationMs){
  i2s_start(I2S_NUM_0);
  
  size_t bytes_written;
  int sample_rate = 44100;
  const int buffer_size = 128;
  int16_t dummy_samples[buffer_size];
  
  int period = sample_rate / frequency;
  if (period == 0) return; 

  for(int i = 0; i < buffer_size; i++){
    dummy_samples[i] = ((i % period) < (period / 2)) ? 2000 : -2000;
  }

  int total_samples_needed = (sample_rate * durationMs) / 1000;
  int loops = total_samples_needed / buffer_size;

  for(int l = 0; l < loops; l++) {
    i2s_write(I2S_NUM_0, dummy_samples, sizeof(dummy_samples), &bytes_written, portMAX_DELAY);
  }

  i2s_zero_dma_buffer(I2S_NUM_0); 
  i2s_stop(I2S_NUM_0);
}

TaskHandle_t audioTaskHandle = NULL;
volatile bool audioPlaying = false;

void audioTask(void*parameter){
  audioPlaying = true;
  playAudio(Arena_Hall_1_, total_samples);
  audioPlaying = false;
  audioTaskHandle = NULL;
  vTaskDelete(NULL);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){
  if(type==WStype_TEXT){
    String message = String((char*)(payload));
    if(message=="START_GAME" && !gameStarted){
      score = 0;
      Serial.println("Game started from website");
      for(int i=0; i<3; i++){
        reached[i] = false;
        ledOnTime[i] = 0;
        lastPress[i] = 0;
        digitalWrite(leds[i], LOW);
      }

      gameStart = millis();
      timeout = 3000;
      previousMillis = millis();
      gameStarted = true;
      ran = random(0,3);
      xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 5, &audioTaskHandle, 0);
    }
  }
}
void setup(){
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);

  
  for(int i=0; i<3; i++){
    pinMode(leds[i], OUTPUT);
    pinMode(buttons[i], INPUT_PULLUP);
  }
  randomSeed(analogRead(0));
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = 44100,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 1024,
      .use_apll = false
  };

  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_BCLK,
      .ws_io_num = I2S_LRC,
      .data_out_num = I2S_DATA,
      .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);

  Serial.println("Testing Speaker hardware...");
  playTest(880, 1000); 
  delay(200);
  
  WiFi.begin(ssid, password);
  while(WiFi.status() !=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println(WiFi.localIP());
}

void loop() {
  webSocket.loop();
  if(gameStarted){
  unsigned long currentMillis = millis();
  if(currentMillis-gameStart>=30000 && !reached[0]){
    Serial.println("YOU LOSE - ALL NUMBERS READ");
    reached[0] = true;
    gameStarted = false;
    for(int i=0; i<3; i++){
      digitalWrite(leds[i], LOW);
    }
    Serial.println("Game over");
    webSocket.broadcastTXT("GAME_OVER");
  }  else if (currentMillis-gameStart>=20000 && score<5 && !reached[1]){
    Serial.println("SPEED UP READING NUMBERS");
    reached[1] = true;
    timeout = 1000;
    webSocket.broadcastTXT("SPEED_UP");
  } else if(currentMillis-gameStart>=10000 && score<5 && !reached[2]){
    Serial.println("START READING CARD NUMBERS");
    reached[2] = true;
    timeout = 2000;
    webSocket.broadcastTXT("START_READING");
  }
  if(currentMillis-previousMillis>=interval){
    previousMillis = currentMillis;
    ran = random(0, 3);
    if(!audioPlaying){

    }
    digitalWrite(leds[ran], HIGH);
    ledOn[ran] = true;
    ledOnTime[ran] = currentMillis;
  }
  for(int i=0; i<3; i++){
    if(ledOn[i] && currentMillis-ledOnTime[i] >=timeout){
      digitalWrite(leds[i], LOW);
      ledOn[i] = false;
      score--;
      Serial.print("Missed, Score: ");
      Serial.println(score);
    }
  }
  for(int i=0; i<3; i++){
    if(digitalRead(buttons[i])==LOW && currentMillis-lastPress[i]>=cooldown){
      lastPress[i] = currentMillis;
      if(ledOn[i]){
        digitalWrite(leds[i], LOW);
        ledOn[i] = false;
        score++;
        Serial.print("Score: ");
        Serial.println(score);
      }
    } 
  }
  }
}