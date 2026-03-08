#include <PN532_HSU.h>
#include <PN532.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi beállítások
const char* ssid = "wifi_ssid";
const char* password = "wifi_password";

HardwareSerial mySerial1(1);
HardwareSerial mySerial2(2);

PN532_HSU pn532hsu1(mySerial1);
PN532 nfc1(pn532hsu1);

PN532_HSU pn532hsu2(mySerial2);
PN532 nfc2(pn532hsu2);

// Kapunyitás vezérlés (relé vagy LED)
const int gatePin = 10;
bool gateOpen = false;
unsigned long gateOpenStart = 0;
const unsigned long gateOpenDuration = 3000; // ms

// HTTP kérés állapot
bool httpInProgress = false;
unsigned long httpTimeout = 3000; // ms

// WiFi újracsatlakozás időzítése
unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 10000; // 10s

// Kártya újraolvasás megakadályozása
uint8_t lastUID1[7];
uint8_t lastUIDLength1 = 0;
unsigned long lastReadTime1 = 0;

uint8_t lastUID2[7];
uint8_t lastUIDLength2 = 0;
unsigned long lastReadTime2 = 0;

const unsigned long cardCooldown = 2000; // 2s cooldown

// Kártya elengedés állapot
bool needsRelease1 = false;
bool needsRelease2 = false;

// ---- Segédfüggvények ----
void printUID(uint8_t *uid, uint8_t uidLength) {
  for (uint8_t i = 0; i < uidLength; i++) {
    if (uid[i] < 0x10) Serial.print("0");
    Serial.print(uid[i], HEX);
    if (i < uidLength - 1) Serial.print(":");
  }
  Serial.println();
}

bool compareUID(uint8_t *uid1, uint8_t *uid2, uint8_t len1, uint8_t len2) {
  if (len1 != len2) return false;
  for (uint8_t i = 0; i < len1; i++) {
    if (uid1[i] != uid2[i]) return false;
  }
  return true;
}

void connectWiFi() {
  Serial.println("\nCsatlakozás WiFi-hez...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
}

void checkWiFi() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = currentMillis;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠ WiFi kapcsolat elveszett! Újracsatlakozás...");
      connectWiFi();
    } else {
      Serial.println("✓ WiFi OK (RSSI: " + String(WiFi.RSSI()) + " dBm)");
    }
  }
}

// --- Kapunyitás állapotgép ---
void openGate() {
  digitalWrite(gatePin, HIGH);
  gateOpen = true;
  gateOpenStart = millis();
  Serial.println(">>> Kapu NYITVA");
}

void updateGate() {
  if (gateOpen && (millis() - gateOpenStart >= gateOpenDuration)) {
    digitalWrite(gatePin, LOW);
    gateOpen = false;
    Serial.println(">>> Kapu ZÁRVA");
  }
}

// ---- Szerver kommunikáció ----
void sendToServer(uint8_t *uid, uint8_t uidLength, uint8_t readerNum) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠ Nincs WiFi kapcsolat, nem tudok küldeni!");
    return;
  }

  if (httpInProgress) {
    Serial.println("⚠ HTTP kérés már folyamatban van!");
    return;
  }

  httpInProgress = true;

  // UID hex string készítése
  String uidStr = "";
  for (uint8_t i = 0; i < uidLength; i++) {
    if (uid[i] < 0x10) uidStr += "0";
    uidStr += String(uid[i], HEX);
    if (i < uidLength - 1) uidStr += ":";
  }
  uidStr.toUpperCase();

  // JSON body
  String postData = "{\"reader\": " + String(readerNum) + ", \"uid\": \"" + uidStr + "\"}";

  WiFiClient client;       // <<< mindig új példány
  HTTPClient http;
  String serverUrl = "http://IP_address/api/check_card/";

  Serial.print("[Reader");
  Serial.print(readerNum);
  Serial.print("] POST -> ");
  Serial.println(serverUrl);

  http.setTimeout(httpTimeout);

  if (!http.begin(client, serverUrl)) {
    Serial.println("✗ HTTP begin sikertelen!");
    http.end();
    httpInProgress = false;
    return;
  }

  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(postData);

  if (httpResponseCode > 0) {
    Serial.print("✓ HTTP ");
    Serial.println(httpResponseCode);

    String resp = http.getString();
    Serial.print("Szerver válasz: ");
    Serial.println(resp);

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, resp.c_str());
    resp = "";  // <<< buffer felszabadítása

    if (!error) {
      const char* action = doc["action"];
      if (action && String(action) == "GATE_OPEN") {
        Serial.println(">> GATE_OPEN - Kapu nyitása!");
        openGate();
      } else {
        Serial.println(">> DENIED vagy más válasz");
      }
    } else {
      Serial.println("✗ JSON feldolgozási hiba!");
    }
  } else {
    Serial.print("✗ POST hiba: ");
    Serial.println(httpResponseCode);
  }

  http.end();  // <<< mindig felszabadítja a kapcsolatot

  // Rögtön jelöljük, hogy el lehet engedni a kártyát
  if (readerNum == 1) needsRelease1 = true;
  else needsRelease2 = true;

  httpInProgress = false;
  delay(50);  // <<< LWIP cleanup idő
}

// ---- Kártya elengedés ----
void releaseCards() {
  if (needsRelease1) {
    nfc1.inRelease();
    needsRelease1 = false;
    Serial.println("[Reader1] Kártya elengedve");
  }
  if (needsRelease2) {
    nfc2.inRelease();
    needsRelease2 = false;
    Serial.println("[Reader2] Kártya elengedve");
  }
}

// ---- Setup és loop ----
void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("\n=== ESP32-S3 NFC Reader + WiFi (stabil verzió) ===\n");

  pinMode(gatePin, OUTPUT);
  digitalWrite(gatePin, LOW);

  connectWiFi();

  Serial.println("\n--- PN532 modulok inicializálása ---");
  mySerial1.begin(115200, SERIAL_8N1, 5, 8); // 5 SDA - 8 SCL
  mySerial2.begin(115200, SERIAL_8N1, 4, 9); // 4 SDA - 9 SCL

  nfc1.begin();
  if (!nfc1.getFirmwareVersion()) {
    Serial.println("✗ PN532 #1 nem található!");
    while (1);
  }
  Serial.println("✓ PN532 #1 OK");
  nfc1.SAMConfig();

  nfc2.begin();
  if (!nfc2.getFirmwareVersion()) {
    Serial.println("✗ PN532 #2 nem található!");
    while (1);
  }
  Serial.println("✓ PN532 #2 OK");
  nfc2.SAMConfig();

  Serial.println("\n=== Rendszer kész ===\n");
}

void loop() {
  checkWiFi();
  updateGate();
  releaseCards();

  unsigned long now = millis();

  // Reader 1
  if (now - lastReadTime1 >= cardCooldown) {
    uint8_t uid1[7];
    uint8_t uidLen1;
    if (nfc1.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid1, &uidLen1, 100)) {
      if (!compareUID(uid1, lastUID1, uidLen1, lastUIDLength1)) {
        memcpy(lastUID1, uid1, uidLen1);
        lastUIDLength1 = uidLen1;
        lastReadTime1 = now;
        sendToServer(uid1, uidLen1, 1);
      } else {
        nfc1.inRelease();
      }
    } else {
      if (now - lastReadTime1 >= cardCooldown * 2) {
        lastUIDLength1 = 0;
      }
    }
  }

  // Reader 2
  if (now - lastReadTime2 >= cardCooldown) {
    uint8_t uid2[7];
    uint8_t uidLen2;
    if (nfc2.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid2, &uidLen2, 100)) {
      if (!compareUID(uid2, lastUID2, uidLen2, lastUIDLength2)) {
        memcpy(lastUID2, uid2, uidLen2);
        lastUIDLength2 = uidLen2;
        lastReadTime2 = now;
        sendToServer(uid2, uidLen2, 2);
      } else {
        nfc2.inRelease();
      }
    } else {
      if (now - lastReadTime2 >= cardCooldown * 2) {
        lastUIDLength2 = 0;
      }
    }
  }

  delay(5);
}
