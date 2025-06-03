#include "secrets.h" // Wi-Fi & AWS certs
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include "HX711.h"
#include <NewPing.h>
#include <time.h> // NTP time functions

// ─────────── Indicator ───────────
unsigned long lastBlink = 0;
bool blinkState = false;

// ─────────── Pin Definitions ───────────
#define LED_YELLOW_PIN 32
#define LED_GREEN_PIN 33
#define MQ135_PIN 35
#define MQ4_PIN 34
#define HX711_DOUT 16
#define HX711_SCK 4
#define TRIG_PIN 27
#define ECHO_PIN 26

// ─────────── Mode ───────────
#define MODE 1 // 1 = real sensors, 2 = simulation

// ─────────── HX711 ───────────
#define SCALE_FACTOR 480.4793814433
HX711 scale;

// ─────────── MQ Sensor Calibration Constants ───────────
#define RL_MQ4 20000.0        // Load resistor for MQ-4 (Ω)
#define RL_MQ135 10000.0      // Load resistor for MQ-135 (Ω)
#define MQ4_CLEAN_AIR_F 4.4   // Clean air factor from datasheet
#define MQ135_CLEAN_AIR_F 3.6 // Clean air factor from datasheet

// Curve parameters: log10(Rs/Ro) = A·log10(ppm) + B
const float MQ4_A = -0.38;
const float MQ4_B = 1.42;
const float NH3_A = -0.47;
const float NH3_B = 1.68;

float Ro_MQ4 = 0;   // Baseline resistance for MQ-4
float Ro_MQ135 = 0; // Baseline resistance for MQ-135

// ─────────── AWS IoT ───────────
WiFiClientSecure net;
PubSubClient client(net);
String locId = "TPS_001";
float latitude = 25.012214781105286;
float longitude = 121.54099861359693;
String topic = "foodwaste/" + locId;

// ─────────── Data Vars ───────────
float weight, fill_percentage;
float CH4_ppm, NH3_ppm;
String timestamp, dayOfWeek;
bool isWeekend, isPickingDay;

// ─────────── MQTT Callback ───────────
void messageHandler(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Incoming on ");
  Serial.print(topic);
  Serial.print(": ");
  StaticJsonDocument<200> doc;
  deserializeJson(doc, payload, length);
  Serial.println(doc["message"] | "");
}

// Connect Wi-Fi, sync time via NTP, then connect to AWS IoT
void connectAWS()
{
  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" OK");

  // NTP sync
  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Synchronizing time via NTP");
  time_t now = time(nullptr);
  while (now < (8 * 3600))
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println(" done");

  // TLS credentials
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // AWS IoT setup
  client.setServer(AWS_IOT_ENDPOINT, 8883);
  client.setCallback(messageHandler);

  Serial.print("Connecting to AWS IoT");
  while (!client.connect(THINGNAME))
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected");
  client.subscribe(topic.c_str());
}

// ─────────── Helpers ───────────
float calculateFillPercentage(float d)
{
  const float maxD = 100.0, minD = 10.0;
  if (d >= maxD)
    return 0.0;
  if (d <= minD)
    return 1.0;
  return 1.0 - ((d - minD) / (maxD - minD));
}

float getRs(int raw, float RL)
{
  float v = raw * (3.3 / 4095.0);
  return (3.3 - v) / v * RL;
}

float getPPM(float Rs, float Ro, float A, float B)
{
  float ratio = Rs / Ro;
  return pow(10.0, (log10(ratio) - B) / A);
}

void calibrateSensors()
{
  const int SAMPLES = 100;
  const int INTERVAL = 50;
  float sum4 = 0, sum135 = 0;

  Serial.println("Calibrating MQ sensors in clean air...");
  for (int i = 0; i < SAMPLES; i++)
  {
    sum4 += getRs(analogRead(MQ4_PIN), RL_MQ4);
    sum135 += getRs(analogRead(MQ135_PIN), RL_MQ135);
    delay(INTERVAL);
  }

  Ro_MQ4 = (sum4 / SAMPLES) / MQ4_CLEAN_AIR_F;
  Ro_MQ135 = (sum135 / SAMPLES) / MQ135_CLEAN_AIR_F;
  Serial.print("Ro_MQ4 = ");
  Serial.println(Ro_MQ4);
  Serial.print("Ro_MQ135 = ");
  Serial.println(Ro_MQ135);
}

void readSensors()
{
  weight = scale.get_units();

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = (duration * 0.0343) / 2.0;
  fill_percentage = calculateFillPercentage(distance);

  float rs4 = getRs(analogRead(MQ4_PIN), RL_MQ4);
  CH4_ppm = getPPM(rs4, Ro_MQ4, MQ4_A, MQ4_B);

  float rs135 = getRs(analogRead(MQ135_PIN), RL_MQ135);
  NH3_ppm = getPPM(rs135, Ro_MQ135, NH3_A, NH3_B);

  // Build timestamp
  time_t now = time(nullptr);
  struct tm ti;
  gmtime_r(&now, &ti);
  timestamp = String(ti.tm_year + 1900) + "-" + String(ti.tm_mon + 1) + "-" + String(ti.tm_mday) +
              " " + String(ti.tm_hour) + ":" + String(ti.tm_min) + ":" + String(ti.tm_sec);
  dayOfWeek = String(ti.tm_wday);
  isWeekend = (ti.tm_wday == 0 || ti.tm_wday == 6);
  isPickingDay = (fill_percentage >= 0.9);

  Serial.printf("Weight: %.2f g\n", weight);
  Serial.printf("Distance: %.1f cm (%.1f%%)\n", distance, fill_percentage * 100);
  Serial.printf("CH4: %.2f ppm, NH3: %.2f ppm\n", CH4_ppm, NH3_ppm);
  Serial.println("Timestamp: " + timestamp);
  Serial.printf("Weekend: %s, PickupDay: %s\n", isWeekend ? "Yes" : "No", isPickingDay ? "Yes" : "No");
  Serial.println("-------------------------------");
}

void readSensorsSim()
{
  weight = random(0, 15000) / 1000.0;
  float dist = random(10, 100);
  fill_percentage = calculateFillPercentage(dist);
  CH4_ppm = random(0, 1000) / 1.0;
  NH3_ppm = random(0, 1000) / 1.0;

  time_t now = time(nullptr);
  struct tm ti;
  gmtime_r(&now, &ti);
  timestamp = String(ti.tm_year + 1900) + "-" + String(ti.tm_mon + 1) + "-" + String(ti.tm_mday) +
              " " + String(ti.tm_hour) + ":" + String(ti.tm_min) + ":" + String(ti.tm_sec);
  dayOfWeek = String(ti.tm_wday);
  isWeekend = (ti.tm_wday == 0 || ti.tm_wday == 6);
  isPickingDay = (fill_percentage >= 0.9);
}

void publishMessage()
{
  StaticJsonDocument<256> doc;
  doc["Timestamp"] = timestamp;
  doc["Berat (kg)"] = weight;
  doc["Fill (%)"] = fill_percentage;
  doc["CH4 (ppm)"] = CH4_ppm;
  doc["NH3 (ppm)"] = NH3_ppm;
  doc["Day"] = dayOfWeek;
  doc["Weekend"] = isWeekend;
  doc["PickupDay"] = isPickingDay;
  doc["LocID"] = locId;
  doc["Latitude"] = latitude;
  doc["Longitude"] = longitude;

  char buf[512];
  size_t len = serializeJson(doc, buf, sizeof(buf));
  buf[len] = '\0'; // ensure null-termination
  client.publish(topic.c_str(), (uint8_t *)buf, len);
}

void setup()
{
  Serial.begin(115200);

  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);

  scale.begin(HX711_DOUT, HX711_SCK);
  scale.set_scale(SCALE_FACTOR);
  scale.tare();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Connect AWS (includes Wi-Fi and NTP sync)
  client.setBufferSize(512);
  connectAWS();

  // Calibrate gas sensors after NTP sync
  if (MODE == 1)
    calibrateSensors();
}

void loop()
{
  if (MODE == 1)
    readSensors();
  else
    readSensorsSim();

  publishMessage();
  client.loop();

  if (!client.connected())
  {
    if (millis() - lastBlink >= 500)
    {
      blinkState = !blinkState;
      digitalWrite(LED_GREEN_PIN, blinkState);
      digitalWrite(LED_YELLOW_PIN, blinkState);
      lastBlink = millis();
    }
  }
  else
  {
    digitalWrite(LED_YELLOW_PIN, fill_percentage >= 0.9 ? HIGH : LOW);
    digitalWrite(LED_GREEN_PIN, fill_percentage < 0.9 ? HIGH : LOW);
  }

  delay(10000);
}
