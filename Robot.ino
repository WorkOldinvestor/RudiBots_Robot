
/*
  Created by Jeremy Thompson, 2026
  
  This work is licensed under the Creative Commons Attribution-ShareAlike 3.0 
  Unported License. To view a copy of this license, visit 
  http://creativecommons.org or send a letter to 
  Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
*/


#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>

#define PIN_ENA D0 
#define PIN_IN1 D1 
#define PIN_IN2 D2 
#define PIN_IN3 D3 
#define PIN_IN4 D4 
#define PIN_ENB D5 
#define PIN_RESET D6

Preferences preferences;
uint8_t peerMac[6];
bool isPaired = false;
unsigned long lastPacketTime = 0;
const unsigned long TIMEOUT_MS = 500;

unsigned long btnPressTime = 0;
bool btnHeld = false;

typedef struct struct_message {
  uint8_t msgType;
  int16_t leftSpeed;
  int16_t rightSpeed;
} struct_message;

void setMotor(int pinPWM, int pinDir1, int pinDir2, int16_t speed) {
  if (speed > 0) {
    digitalWrite(pinDir1, HIGH); digitalWrite(pinDir2, LOW);
    ledcWrite(pinPWM, speed);
  } else if (speed < 0) {
    digitalWrite(pinDir1, LOW); digitalWrite(pinDir2, HIGH);
    ledcWrite(pinPWM, -speed);
  } else {
    digitalWrite(pinDir1, LOW); digitalWrite(pinDir2, LOW);
    ledcWrite(pinPWM, 0);
  }
}

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingDataBytes, int len) {
  if (len != sizeof(struct_message)) return;
  
  struct_message packet;
  memcpy(&packet, incomingDataBytes, sizeof(packet));

  if (!isPaired) {
    if (packet.msgType == 1) { // Pair Request
      Serial.println("✅ Pair request received! Saving MAC & sending ACK.");
      memcpy(peerMac, info->src_addr, 6);
      preferences.putBytes("mac", peerMac, 6);
      isPaired = true;
      
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, peerMac, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      esp_now_add_peer(&peerInfo);
      
      struct_message ack = {2, 0, 0};
      esp_now_send(peerMac, (uint8_t*)&ack, sizeof(ack));
    }
  } else {
    if (memcmp(info->src_addr, peerMac, 6) != 0) return; 
    
    if (packet.msgType == 0) { // Normal Driving Data
      lastPacketTime = millis();
      setMotor(PIN_ENA, PIN_IN1, PIN_IN2, packet.leftSpeed);
      setMotor(PIN_ENB, PIN_IN3, PIN_IN4, packet.rightSpeed);
    } else if (packet.msgType == 1) { 
      // Handshake recovery: Controller missed our ACK and is still asking
      Serial.println("Controller missed our ACK. Resending...");
      struct_message ack = {2, 0, 0};
      esp_now_send(peerMac, (uint8_t*)&ack, sizeof(ack));
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RESET, INPUT_PULLUP);
  
  pinMode(PIN_IN1, OUTPUT); pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT); pinMode(PIN_IN4, OUTPUT);
  
  ledcAttach(PIN_ENA, 4000, 8);
  ledcAttach(PIN_ENB, 4000, 8);
  setMotor(PIN_ENA, PIN_IN1, PIN_IN2, 0);
  setMotor(PIN_ENB, PIN_IN3, PIN_IN4, 0);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) return;

  preferences.begin("rc_link", false);
  if (preferences.getBytes("mac", peerMac, 6) == 6) {
    Serial.println("Loaded saved Controller MAC. Starting in Paired Mode.");
    isPaired = true;
    
    // Register peer so we can reply if they missed an ACK
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  } else {
    Serial.println("No saved MAC. Starting in Unpaired Mode (Listening).");
    isPaired = false;
  }

  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
  if (digitalRead(PIN_RESET) == LOW) {
    if (!btnHeld) {
      btnHeld = true;
      btnPressTime = millis();
    } else if (millis() - btnPressTime > 3000) {
      Serial.println("⚠️ MEMORY WIPED! Rebooting...");
      preferences.clear(); 
      setMotor(PIN_ENA, PIN_IN1, PIN_IN2, 0); 
      setMotor(PIN_ENB, PIN_IN3, PIN_IN4, 0);
      delay(500);
      ESP.restart();       
    }
  } else {
    btnHeld = false;
  }

  if (isPaired && (millis() - lastPacketTime > TIMEOUT_MS)) {
    setMotor(PIN_ENA, PIN_IN1, PIN_IN2, 0);
    setMotor(PIN_ENB, PIN_IN3, PIN_IN4, 0);
  }
  
  delay(10);
}
