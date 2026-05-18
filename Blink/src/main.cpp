#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <FastLED.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

const char* ssid = "SM-S901W1979";
const char* password = "xkpj1427";
//10.93.76.236
WebSocketsServer webSocket = WebSocketsServer(81);

#define NUM_STRIPS  3
#define NUM_LEDS    17
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

const int LED_PINS[NUM_STRIPS] = {14, 25, 27};

CRGB leds[NUM_STRIPS][NUM_LEDS];

const CRGB LED_COLORS[NUM_STRIPS] = {CRGB::Red, CRGB::Green, CRGB::Blue};

const int buttons[NUM_STRIPS] = {4, 32, 21};

int score = 0;
const int interval = 1000;
int timeout = 3000;
long gameStart = 0;
bool gameStarted = false;
unsigned long previousMillis = 0;
unsigned long lastPress[NUM_STRIPS] = {0, 0, 0};
unsigned long ledOnTime[NUM_STRIPS] = {0, 0, 0};
bool ledActive[NUM_STRIPS] = {false, false, false};
bool reached[NUM_STRIPS] = {false, false, false};
const long cooldown = 300;
int ran = 0;

void setStrip(int index, bool on) {
  for (int j = 0; j < NUM_LEDS; j++) {
    leds[index][j] = on ? LED_COLORS[index] : CRGB::Black;
  }
  FastLED.show();
  ledActive[index] = on;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_TEXT) {
    String message = String((char*)(payload));
    if (message == "START_GAME" && !gameStarted) {
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
    }
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);

  // Register each strip on its own pin
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
