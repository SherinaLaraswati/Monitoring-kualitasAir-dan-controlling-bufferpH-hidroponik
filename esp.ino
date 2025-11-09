#include <Firebase_ESP_Client.h> 
#include <SoftwareSerial.h>

#define WIFI_SSID "Sherina"
#define WIFI_PASSWORD "5herinaa"

// Deklarasi firebase dan pin
FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

// Pin
#define NODEMCU_RX_PIN D1 // GPIO4 - menerima dari Arduino TX
#define NODEMCU_TX_PIN D2  // GPIO5 - mengirim ke Arduino RX
#define LED_BUILTIN_PIN 2  

// Serial komunikasi 
SoftwareSerial arduinoSerial(NODEMCU_RX_PIN, NODEMCU_TX_PIN);

struct SensorData {
  float temperature;
  float tds;
  float pH;
  String kualitasAir;
  String statusP1;
  String statusP2;
  float waktup1;
  float waktup2;
  float z_kualitas; float z_up; float z_down;
  bool dataValid;
  int dataCount;
} sensorData;

struct SystemStatus {
  bool arduinoConnected;
  unsigned long lastDataReceived;
  unsigned long uptime;
  int totalPacketsReceived;
  int errorCount;
  String lastError;
  unsigned long startTime;
} systemStatus;

// Variable waktu
unsigned long lastDataCheck = 0;
unsigned long lastStatsDisplay = 0;

const unsigned long DATA_TIMEOUT = 5000;        // 5 detik tanpa data = disconnect
const unsigned long STATS_INTERVAL = 15000;      // Display stats setiap 15 detik

void setup() {
  Serial.begin(9600);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");

  config.host = "hidroponik-f05fd-default-rtdb.firebaseio.com/";                  
  config.signer.tokens.legacy_token = "IqdpZsDmcJl54NzliVRxBiNV4xqhcR6hbowNdLZa";  // Database secret

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  delay(500);

  Serial.println();
  Serial.println("=====================================");
  Serial.println("     NodeMCU Serial Data Receiver");
  Serial.println("=====================================");

  pinMode(LED_BUILTIN_PIN, OUTPUT);
  digitalWrite(LED_BUILTIN_PIN, HIGH);  // LED off

  // Inisialisasi komunikasi dengan Arduino
  arduinoSerial.begin(9600);
  Serial.println("SoftwareSerial initialized pada D1, D2");

  initializeData();

  Serial.println("=====================================");
  Serial.println("System ready! Waiting for Arduino data...");
  Serial.println("Commands: Type 'stats' for statistics");
  Serial.println("          Type 'reset' to reset counters");
  Serial.println("          Type 'help' for all commands");
  Serial.println("=====================================");

  // LED, 3: jumlah kedipan, durasi delay tiap kedipan 200ms
  blinkLED(3, 200);
}

void loop() {
  // Baca data dari arduino 
  handleArduinoData();

  // Cek status koneksi arduino
  if (millis() - lastDataCheck >= DATA_TIMEOUT) {
    checkArduinoConnection();

    firebase();
    lastDataCheck = millis();
  }

  if (millis() - lastStatsDisplay >= STATS_INTERVAL) {
    displayStatistics();
    lastStatsDisplay = millis();
  }

  updateSystemStatus();

  delay(10);
}

void initializeData() {
  sensorData.temperature = 0.0;
  sensorData.tds = 0.0;
  sensorData.pH = 0.0;
  sensorData.statusP1 = "";
  sensorData.statusP2 = "";
  sensorData.kualitasAir = "";
  sensorData.waktup1 = 0.0;
  sensorData.waktup2 = 0.0;
  sensorData.z_kualitas = 0.0;
  sensorData.z_up = 0.0;
  sensorData.z_down = 0.0;
  sensorData.dataValid = false;
  sensorData.dataCount = 0;

  systemStatus.arduinoConnected = false;
  systemStatus.lastDataReceived = 0;
  systemStatus.startTime = millis();
  systemStatus.uptime = 0;
  systemStatus.totalPacketsReceived = 0;
  systemStatus.errorCount = 0;
  systemStatus.lastError = "";
}

// Komunikasi arduino
void handleArduinoData() {
  if (arduinoSerial.available()) {
    String receivedData = arduinoSerial.readStringUntil(';');
    arduinoSerial.read();
    receivedData.trim();

    if (receivedData.length() > 0) {
      Serial.println();
      Serial.println("<<< RECEIVED: " + receivedData);
      systemStatus.lastDataReceived = millis();
      systemStatus.totalPacketsReceived++;

      // Parse dan proses data
      if (parseData(receivedData)) {
        // Kirim konfirmasi ke Arduino
        arduinoSerial.print("OK\n");
        Serial.println(">>> SENT: OK");
        Serial.println(">> Data dari Arduino:");
        Serial.println("SUHU: " + String(sensorData.temperature));
        Serial.println("PPM : " + String(sensorData.tds));
        Serial.println("pH  : " + String(sensorData.pH));
        Serial.println("KUALITAS  : " + sensorData.kualitasAir);
        Serial.println("STATUSP1  : " + sensorData.statusP1);
        Serial.println("STATUSP2  : " + sensorData.statusP2);
        Serial.println("WAKTU P1  : " + String(sensorData.waktup1));
        Serial.println("WAKTU P2  : " + String(sensorData.waktup2));
        Serial.println("OUTPUT-KUALITAS  : " + String(sensorData.z_kualitas));
        Serial.println("OUTPUT-POMPA1UP : " + String(sensorData.z_up));
        Serial.println("OUTPUT-POMPA2DOWN  : " + String(sensorData.z_down));

        systemStatus.arduinoConnected = true;

        // Indikator LED
        blinkLED(1, 50);
      } else {
        arduinoSerial.print("ERROR:PARSE_FAILED\n");
        Serial.println(">>> SENT: ERROR:PARSE_FAILED");
        systemStatus.errorCount++;
        systemStatus.lastError = "Parse failed for: " + receivedData;
        blinkLED(3, 100);
      }
    }
  }
}

bool parseData(String data) {
   if (data.startsWith("SUHU:") || data.startsWith("PPM:") ||
      data.startsWith("pH:") || data.startsWith("KA:") ||
      data.startsWith("P1:") || data.startsWith("P2:") ||
      data.startsWith("WP1:") || data.startsWith("WP2:") ||
      data.startsWith("OKA:") || data.startsWith("OP1:") ||
      data.startsWith("OP2:")) {
    return parseSensorData(data);
  }
  Serial.println("✗ Unknown data format: " + data);
  return false;
}

bool parseSensorData(String data) {
  // Suhu
  int tempIndex = data.indexOf("SUHU:");
  if (tempIndex != -1) {
    int end = data.indexOf(",", tempIndex);
    sensorData.temperature = data.substring(tempIndex + 5, end).toFloat();
  }

  // PPM
  int ppmIndex = data.indexOf("PPM:");
  if (ppmIndex != -1) {
    int end = data.indexOf(",", ppmIndex);
    sensorData.tds = data.substring(ppmIndex + 4, end).toInt();
  }

  // pH
  int phIndex = data.indexOf("pH:");
  if (phIndex != -1) {
    int end = data.indexOf(",", phIndex);
    sensorData.pH = data.substring(phIndex + 3, end).toFloat();
  }

  // Kualitas
  int kualitasIndex = data.indexOf("KA:");
  if (kualitasIndex != -1) {
    int end = data.indexOf(",", kualitasIndex);
    sensorData.kualitasAir = (end == -1)
      ? data.substring(kualitasIndex + 3)
      : data.substring(kualitasIndex + 3, end);
  }

  // Pompa1
  int p1Index = data.indexOf("P1:");
  if (p1Index != -1) {
    int end = data.indexOf(",", p1Index);
    sensorData.statusP1 = (end == -1)
      ? data.substring(p1Index + 3)
      : data.substring(p1Index + 3, end);
  }

  // Pompa2
  int p2Index = data.indexOf("P2:");
  if (p2Index != -1) {
    int end = data.indexOf(",", p2Index);
    sensorData.statusP2 = (end == -1)
      ? data.substring(p2Index + 3)
      : data.substring(p2Index + 3, end);
  }

  // WwaktuP1
  int w1Index = data.indexOf("WP1:");
  if (w1Index != -1) {
    int end = data.indexOf(",", w1Index);
    sensorData.waktup1 = data.substring(w1Index + 4, end).toFloat();
  }

   // WaktuP2
  int w2Index = data.indexOf("WP2:");
  if (w2Index != -1) {
    int end = data.indexOf(",", w2Index);
    sensorData.waktup2 = data.substring(w2Index + 4, end).toFloat();
  }

  // z-kualitas air
  int zKaIndex = data.indexOf("OKA:");
  if (zKaIndex != -1) {
    int end = data.indexOf(",", zKaIndex);
    sensorData.z_kualitas = data.substring(zKaIndex + 4, end).toFloat();
  }

  // z-pompa up
  int zupIndex = data.indexOf("OP1:");
  if (zupIndex != -1) {
    int end = data.indexOf(",", zupIndex);
    sensorData.z_up = data.substring(zupIndex + 4, end).toFloat();
  }
  // z-pompa down
  int zdownIndex = data.indexOf("OP2:");
  if (zdownIndex != -1) {
    int end = data.indexOf(",", zdownIndex);
    sensorData.z_down = (end == -1)
      ? data.substring(zdownIndex + 4).toFloat()
      : data.substring(zdownIndex + 4, end).toFloat();
  }

  sensorData.dataValid = true;
  sensorData.dataCount++;
  return true;
}

void checkArduinoConnection() {
  unsigned long timeSinceLastData = millis() - systemStatus.lastDataReceived;

  if (timeSinceLastData > DATA_TIMEOUT && systemStatus.arduinoConnected) {
    Serial.println();
    Serial.println("⚠ ARDUINO CONNECTION TIMEOUT!");
    Serial.println("Last data received " + String(timeSinceLastData / 1000) + " seconds ago");
    systemStatus.arduinoConnected = false;
    sensorData.dataValid = false;
  }
}

void firebase() {
  // Kirim data ke Firebase jika Firebase siap
  if (Firebase.ready()) {
    Serial.println("FIREBASE IS READY!");

    // Kirim Temperature
    if (Firebase.RTDB.setFloat(&firebaseData, "/SensorData/suhuAir", sensorData.temperature)) {
      Serial.println("suhuAir updated");
    } else {
      Serial.println("suhuAir update failed: " + firebaseData.errorReason());
    }

    // Kirim PPM
    if (Firebase.RTDB.setInt(&firebaseData, "/SensorData/PPM", sensorData.tds)) {
      Serial.println("PPM updated");
    } else {
      Serial.println("PPM update failed: " + firebaseData.errorReason());
    }

    // Kirim pH
    if (Firebase.RTDB.setFloat(&firebaseData, "/SensorData/pH", sensorData.pH)) {
      Serial.println("pH updated");
    } else {
      Serial.println("pH update failed: " + firebaseData.errorReason());
    }

    if (sensorData.kualitasAir != "")
      Firebase.RTDB.setString(&firebaseData, "/SensorData/Status", sensorData.kualitasAir);
      Serial.println("Status updated");

    if (sensorData.statusP1 != "")
      Firebase.RTDB.setString(&firebaseData, "/SensorData/statusP1", sensorData.statusP1);
      Serial.println("statusp1 updated");

    if (sensorData.statusP2 != "")
      Firebase.RTDB.setString(&firebaseData, "/SensorData/statusP2", sensorData.statusP2);
      Serial.println("statusp2 updated");

    Firebase.RTDB.setFloat(&firebaseData, "/SensorData/waktuP1", sensorData.waktup1);
    Serial.println("waktup1 updated");

    Firebase.RTDB.setFloat(&firebaseData, "/SensorData/waktuP2", sensorData.waktup2);
    Serial.println("waktup2 updated");

    if (Firebase.RTDB.setFloat(&firebaseData, "/SensorData/OutputKualitasAir", sensorData.z_kualitas))
      Serial.println("OutputKualitasAir updated");
      
    if (Firebase.RTDB.setFloat(&firebaseData, "/SensorData/OutputPompa1Up", sensorData.z_up))
      Serial.println("OutputPompa1Up updated");

    if (Firebase.RTDB.setFloat(&firebaseData, "/SensorData/OutputPompa2Down", sensorData.z_down))
      Serial.println("OutputPompa2Down updated");

  } else {
    Serial.println("FIREBASE NOT READY!");
  }
}

void displayStatistics() {
  unsigned long uptime = millis() - systemStatus.startTime;

  Serial.println();
  Serial.println("╔═══════════════════════════════════════════╗");
  Serial.println("║              SYSTEM STATISTICS            ║");
  Serial.println("╠═══════════════════════════════════════════╣");
  Serial.printf("║ Uptime:         %8lu ms           ║\n", uptime);
  Serial.printf("║ Total Packets:  %8d                 ║\n", systemStatus.totalPacketsReceived);
  Serial.printf("║ Valid Data:     %8d                 ║\n", sensorData.dataCount);
  Serial.printf("║ Errors:         %8d                 ║\n", systemStatus.errorCount);
  Serial.printf("║ Arduino Status: %-15s       ║\n", systemStatus.arduinoConnected ? "CONNECTED" : "DISCONNECTED");
  if (systemStatus.lastError.length() > 0) {
    Serial.println("║ Last Error:                               ║");
    Serial.println("║ " + systemStatus.lastError.substring(0, 41) + " ║");
  }
  Serial.println("╚═══════════════════════════════════════════╝");
}

void updateSystemStatus() {
  systemStatus.uptime = millis() - systemStatus.startTime;
}

void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_BUILTIN_PIN, LOW);  // LED on
    delay(delayMs);
    digitalWrite(LED_BUILTIN_PIN, HIGH);  // LED off
    if (i < times - 1) delay(delayMs);
  }
}