#include <Arduino.h>

/*
 * RFID Scanner with audio and lights
 *
 * Communication via WiFi & MQTT using Homie.h
 * The ID will be sent to the MQTT topic.
 * When a success verification occurs, the lights and speaker will be activated
 * depending on the value.
 *
 *
 * Written by Cory Guynn
 * www.InternetOfLEGO.com
 * 2016
 *
 */


// Networking - MQTT using Homie
// https://github.com/marvinroger/homie/tree/master
#include <Homie.h>
HomieNode rfidNode("rfid", "RFID-1", "sensor");
HomieNode temperatureNode("temp-and-humidity", "Temperature and humidity", "sensor");

// Audio
#include "pitches.h"

// RFID
#include "MFRC522.h"
#define RST_PIN D3 // RST-PIN for RC522 - RFID - SPI - Modul GPIO15
#define SS_PIN  D8  // SDA-PIN for RC522 - RFID - SPI - Modul GPIO2
MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance

// DHT
#include "DHT.h"
#define DHTPIN D1     // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11 // DHT 11

// Initialise DHT sensor
DHT dht(DHTPIN, DHTTYPE);
const int TEMPERATURE_INTERVAL = 60;
unsigned long lastTemperatureSent = 0;

#include <NTPClient.h>
// +10 in seconds
//const long utcOffsetInSeconds = 10 * 60 * 60;
const long utcOffsetInSeconds = 0;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, SECRET_NTP_SERVER, utcOffsetInSeconds);

// LEDs and Speaker
/*
const int PIN_RESET = 0; //D3 WeMos ~ This pin will flash WeMos when held low 5s
*/
const int PIN_RED = D4;    //D0 WeMos
const int PIN_GREEN = D0;  //D8 WeMos
const int PIN_SPEAKER = D2;  //D1 WeMos

#define firmwareVersion "1.0.4"

// Standard Functions

void setup() {
  // Console
  Serial.begin(115200);    // Initialize serial communications

  Homie_setFirmware("rfid-and-temp", firmwareVersion);
  Homie.setSetupFunction(setupHandler);
  Homie.setLoopFunction(loopHandler);

  rfidNode.advertise("validate").setName("validated access").settable(verifyHandler);
  rfidNode.advertise("uid").setName("RFID UID").setDatatype("string");
  temperatureNode.advertise("degrees").setName("Degrees").setDatatype("float").setUnit("ºC");
  temperatureNode.advertise("humidity").setName("Relative humidity").setDatatype("percent").setUnit("%");
  temperatureNode.advertise("json-temp").setName("Temp and TS").setDatatype("string");
  temperatureNode.advertise("json-humidity").setName("Humidity and TS").setDatatype("string");
  
  Homie_setBrand(SECRET_HOMIE_BRAND);
  Homie.setup();
}

void loop() {
  // all loop activity is handled by Homie, to ensure connectivity and prevent blocking activity that could disrupt communication
  Homie.loop();
}

void setupHandler() {
  // this replaces the traditional "setup()" to ensure connectivity and handle OTA

  // RFID
  SPI.begin();           // Init SPI bus
  mfrc522.PCD_Init();    // Init MFRC522

  // sound beep
  tone(PIN_SPEAKER,NOTE_C4,250);
  delay(150);
  tone(PIN_SPEAKER,NOTE_E4,250);
  delay(150);
  tone(PIN_SPEAKER,NOTE_C4,250);
  delay(150);
  noTone(PIN_SPEAKER);

  // initialize LEDs
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  digitalWrite(PIN_RED, HIGH);
  digitalWrite(PIN_GREEN, HIGH);
  delay(1000);
  digitalWrite(PIN_GREEN, LOW);
}


// Global Timer
unsigned long previousMillis = 0;
int interval = 2000;

void loopHandler() {
  float t, h;
  float hidx;
  unsigned long epochTime;
  unsigned long currentMillis;

  // current time (aka value from millis()) is used throughout this function, therefore store it in a variable
  currentMillis = millis();

  // check for temperature and humidity if appropriate time has passed
  if (currentMillis - lastTemperatureSent >= TEMPERATURE_INTERVAL * 1000UL || lastTemperatureSent == 0) {
    t = dht.readTemperature();
    h = dht.readHumidity();
    // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(t)) {
        Serial.println(F("Failed to read from DHT sensor!"));
        return;
    }
    // Compute heat index in Celsius (isFahreheit = false)
    hidx = dht.computeHeatIndex(t, h, false);

    timeClient.update();
    epochTime = timeClient.getEpochTime();

    Homie.getLogger() << "time (epoc): " << epochTime << endl;
    
    Homie.getLogger() << "Temperature: " << t << " °C" << endl;
    temperatureNode.setProperty("degrees").send(String(t));
    Homie.getLogger() << "Humidity: " << h << " %" << endl;
    temperatureNode.setProperty("humidity").send(String(h));

    temperatureNode.setProperty("json-temp").send("{\"ts\":" + String(epochTime) + ",\"temp\":" + String(t) + "}");
    temperatureNode.setProperty("json-humidity").send("{\"ts\":" + String(epochTime) + ",\"humidity\":" + String(h) + "}");

    lastTemperatureSent = millis();
  }

  // Look for new RFID cards
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    //Serial.print("scanning");
    delay(50);
    return;
  }

  // scan the cards. Put in a non-blocking delay to avoid duplicate readings
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    // do non-blocking thing here

    // Select one of the cards
    if ( ! mfrc522.PICC_ReadCardSerial()) {
      Serial.print("found card...");
      delay(50);
      return;
    }

    // Process card
    Serial.print(F("Card UID:"));
    dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  }
}


// RFID: dump a byte array as hex values to Serial, then send to validation routine.
void dump_byte_array(byte *buffer, byte bufferSize) {
  String uid;
  const long interval = 1000;

  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
    uid = uid + String(buffer[i], HEX);
  }
  if(uid){
    validate(uid);
  }
}

// validate card UID by sending to server via MQTT
void validate(String uid){
  Serial.print("validating card: ");
  Serial.println(uid);

  // set RFID uid property to be sent via MQTT
// old v1.x model
//  Homie.setNodeProperty(rfidNode, "uid", uid, true);
  rfidNode.setProperty("uid").send(uid);

  // Turn both LEDs on
  digitalWrite(PIN_RED, HIGH);
  digitalWrite(PIN_GREEN, HIGH);
  // play sounds
  tone(PIN_SPEAKER,NOTE_D4,150);
  delay(120);
  tone(PIN_SPEAKER,NOTE_E4,150);
  delay(120);
  noTone(PIN_SPEAKER);
  digitalWrite(PIN_GREEN, LOW);
}

// Receive response from server via MQTT
bool verifyHandler(const HomieRange& range, const String& value) {
  // debugging things - for some reason the code in this function causes an exception
  Homie.getLogger() << "card is " << value << endl;
  return true;
  
  Serial.print("verifyHandler ");
  Serial.println(value);

//  tone(PIN_SPEAKER,NOTE_C5,100);
  digitalWrite(PIN_RED, LOW);
  digitalWrite(PIN_GREEN, LOW);
/*
    delay(250);
    noTone(PIN_SPEAKER);
*/

  if (value == "granted") {
    Serial.println("card accepted");
// old v1.x model
//    Homie.setNodeProperty(rfidNode, "validate", "granted", true);
    rfidNode.setProperty("validate").send(value);
//    Homie.getLogger() << "card is " << value << endl;
    digitalWrite(PIN_GREEN, HIGH);
/*
    tone(PIN_SPEAKER,NOTE_C5,250);
    delay(250);
    tone(PIN_SPEAKER,NOTE_C6,500);
    delay(500);
    noTone(PIN_SPEAKER);
*/
    delay(1000);
    digitalWrite(PIN_GREEN, LOW);
  } else if (value == "denied") {
    Serial.print("card denied");
// old v1.x model
//    Homie.setNodeProperty(rfidNode, "validate", "denied", true);
    rfidNode.setProperty("validate").send(value);
//    Homie.getLogger() << "card is " << value << endl;
/*
    digitalWrite(PIN_RED, HIGH);
    tone(PIN_SPEAKER,NOTE_C5,250);
    delay(250);
    digitalWrite(PIN_RED, LOW);
    tone(PIN_SPEAKER,NOTE_G4,500);
    delay(250);
    noTone(PIN_SPEAKER);
    digitalWrite(PIN_RED, HIGH);
    delay(1000);
*/
  } else {
//    digitalWrite(PIN_RED, HIGH);
    Serial.println("unexpected response: ");
    Serial.print(value);
// old v1.x model
//    Homie.setNodeProperty(rfidNode, "validate", "unexpected", true);
    rfidNode.setProperty("validate").send(String("unexpected"));
//    Homie.getLogger() << "unexpected validate response" << endl;
/*
    tone(PIN_SPEAKER,NOTE_A4,500);
    delay(250);
    digitalWrite(PIN_RED, LOW);
    tone(PIN_SPEAKER,NOTE_A4,500);
    delay(500);
    noTone(PIN_SPEAKER);
*/
    return false;
  }
  digitalWrite(PIN_RED, HIGH);
  return true;
}
