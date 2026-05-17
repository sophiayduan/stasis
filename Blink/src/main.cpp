#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

const char* ssid = "SM-S901W1979";
const char* password = "xkpj1427";

WebSocketsServer webSocket = WebSocketsServer(81);



const int buttons[3] = {4,32,21};
const int leds[3] = {5,33,2};
int score = 0;
const int interval = 1000;
int timeout = 3000;
long gameStart = 0;
bool gameStarted = false;

unsigned long ran_go = 0;
unsigned long previousMillis = 0;
unsigned long lastPress[3] = {0,0,0};
unsigned long ledOnTime[3] = {0,0,0};


bool reached[3] = {false,false, false};
const long cooldown = 300;
int ran = random(0, 3);


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
    }
  }
}

void setup(){
  Serial.begin(115200);
  delay(1000);

  
  for(int i=0; i<3; i++){
    pinMode(leds[i], OUTPUT);
    pinMode(buttons[i], INPUT_PULLUP);
  }
  randomSeed(analogRead(0));
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
  } else if(currentMillis-gameStart>=10000 && score<5 && !reached[2]){
    Serial.println("START READING CARD NUMBERS");
    reached[2] = true;
    timeout = 2000;
  }
  if(currentMillis-previousMillis>=interval){
    previousMillis = currentMillis;
    ran = random(0, 3);
    digitalWrite(leds[ran], HIGH);
    ledOnTime[ran] = currentMillis;
  }
  for(int i=0; i<3; i++){
    if(digitalRead(leds[i])==HIGH && currentMillis-ledOnTime[i] >=timeout){
      digitalWrite(leds[i], LOW);
      score--;
      Serial.print("Missed, Score: ");
      Serial.println(score);
    }
  }
  for(int i=0; i<3; i++){
    if(digitalRead(buttons[i])==LOW && currentMillis-lastPress[i]>=cooldown){
      lastPress[i] = currentMillis;
      if(digitalRead(leds[i])==HIGH){
        digitalWrite(leds[i], LOW);
        score++;
        Serial.print("Score: ");
        Serial.println(score);
      }
    } 
  }
  }
}