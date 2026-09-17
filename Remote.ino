#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>

#define PIN_JOY_LEFT  A0
#define PIN_JOY_RIGHT A1
#define PIN_RESET     D6

Preferences preferences;
esp_now_peer_info_t peerInfo;

uint8_t peerMac[6];
bool isPaired = false;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// 0 = Data, 1 = Pair Request, 2 = Pair Acknowledge
typedef struct struct_message {
  uint8_t msgType;     
  int16_t leftSpeed;
  int16_t rightSpeed;
} struct_message;

struct_message controlData;
unsigned long lastPairingReq = 0;
unsigned long btnPressTime = 0;
bool btnHeld = false;

// Read analog joystick with deadband
int16_t readJoystickToSpeed(uint8_t pin) {
  int raw = analogRead(pin); 
  int centered = raw - 2048;
  if (abs(centered) < 150) return 0;
  int speed = map(centered, -2048, 2047, -255, 255);
  return (int16_t)constrain(speed, -255, 255);
}

// Callback for receiving Pairing Acknowledgement
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingDataBytes, int len) {
  if (!isPaired && len == sizeof(struct_message)) {
    struct_message packet;
    memcpy(&packet, incomingDataBytes, sizeof(packet));
    
    if (packet.msgType == 2) { // Received ACK from a Robot
      memcpy(peerMac, info->src_addr, 6);
      preferences.putBytes("mac", peerMac, 6); // Save to permanent memory
      delay(100);
      ESP.restart(); // Reboot into paired mode
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RESET, INPUT_PULLUP);
  
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) return;

  esp_now_register_recv_cb(OnDataRecv);

  // Check permanent memory for a paired MAC
  preferences.begin("rc_link", false);
  if (preferences.getBytes("mac", peerMac, 6) == 6) {
    isPaired = true;
    memcpy(peerInfo.peer_addr, peerMac, 6);
  } else {
    isPaired = false;
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  }
  
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void loop() {
  // Reset Button Logic (Hold for 3 seconds)
  if (digitalRead(PIN_RESET) == LOW) {
    if (!btnHeld) {
      btnHeld = true;
      btnPressTime = millis();
    } else if (millis() - btnPressTime > 3000) {
      preferences.clear(); // Wipe memory
      ESP.restart();       // Reboot
    }
  } else {
    btnHeld = false;
  }

  // Operation Logic
  if (isPaired) {
    // We are paired: Send joystick data at ~30Hz
    controlData.msgType = 0; // Data type
    controlData.leftSpeed  = readJoystickToSpeed(PIN_JOY_LEFT);
    controlData.rightSpeed = readJoystickToSpeed(PIN_JOY_RIGHT);
    esp_now_send(peerMac, (uint8_t *) &controlData, sizeof(controlData));
    delay(33);
  } else {
    // We are unpaired: Broadcast Pairing Request every 500ms
    if (millis() - lastPairingReq > 500) {
      controlData.msgType = 1; // Pair Req type
      esp_now_send(broadcastAddress, (uint8_t *) &controlData, sizeof(controlData));
      lastPairingReq = millis();
    }
  }
}
