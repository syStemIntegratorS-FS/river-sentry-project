#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>

// ============================================
// WS2812B RGB LED Configuration (FastLED)
// ============================================
#define RGB_LED_PIN     48
#define NUM_LEDS        1
#define LED_TYPE        WS2812B
#define COLOR_ORDER     RGB
#define RGB_BRIGHTNESS  30

CRGB leds[NUM_LEDS];

// ============================================
// WiFi Credentials
// ============================================
const char* ssid = "Bridge_Eth01";
const char* password = "Admin.12345";

// ============================================
// MQTT Credentials
// ============================================
const char* mqtt_broker = "699cfaaf78a040b5a6ab0bb14948e8f1.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_username = "publisher"; //edit it based on access control to hive dashboard_____________________
const char* mqtt_password = "Admin1234";

// ============================================
// MQTT Topics
// ============================================
const char* mqtt_topic = "river/monitor/status";
const char* mqtt_telemetry_topic = "river/monitor/telemetry";

// ============================================
// INPUT PINS (Water Level Sensors)
// ============================================
const int PROBE_LOW = 13;
const int PROBE_MID = 12;
const int PROBE_HIGH = 11;

// ============================================
// OUTPUT PINS (MOSFET Controls - External Indicators)
// ============================================
const int OUTPUT_YELLOW = 4;
const int OUTPUT_ORANGE = 5;
const int OUTPUT_RED = 6;
const int SIREN_PIN = 7;

// ============================================
// Alert Levels
// ============================================
enum AlertLevel {
  ALERT_NONE = 0,
  ALERT_YELLOW = 1,
  ALERT_ORANGE = 2,
  ALERT_RED = 3
};

// ============================================
// System State
// ============================================
AlertLevel current_alert = ALERT_NONE;
AlertLevel last_sent_alert = ALERT_NONE;
unsigned long last_debounce = 0;
unsigned long last_continuous_send = 0;
unsigned long last_telemetry = 0;
unsigned long last_rgb_blink = 0;

const int DEBOUNCE_MS = 10;
const int CONTINUOUS_INTERVAL = 5000;
const int TELEMETRY_INTERVAL = 10000;

bool wifi_connected = false;
bool mqtt_connected = false;
bool rgb_blink_state = false;

WiFiClientSecure espClient;
PubSubClient mqtt(espClient);

// ============================================
// RGB LED Control (Connection Status Only)
// ============================================
void setRGB(CRGB color) {
  leds[0] = color;
  FastLED.setBrightness(RGB_BRIGHTNESS);
  FastLED.show();
}

void rgbOff() {
  setRGB(CRGB::Black);
}

void rgbGreen() {
  setRGB(CRGB::Green);
  //Serial.println("🔌 RGB: GREEN - System Online");
}

void rgbRed() {
  setRGB(CRGB::Red);
  //Serial.println("🔌 RGB: RED - Connection Error");
}

void rgbPurple() {
  setRGB(CRGB::Purple);
  //Serial.println("🔌 RGB: PURPLE - Connecting...");
}

void rgbYellow() {
  setRGB(CRGB::Yellow);
  //Serial.println("🔌 RGB: YELLOW - Connection Degraded");
}

// ============================================
// Connection Status RGB Update
// ============================================
void updateRGBByConnection() {
  if (!wifi_connected) {
    rgbRed();
  } 
  else if (!mqtt_connected) {
    if (millis() - last_rgb_blink > 500) {
      rgb_blink_state = !rgb_blink_state;
      if (rgb_blink_state) {
        setRGB(CRGB::Red);
      } else {
        rgbOff();
      }
      last_rgb_blink = millis();
    }
  }
  else if (WiFi.RSSI() < -70) {
    rgbYellow();
  }
  else {
    rgbGreen();
  }
}

void flashTelemetry() {
  setRGB(CRGB::Blue);
  delay(70);
  updateRGBByConnection();
}

// ============================================
// Hardware Output Control (MOSFETs & Siren)
// ============================================
void setOutputForLevel(AlertLevel level) {
  // Turn off all outputs first
  digitalWrite(OUTPUT_YELLOW, LOW);
  digitalWrite(OUTPUT_ORANGE, LOW);
  digitalWrite(OUTPUT_RED, LOW);
  
  switch(level) {
    case ALERT_YELLOW:
      digitalWrite(OUTPUT_YELLOW, HIGH);
      Serial.println("🚨 YELLOW Alert - MOSFET ON");
      break;
    case ALERT_ORANGE:
      digitalWrite(OUTPUT_ORANGE, HIGH);
      Serial.println("🚨 ORANGE Alert - MOSFET ON");
      break;
    case ALERT_RED:
      digitalWrite(OUTPUT_RED, HIGH);
      Serial.println("🚨 RED Alert - MOSFET ON");
      break;
    case ALERT_NONE:
      Serial.println("✅ All MOSFETs OFF");
      break;
  }
}

void activateSiren(AlertLevel level) {
  switch(level) {
    case ALERT_YELLOW:
      Serial.println("🔔 YELLOW Alert - 1 cycle");
      digitalWrite(SIREN_PIN, HIGH);
      delay(3000);
      digitalWrite(SIREN_PIN, LOW);
      break;
    case ALERT_ORANGE:
      Serial.println("🔔 ORANGE Alert - 2 cycles");
      for(int i = 0; i < 2; i++) {
        digitalWrite(SIREN_PIN, HIGH);
        delay(3000);
        digitalWrite(SIREN_PIN, LOW);
        if(i < 1) delay(1000);
      }
      break;
    case ALERT_RED:
      Serial.println("🔔 RED Alert - 3 cycles");
      for(int i = 0; i < 3; i++) {
        digitalWrite(SIREN_PIN, HIGH);
        delay(3000);
        digitalWrite(SIREN_PIN, LOW);
        if(i < 2) delay(1000);
      }
      break;
    default:
      break;
  }
}

// ============================================
// MQTT Functions
// ============================================
void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  String clientId = "DRM_RiverMonitor_" + String(random(0xffff), HEX);
  
  if (mqtt.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
    mqtt_connected = true;
    Serial.println("✅ MQTT Connected");
  } else {
    mqtt_connected = false;
    Serial.printf("❌ MQTT Failed! State: %d\n", mqtt.state());
  }
  updateRGBByConnection();
}

void sendStatus(AlertLevel level, const char* event_type) {
  if (!mqtt.connected()) {
    connectMQTT();
    if (!mqtt.connected()) return;
  }
  
  StaticJsonDocument<256> doc;
  doc["site_id"] = "ILO-SITE-01";
  doc["timestamp"] = millis();
  doc["event"] = event_type;
  
  switch(level) {
    case ALERT_YELLOW:
      doc["alert"] = "YELLOW";
      doc["severity"] = 1;
      doc["message"] = "Monitor water levels - Flooding possible";
      doc["instruction"] = "DRM Personnel, monitor and prepare";
      doc["siren"] = "1 Long Siren";
      break;
    case ALERT_ORANGE:
      doc["alert"] = "ORANGE";
      doc["severity"] = 2;
      doc["message"] = "Prepare for evacuation - Flooding threatening";
      doc["instruction"] = "Residents pack belongings and prepare";
      doc["siren"] = "2 Long Sirens";
      break;
    case ALERT_RED:
      doc["alert"] = "RED";
      doc["severity"] = 3;
      doc["message"] = "EVACUATE NOW - Serious flooding expected";
      doc["instruction"] = "FORCED EVACUATION - Leave immediately";
      doc["siren"] = "3 Long Sirens";
      break;
    default:
      doc["alert"] = "NORMAL";
      doc["severity"] = 0;
      doc["message"] = "River conditions normal";
      doc["instruction"] = "No action required";
      doc["siren"] = "IDLE";
  }
  
  char buffer[256];
  serializeJson(doc, buffer);
  
  if (mqtt.publish(mqtt_topic, buffer)) {
    Serial.printf("📤 %s (%s) - Severity %d\n", 
                  doc["alert"].as<String>().c_str(), 
                  event_type,
                  doc["severity"].as<int>());
    last_sent_alert = level;
  }
}

void sendTelemetry() {
  StaticJsonDocument<128> doc;
  doc["type"] = "telemetry";
  doc["site_id"] = "Jalaur River"; //edit this for every node location__________________________________________________
  doc["uptime"] = millis() / 1000;
  doc["current_status"] = current_alert;
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  
  char buffer[128];
  serializeJson(doc, buffer);

  
  if (mqtt.publish(mqtt_telemetry_topic, buffer)) {
    Serial.println("📡 Telemetry sent");
    flashTelemetry();  //flash every send
  }
}
AlertLevel getAlertLevel() {
  bool low = (digitalRead(PROBE_LOW) == LOW);
  bool mid = (digitalRead(PROBE_MID) == LOW);
  bool high = (digitalRead(PROBE_HIGH) == LOW);
  
  if (high) return ALERT_RED;
  if (mid) return ALERT_ORANGE;
  if (low) return ALERT_YELLOW;
  return ALERT_NONE;
}

// ============================================
// Setup
// ============================================
void setup() {
  Serial.begin(115200);
  delay(500);
  
  // Initialize FastLED
  FastLED.addLeds<LED_TYPE, RGB_LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(RGB_BRIGHTNESS);
  
  // Startup indication - Purple pulse
  rgbPurple();
  
  // Initialize pins
  pinMode(PROBE_LOW, INPUT_PULLUP);
  pinMode(PROBE_MID, INPUT_PULLUP);
  pinMode(PROBE_HIGH, INPUT_PULLUP);
  
  pinMode(OUTPUT_YELLOW, OUTPUT);
  pinMode(OUTPUT_ORANGE, OUTPUT);
  pinMode(OUTPUT_RED, OUTPUT);
  pinMode(SIREN_PIN, OUTPUT);
  
  digitalWrite(OUTPUT_YELLOW, LOW);
  digitalWrite(OUTPUT_ORANGE, LOW);
  digitalWrite(OUTPUT_RED, LOW);
  digitalWrite(SIREN_PIN, LOW);
  
  Serial.println("\n=========================================");
  Serial.println("🌊 RIVER MONITOR - DRM SYSTEM");
  Serial.println("=========================================");
  
  // Connect WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;
    Serial.println("\n WiFi Connected!");
    Serial.print("   IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("   RSSI: ");
    Serial.println(WiFi.RSSI());
  } else {
    wifi_connected = false;
    Serial.println("\n❌ WiFi Failed!");
  }
  
  espClient.setInsecure();
  mqtt.setServer(mqtt_broker, mqtt_port);
  connectMQTT();
  
  Serial.println("\n SYSTEM READY");
  Serial.println("=========================================\n");
}

// ============================================
// Main Loop
// ============================================
void loop() {
  // Update connection status
  wifi_connected = (WiFi.status() == WL_CONNECTED);
  
  if (wifi_connected && !mqtt.connected()) {
    connectMQTT();
  }
  
  if (mqtt.connected()) {
    mqtt.loop();
    mqtt_connected = true;
  } else {
    mqtt_connected = false;
  }
  
  updateRGBByConnection();
  
  // Send telemetry
  if (mqtt.connected() && millis() - last_telemetry > TELEMETRY_INTERVAL) {
    sendTelemetry();
    last_telemetry = millis();
  }
  
  // Read water level sensors and control MOSFETs
  AlertLevel new_alert = getAlertLevel();
  
  if (new_alert != current_alert) {
    if (millis() - last_debounce > DEBOUNCE_MS) {
      
      Serial.println("\n⚡ ALERT STATE CHANGE!");
      Serial.printf("  Previous: %d → New: %d\n", current_alert, new_alert);
      
      // Control external MOSFETs
      setOutputForLevel(new_alert);
      
      if (new_alert != ALERT_NONE) {
        sendStatus(new_alert, "trigger");
        activateSiren(new_alert);
        last_continuous_send = millis();
      } else {
        sendStatus(ALERT_NONE, "release");
      }
      
      current_alert = new_alert;
      last_debounce = millis();
    }
  }
  
  // Continuous updates during active alert
  if (current_alert != ALERT_NONE && mqtt.connected()) {
    if (millis() - last_continuous_send > CONTINUOUS_INTERVAL) {
      sendStatus(current_alert, "continuous");
      last_continuous_send = millis();
    }
  }
  
  delay(10);
}