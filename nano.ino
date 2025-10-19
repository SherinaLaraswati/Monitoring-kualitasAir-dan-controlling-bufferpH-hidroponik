#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include "GravityTDS.h"

// LCD 16x2 
LiquidCrystal_I2C lcd(0x27, 16, 2);
bool tampilSensorData = false;
unsigned long lastBlinkTime = 0;
const unsigned long BLINK_INTERVAL = 3000;

// Serial komunikasi ke NodeMCU
#define ARDUINO_TX_PIN 2
#define ARDUINO_RX_PIN 3
SoftwareSerial nodeSerial(ARDUINO_RX_PIN, ARDUINO_TX_PIN);

// TDS 
#define TdsSensorPin A1
float tdsValue = 0;
float tdsVoltage;
float temperature = 25;
GravityTDS gravityTds;

// pH 
#define pHSensorPin A2
float calibration_value = 21.34 + 0.54;
unsigned long int avgval;
int buffer_arr[10], temp;
float pHValue;
  
// Suhu 
#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature suhuSensor(&oneWire);
float suhu;

// Relay
#define RELAY_PIN1 6  // pH-UP
#define RELAY_PIN2 7  // pH-DOWN

// Status pompa
String sRelay1 = "MATI";
String sRelay2 = "MATI";
unsigned long relayStartUp = 0;
unsigned long relayStartDown = 0;
int relayDurationUp = 0;
int relayDurationDown = 0;
bool relayUpAktif = false;
bool relayDownAktif = false;

// Waktu 
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 2000;

// Fuzzy variable 
float z_kualitas, z_up, z_down;
int kualitas;
String kualitasAir = "----";

void setup() {
  Serial.begin(9600);
  nodeSerial.begin(9600);

  pinMode(RELAY_PIN1, OUTPUT);
  pinMode(RELAY_PIN2, OUTPUT);

  lcd.init();
  lcd.backlight();

  suhuSensor.begin();
  gravityTds.setPin(TdsSensorPin);
  gravityTds.setAref(5.0);  
  gravityTds.setAdcRange(1024);  
  gravityTds.begin();  

  Serial.println("Sistem Siap!");
}

void loop() {
  unsigned long now = millis();
  updateRelayStatus();

  if (now - lastSendTime >= SEND_INTERVAL) {
    bacaSensor();
    fuzzySugeno(pHValue, tdsValue);
    updateRelayStatus();
    tampilSerial();
    sendSensorData();
    lastSendTime = now;
  }

  tampilLCD();
  checkNodeMCUResponse();
}

void bacaSensor() {
  // Sensor suhu
  suhuSensor.requestTemperatures();
  float suhuBaca = suhuSensor.getTempCByIndex(0);
  if (suhuBaca == -127.0 || suhuBaca == DEVICE_DISCONNECTED_C) {
    Serial.println(" Error: Sensor suhu gagal baca! Menggunakan suhu default 25°C");
    suhu = 25.0;
  } else {
    suhu = suhuBaca;
  }

  // Tds
  gravityTds.setTemperature(temperature);  
  gravityTds.update();  
  tdsValue = gravityTds.getTdsValue(); 

  // Sensor pH
  for (int i = 0; i < 10; i++) {
    buffer_arr[i] = analogRead(pHSensorPin);
    delay(30);
  }

  // Urutkan data 
  for (int i = 0; i < 9; i++) {
    for (int j = i + 1; j < 10; j++) {
      if (buffer_arr[i] > buffer_arr[j]) {
        temp = buffer_arr[i];
        buffer_arr[i] = buffer_arr[j];
        buffer_arr[j] = temp;
      }
    }
  }

  // Ambil rata-rata dari data tengah
  avgval = 0;
  for (int i = 2; i < 8; i++)
    avgval += buffer_arr[i];

  // Konversi hasil analog ke voltage
  float volt = (float)avgval * 5.0 / 1023.0 / 6;
  pHValue = -5.70 * volt + calibration_value;
}

void tampilSerial() {
  Serial.print("Get Data - PPM: ");
  Serial.print(tdsValue,0);
  Serial.print(" pH: ");
  Serial.print(pHValue,2);
  Serial.print(" suhu air: ");
  Serial.print(suhu,1);
  Serial.print(" Kualitas Air: ");
  Serial.println(kualitasAir);
  Serial.print("Status pompa1: ");
  Serial.print(sRelay1);
  Serial.print(" Status pompa2: ");
  Serial.print(sRelay2);
  Serial.print(" Waktu pompa1: ");
  Serial.print(relayDurationUp / 1000.0);
  Serial.print(" Waktu pompa2 : ");
  Serial.println(relayDurationDown / 1000.0);
}

void tampilLCD() {

  if (millis() - lastBlinkTime >= BLINK_INTERVAL) {
    lastBlinkTime = millis();
    lcd.clear();

    if (tampilSensorData) {
      lcd.backlight();
      lcd.setCursor(2, 0);
      lcd.print("kualitas air");

      if (kualitasAir == "BURUK" || kualitasAir == "BAIK"){
        lcd.setCursor (5, 1);
      }else{
        lcd.setCursor(3,1);
      }
      lcd.print(kualitasAir);

    } else {
      lcd.setCursor(5, 0);
      lcd.print("PH:");
      lcd.print(pHValue, 2);

      if (tdsValue < 1000) {
        lcd.setCursor(4, 1);
      } else {
        lcd.setCursor(2, 1);
      }
      lcd.print("PPM:");
      lcd.print(tdsValue, 0);
    }
    tampilSensorData = !tampilSensorData;
  }
}

void sendSensorData() {
  String dataPacket = "SUHU:" + String(suhu, 1) +
                      ",PPM:" + String(tdsValue, 0) +
                      ",pH:" + String(pHValue, 2) +
                      ",KA:" + kualitasAir +
                      ",P1:" + sRelay1 +
                      ",P2:" + sRelay2 +
                      ",WP1:" + String(relayDurationUp / 1000.0, 2) +
                      ",WP2:" + String(relayDurationDown / 1000.0, 2) + 
                      ",OKA:" + String(z_kualitas) +
                      ",OP1:" + String(z_up) + 
                      ",OP2:" + String(z_down) + ";";

  nodeSerial.println(dataPacket);
  Serial.print(">>> Kirim: " + dataPacket);
  Serial.println("");
}

void updateRelayStatus() {
  unsigned long now = millis();

  if (relayUpAktif && now - relayStartUp >= relayDurationUp) {
    digitalWrite(RELAY_PIN1, HIGH); //OFF
    relayUpAktif = false;
    relayDurationUp = 0;
  }

  if (relayDownAktif && now - relayStartDown >= relayDurationDown) {
    digitalWrite(RELAY_PIN2, HIGH);
    relayDownAktif = false;
    relayDurationDown = 0;
  }

  if (relayUpAktif) {
    sRelay1 = "ON";
  } else {
    sRelay1 = "OFF";
  }

  if (relayDownAktif) {
    sRelay2 = "ON";
  } else {
    sRelay2 = "OFF";
  }

}

void checkNodeMCUResponse() {
  if (nodeSerial.available()) {
    String response = nodeSerial.readStringUntil('\n');
    response.trim();
    if (response.length() > 0) {
      Serial.println("<<< NodeMCU: " + response);
    }
  }
}

// Fuzzy min
float fuzzymin(float a, float b) {
    if (a < b) {
        return a;
    } else {
        return b;
    }
}

// Fuzzifikasi 
float sangatAsam(float x) {
  float result;
  if (x <= 4.0) {
    result = 1.0;
  } else if (x < 5.0) {
    result = (5.0 - x) / (5.0 - 4.0);
  } else {
    result = 0.0;
  }
  return result;
}

float asam(float x) {
  float result;
  if (x <= 4.0 || x >= 6.0) {
    result = 0.0;
  } else if (x < 5.5) {
    result = (5.5 - x) / 1.5;
  } else if (x == 5.5) {
    result = 1.0;
  } else {
    result = (6.0 - x) / 0.5;
  }
  return result;
}

float normalPH(float x) {
  float result;
  if (x <= 5.5 || x >= 7.0) {
    result = 0.0;
  } else if (x < 6.5) {
    result = (6.5 - x) / 1.0;
  } else if (x == 6.5) {
    result = 1.0;
  } else {
    result = (7.0 - x) / 0.5;
  }
  return result;
}

float basa(float x) {
  float result;
  if (x <= 6.5 || x >= 8.5) {
    result = 0.0;
  } else if (x < 7.5) {
    result = (7.5 - x) / 1.0;
  } else if (x == 7.5) {
    result = 1.0;
  } else {
    result = (8.5 - x) / 1.0;
  }
  return result;
}

float sangatBasa(float x) {
  float result;

  if (x <= 8.0 || x >= 14.0) {
    result = 0.0;
  } else if (x > 8.0 && x < 11.0) {
    result = (x - 8.0) / (11.0 - 8.0);  // naik dari 0 ke 1
  } else if (x >= 11.0 && x <= 14.0) {
    result = 1.0;  // tetap 1 pada rentang ini
  }

  return result;
}

float rendahPPM(float x) {
  float result;

  if (x <= 0.0 || x >= 570.0) {
    result = 0.0;
  } else if (x <= 200.0) {
    result = 1.0;
  } else { // 200 < x < 570
    result = (570.0 - x) / (570.0 - 200.0); 
  }

  return result;
}

float normalPPM(float x) {
  float result;
  if (x <= 560.0 || x >= 840.0) {
    result = 0.0;
  } else if (x < 680.0) {
    result = (680.0 - x) / 120.0;
  } else if (x == 680.0) {
    result = 1.0;
  } else {
    result = (840.0 - x) / 160.0;
  }
  return result;
}

float tinggiPPM(float x) {
  float result;

  if (x <= 830.0 || x >= 1400.0) {
    result = 0.0;
  } else if (x > 830.0 && x < 1100.0) {
    result = (x - 830.0) / (1100.0 - 830.0);  
  } else { // 1100 <= x <= 1400
    result = 1.0;
  }

  return result;
}

struct Rule {
  float mu;
  int outkualitas;
  int outUp;
  int outDown;
};

void fuzzySugeno(float ph, float ppm) {
  float muSangatAsam = sangatAsam(ph);
  float muAsam = asam(ph);
  float muNormalPH = normalPH(ph);
  float muBasa = basa(ph);
  float muSangatBasa = sangatBasa(ph);

  float muRendah = rendahPPM(ppm);
  float muNormalPPM = normalPPM(ppm);
  float muTinggi = tinggiPPM(ppm);

  // Rulebase
  Rule rules[15];
  int index = 0;
  rules[index++] = { fuzzymin(muSangatAsam, muRendah), 0, 5, 0 }; // Kualitas air, pompa 1 (basa/up), pompa 2 (asam/down)
  rules[index++] = { fuzzymin(muAsam, muRendah),       0, 2, 0 };
  rules[index++] = { fuzzymin(muNormalPH, muRendah),   1, 0, 0 };
  rules[index++] = { fuzzymin(muBasa, muRendah),       0, 0, 2 };
  rules[index++] = { fuzzymin(muSangatBasa, muRendah), 0, 0, 5 };
  rules[index++] = { fuzzymin(muSangatAsam, muNormalPPM), 0, 5, 0 };
  rules[index++] = { fuzzymin(muAsam, muNormalPPM),       0, 2, 0 };
  rules[index++] = { fuzzymin(muNormalPH, muNormalPPM),   2, 0, 0 };
  rules[index++] = { fuzzymin(muBasa, muNormalPPM),       0, 0, 2 };
  rules[index++] = { fuzzymin(muSangatBasa, muNormalPPM), 0, 0, 5 };
  rules[index++] = { fuzzymin(muSangatAsam, muTinggi),    0, 5, 0 };
  rules[index++] = { fuzzymin(muAsam, muTinggi),          0, 2, 0 };
  rules[index++] = { fuzzymin(muNormalPH, muTinggi),      1, 0, 0 };
  rules[index++] = { fuzzymin(muBasa, muTinggi),          0, 0, 2 };
  rules[index++] = { fuzzymin(muSangatBasa, muTinggi),    0, 0, 5 };

  // Inferensi + Defuzzifikasi Sugeno
  float sumMu = 0, sumKualitas = 0, sumUp = 0, sumDown = 0;
  for (int i = 0; i < 15; i++) {
    sumMu += rules[i].mu;
    sumKualitas += rules[i].mu * rules[i].outkualitas;
    sumUp += rules[i].mu * rules[i].outUp;
    sumDown += rules[i].mu * rules[i].outDown;
  }

  if (sumMu == 0) {
    z_kualitas = z_up = z_down = 0;
  } else {
    z_kualitas = sumKualitas / sumMu;
    z_up       = sumUp / sumMu;
    z_down     = sumDown / sumMu;
  }

  Serial.println(" --- Hasil deffuzifikasi ---");
  Serial.println("pH = " + String(pHValue));
  Serial.println("PPM = " + String(tdsValue));
  Serial.println("Kualitas = " + String(z_kualitas));
  Serial.println("Pompa Up = " + String(z_up));
  Serial.println("Pompa Down = " + String(z_down));

  if (z_kualitas < 0.5) {
    kualitasAir = "BURUK";
  } else if (z_kualitas < 1.5) {
    kualitasAir = "CUKUP BAIK";
  } else {
    kualitasAir = "BAIK";
  }

  // Kontrol relay
  unsigned long now = millis();

  if (z_up < 1 && z_down < 1) {
    // Kondisi normal 
    sRelay1 = "OFF"; relayDurationUp = 0; relayUpAktif = false;
    sRelay2 = "OFF"; relayDurationDown = 0; relayDownAktif = false;
    digitalWrite(RELAY_PIN1, HIGH);  // relay OFF
    digitalWrite(RELAY_PIN2, HIGH);

  } else if (z_up >= 1 && z_up <= 3.5 && z_down < 1) {
    // Kondisi asam 
    sRelay1 = "ON"; relayDurationUp = 2000;
    relayStartUp = now; relayUpAktif = true;
    digitalWrite(RELAY_PIN1, LOW);   // relay ON
    sRelay2 = "OFF"; relayDurationDown = 0; relayDownAktif = false;
    digitalWrite(RELAY_PIN2, HIGH);

  } else if (z_up > 3.5 && z_down < 1) {
    // Kondisi sangat asam
    sRelay1 = "ON"; relayDurationUp = 5000;
    relayStartUp = now; relayUpAktif = true;
    digitalWrite(RELAY_PIN1, LOW);
    sRelay2 = "OFF"; relayDurationDown = 0; relayDownAktif = false;
    digitalWrite(RELAY_PIN2, HIGH);

  } else if (z_down >= 1 && z_down <= 3.5 && z_up < 1) {
    // Kondisi basa 
    sRelay2 = "ON"; relayDurationDown = 2000;
    relayStartDown = now; relayDownAktif = true;
    digitalWrite(RELAY_PIN2, LOW);
    sRelay1 = "OFF"; relayDurationUp = 0; relayUpAktif = false;
    digitalWrite(RELAY_PIN1, HIGH);

  } else if (z_down > 3.5 && z_up < 1) {
    // Kondisi sangat basa
    sRelay2 = "ON"; relayDurationDown = 5000;
    relayStartDown = now; relayDownAktif = true;
    digitalWrite(RELAY_PIN2, LOW);
    sRelay1 = "OFF"; relayDurationUp = 0; relayUpAktif = false;
    digitalWrite(RELAY_PIN1, HIGH);
  }
}