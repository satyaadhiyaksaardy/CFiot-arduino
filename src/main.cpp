#include "secrets.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include "HX711.h"
#include <RTClib.h>
#include <NewPing.h>

// Sensor Pin Definitions
#define MQ135_PIN 34
#define MQ4_PIN 35
#define HX711_DOUT 12
#define HX711_SCK 14
#define TRIG_PIN 27
#define ECHO_PIN 26

// Define Mode
#define MODE 2 // 1 for data real sensor, 2 for simulation data

// HX711 Calibration
#define SCALE_FACTOR 1.0

// RTC Initialization
RTC_DS3231 rtc;

// HX711 Initialization
HX711 scale;

// HC-SR04 Initialization
long duration;
float distance;

// Data Variables
float weight;
float fill_percentage;
float NH3;
float CH4;
String dayOfWeek;
bool isWeekend;
bool isPickingDay;
String timestamp;
String locId = "TPS_001";
float latitude = 25.02654098984479;
float longitude = 121.5447185774265;

String topic = "foodwaste/" + locId;
 
WiFiClientSecure net = WiFiClientSecure();
PubSubClient client(net);

void messageHandler(char* topic, byte* payload, unsigned int length)
{
  Serial.print("incoming: ");
  Serial.println(topic);
 
  StaticJsonDocument<200> doc;
  deserializeJson(doc, payload);
  const char* message = doc["message"];
  Serial.println(message);
}
 
void connectAWS()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
 
  Serial.println("Connecting to Wi-Fi");
 
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
 
  // Configure WiFiClientSecure to use the AWS IoT device credentials
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);
 
  // Connect to the MQTT broker on the AWS endpoint we defined earlier
  client.setServer(AWS_IOT_ENDPOINT, 8883);
 
  // Create a message handler
  client.setCallback(messageHandler);
 
  Serial.println("Connecting to AWS IOT");
 
  while (!client.connect(THINGNAME))
  {
    Serial.print(".");
    delay(100);
  }
 
  if (!client.connected())
  {
    Serial.println("AWS IoT Timeout!");
    return;
  }
 
  // Subscribe to a topic
  client.subscribe(topic.c_str());
 
  Serial.println("AWS IoT Connected!");
}

float calculateFillPercentage(float distance) {
  // Calibrate 
  float maxDistance = 100.0; // Max trashbin depth
  float minDistance = 10.0; // Min trashbin depth
  float fillPercentage = 0.0;

  if (distance >= maxDistance) {
    fillPercentage = 0.0;
  } else if (distance <= minDistance) {
    fillPercentage = 1.0;
  } else {
    fillPercentage = 1.0 - ((distance - minDistance) / (maxDistance - minDistance));
  }

  return fillPercentage;
}

String getDayOfWeek(int dayIndex) {
  String days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  return days[dayIndex];
}

void readSensors() {
  // Read HX711 Weight Sensor
  weight = scale.get_units();

  // Read HC-SR04 Distance Sensor
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  duration = pulseIn(ECHO_PIN, HIGH);
  distance = (duration * 0.0343) / 2; // Calculate distance in cm
  fill_percentage = calculateFillPercentage(distance);

  // Read MQ-135 and MQ-4 Gas Sensors
  NH3 = analogRead(MQ135_PIN);
  CH4 = analogRead(MQ4_PIN);

  // Read RTC Time
  DateTime now = rtc.now();
  timestamp = String(now.year(), 10) + "-" + String(now.month(), 10) + "-" + String(now.day(), 10) + " " + String(now.hour(), 10) + ":" + String(now.minute(), 10) + ":" + String(now.second(), 10);
  dayOfWeek = getDayOfWeek(now.dayOfTheWeek());
  isWeekend = (now.dayOfTheWeek() == 0 || now.dayOfTheWeek() == 6);
  isPickingDay = (fill_percentage >= 0.9);
}

void readSensorsSim() {
  // Simulated HX711 Weight Sensor
  weight = random(0, 15000) / 1000.0; // Random weight between 0 and 15 kg

  // Simulated HC-SR04 Distance Sensor
  distance = random(10, 100); // Random distance between 10 and 100 cm
  fill_percentage = calculateFillPercentage(distance);

  // Simulated MQ-135 and MQ-4 Gas Sensors
  NH3 = random(0, 1000) / 10.0; // Random NH3 value
  CH4 = random(0, 1000) / 10.0; // Random CH4 value

  // Simulated RTC Time
  DateTime now(2024, 3, 11, random(0, 23), random(0, 59), random(0, 59)); // Simulated date and time
  timestamp = String(now.year(), 10) + "-" + String(now.month(), 10) + "-" + String(now.day(), 10) + " " + String(now.hour(), 10) + ":" + String(now.minute(), 10) + ":" + String(now.second(), 10);
  dayOfWeek = getDayOfWeek(now.dayOfTheWeek());
  isWeekend = (now.dayOfTheWeek() == 0 || now.dayOfTheWeek() == 6);
  isPickingDay = (fill_percentage >= 0.9); // Simulated picking day
}
 
void publishMessage() {
  StaticJsonDocument<200> doc;
  doc["Timestamp"] = timestamp;
  doc["Berat (kg)"] = weight;
  doc["Fill_Percentage (%)"] = fill_percentage;
  doc["Kualitas_Gas_CH4 (ppm)"] = CH4;
  doc["Kualitas_Gas_NH3 (ppm)"] = NH3;
  doc["Hari_dalam_Minggu"] = dayOfWeek;
  doc["Akhir_Pekan"] = isWeekend;
  doc["Hari_Pengambilan"] = isPickingDay;
  doc["Lokasi_ID"] = locId;
  doc["Latitude"] = latitude;
  doc["Longitude"] = longitude;

  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);

  client.publish(topic.c_str(), jsonBuffer);
}
 
void setup()
{
  Serial.begin(115200);
  
  scale.begin(HX711_DOUT, HX711_SCK);
  scale.set_scale(SCALE_FACTOR);
  scale.tare();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  if (!rtc.begin()) {
    Serial.println("RTC tidak ditemukan!");
    while (1);
  }

  connectAWS();
}

void loop()
{
  if (MODE > 1) {
    readSensorsSim();
  } else {
    readSensors();
  }
  publishMessage();
  client.loop();
  delay(1000);
}