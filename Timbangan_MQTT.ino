#include <HX711_ADC.h>
#include <PubSubClient.h> // import Library
#include <ESP8266WiFi.h> // import Library

// --- Library untuk WiFiManager ---
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
// ---------------------------------

#if defined(ESP8266)|| defined(ESP32) || defined(AVR)
#include <EEPROM.h>
#endif

//pins:
const int HX711_dout = D2; //mcu > HX711 dout pin
const int HX711_sck = D6; //mcu > HX711 sck  

//HX711 constructor:
HX711_ADC LoadCell(HX711_dout, HX711_sck);

const char* guid                  = "73b8e613-081e-4aaa-a7fa-998992f85cae";

// --- Pengaturan WiFiManager ---
const char* wifiManagerAPName = "LSKK-IWK-LOADCELL-13"; // Nama AP untuk konfigurasi WiFi
const int wifiManagerTimeout = 50; // Timeout portal konfigurasi dalam detik
// ---------------------------------

// const String wifiSsid           = "TEKIDO"; // Dihapus, diganti WiFiManager
// const char* wifiPassword        = "iotworkshop2021"; // Dihapus, diganti WiFiManager
const char* mqttHost              = "iwkrmq.pptik.id"; // Deklarasi untuk link yang akan dituju
const char* mqttUserName          = "/trainerkit:trainerkit"; // Deklarasi untuk nama UserName DI RMQ /survey:survey
const char* mqttPassword          = "12345678"; // Deklarasi untuk Passwordnya di RMQ $surv3yy!
const char* mqttQueueTimbangan    = "Timbangan"; // Deklarasi untuk nama Queue di RMQ
// const char* CL = guid; // Dihapus, diganti MAC Address

WiFiClient espClient;
PubSubClient client(espClient);
byte mac[6]; //array temp mac address //mac memakai 6 byte
String MACAddress; // dari byte dirubah jadi string
////////////////////////////////////////
const int calVal_eepromAdress = 0;
unsigned long t = 0;

// --- Fungsi Setup WiFi Baru dengan WiFiManager ---
void setup_wifi() {
  Serial.println("Memulai konfigurasi WiFi...");
  
  WiFiManager wm;

  // Atur timeout portal konfigurasi
  wm.setConfigPortalTimeout(wifiManagerTimeout);

  Serial.println("Mencoba menghubungkan ke WiFi terakhir...");

  // autoConnect mencoba menghubungkan ke kredensial tersimpan.
  // Jika gagal, ia akan memulai AP dengan nama 'wifiManagerAPName'.
  if (!wm.autoConnect(wifiManagerAPName)) {
    // Blok ini akan dieksekusi jika portal konfigurasi timeout (50 detik)
    Serial.print("Portal konfigurasi timeout setelah ");
    Serial.print(wifiManagerTimeout);
    Serial.println(" detik.");
    Serial.println("Tidak ada kredensial baru diterima. Restart dan coba lagi...");
    delay(3000);
    ESP.restart(); // Restart ESP untuk mencoba lagi koneksi ke jaringan terakhir
  }

  // Jika sampai di sini, artinya WiFi berhasil terhubung
  Serial.println("");
  Serial.println("WiFi terhubung!");
  Serial.print("Alamat IP: ");
  Serial.println(WiFi.localIP());
}

String mac2String(byte ar[]) { 
  String s;
  for (byte i = 0; i < 6; ++i)
  {
    char buf[3];
    sprintf(buf, "%2X", ar[i]);
    s += buf;
    if (i < 5) s += ':';
  }
  return s;
}

void printMACAddress() {
  WiFi.macAddress(mac);
  MACAddress = mac2String(mac);
  Serial.println(MACAddress); // Tampilkan MacAddress
}

// --- Menambahkan fungsi callback (perbaikan bug) ---
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  // Kode ini subscribe ke topiknya sendiri,
  // tapi sepertinya tidak melakukan apa-apa dengan pesannya.
}

void reconnect() {
  // Loop until we're reconnected
  Serial.println("In reconnect...");
  printMACAddress(); // Pastikan MACAddress terisi
  Serial.print("Menggunakan Client ID: ");
  Serial.println(MACAddress);

  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if(client.connect(MACAddress.c_str(), mqttUserName, mqttPassword)) { // Diubah dari CL ke MACAddress
      client.subscribe(mqttQueueTimbangan);
      Serial.println("connected");
      Serial.print("Subcribed to: ");
      Serial.println(mqttQueueTimbangan);
      Serial.println('\n');

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200); delay(500);
  Serial.println();
  Serial.println("Starting...");

  setup_wifi(); // Memanggil fungsi WiFiManager baru
  printMACAddress();
  client.setServer(mqttHost, 1883);
  client.setCallback(callback); // Menambahkan setCallback (perbaikan bug)

  LoadCell.begin();
  //LoadCell.setReverseOutput(); //uncomment to turn a negative output value to positive
  float calibrationValue; // calibration value (see example file "Calibration.ino")
  //calibrationValue = 17163.93; // uncomment this if you want to set the calibration value in the sketch
#if defined(ESP8266)|| defined(ESP32)
  EEPROM.begin(512); // uncomment this if you use ESP8266/ESP32 and want to fetch the calibration value from eeprom
#endif
  EEPROM.get(calVal_eepromAdress, calibrationValue); // uncomment this if you want to fetch the calibration value from eeprom

  unsigned long stabilizingtime = 2000; // preciscion right after power-up can be improved by adding a few seconds of stabilizing time
  boolean _tare = true; //set this to false if you don't want tare to be performed in the next step
  LoadCell.start(stabilizingtime, _tare);
  if (LoadCell.getTareTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
    while (1);
  }
  else {
    LoadCell.setCalFactor(calibrationValue); // set calibration value (float)
    Serial.println("Startup is complete");
  }
}

void loop() {
  static boolean newDataReady = 0;
  const int serialPrintInterval = 0; //increase value to slow down serial print activity
  
  if (!client.connected()) {
    reconnect();
  }
  
  // check for new data/start next conversion:
  if (LoadCell.update()) newDataReady = true;

  // get smoothed value from the dataset:
  if (newDataReady) {
    if (millis() > t + serialPrintInterval) {
      float i = LoadCell.getData();
      if (i < 0) {
        i = 0;
      }
      float dataWeight;
      dataWeight = i;
      Serial.println(dataWeight);
      String convertDataWeight = String(dataWeight);
      String dataRMQ = String(String(guid) + "#" + convertDataWeight );
      char dataToMQTT[50];
      dataRMQ.toCharArray(dataToMQTT, sizeof(dataToMQTT)); 
      Serial.println("Ini Data untuk ke MQTT: ");
      Serial.println(dataToMQTT);
      //Serial.println(" Kg");
      
      client.publish(mqttQueueTimbangan,dataToMQTT);
      client.loop();
      delay(500); //
      Serial.print("Load_cell output val: ");
      Serial.println(i);
      newDataReady = 0;
      t = millis();
    }
  }

  // receive command from serial terminal, send 't' to initiate tare operation:
  if (Serial.available() > 0) {
    char inByte = Serial.read();
    if (inByte == 't') LoadCell.tareNoDelay();
  }

  // check if last tare operation is complete:
  if (LoadCell.getTareStatus() == true) {
    Serial.println("Tare complete");
  }
}