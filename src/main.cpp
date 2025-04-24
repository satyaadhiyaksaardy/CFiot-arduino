#include "secrets.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include "HX711.h"
#include <RTClib.h>
#include <NewPing.h>
#include "esp_sleep.h"

// ————— CONFIG —————
#define DEBUG            0       // 1 = Serial debug on
#define FILL_THRESHOLD   0.90f   // publish immediately if fill% ≥ this

// Sensor pins
#define MQ135_PIN   34
#define MQ4_PIN     35
#define HX711_DOUT  12
#define HX711_SCK   14
#define TRIG_PIN    27
#define ECHO_PIN    26

// Calibration
#define SCALE_FACTOR 1.0f
#define MAX_DEPTH_CM 100.0f
#define MIN_DEPTH_CM 10.0f

// Location & MQTT topic
const char* locId    = "TPS_001";
const char* topicPub = "foodwaste/TPS_001";

// Globals
RTC_DS3231   rtc;
HX711        scale;
WiFiClientSecure net;
PubSubClient     client(net);

// Persist across deep-sleep cycles
RTC_DATA_ATTR float lastPublishedFill = 0.0f;

// Debug macros
#if DEBUG
  #define DBG_PRINT(...)   Serial.print(__VA_ARGS__)
  #define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define DBG_PRINT(...)
  #define DBG_PRINTLN(...)
#endif

const char* daysOfWeek[] = {
  "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
};

// Compute fill percentage
float calcFill(float dist) {
  if (dist >= MAX_DEPTH_CM) return 0.0f;
  if (dist <= MIN_DEPTH_CM) return 1.0f;
  return 1.0f - ((dist - MIN_DEPTH_CM) / (MAX_DEPTH_CM - MIN_DEPTH_CM));
}

// Connect to AWS IoT, publish JSON, then disconnect
void publishReading(float weight, float fill, float nh3, float ch4,
                    const String& ts, const char* dow, bool isWeekend, bool pickDay) {
  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    DBG_PRINT(".");
  }
  DBG_PRINTLN("WiFi connected");

  // TLS credentials
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // MQTT
  client.setServer(AWS_IOT_ENDPOINT, 8883);
  if (!client.connect(THINGNAME)) {
    DBG_PRINTLN("MQTT connect failed");
    WiFi.disconnect(true);
    return;
  }
  DBG_PRINTLN("MQTT connected");

  // Build JSON payload
  StaticJsonDocument<256> doc;
  doc["Timestamp"]       = ts;
  doc["Weight_kg"]       = weight;
  doc["Fill_pct"]        = fill;
  doc["Gas_CH4_ppm"]     = ch4;
  doc["Gas_NH3_ppm"]     = nh3;
  doc["DayOfWeek"]       = dow;
  doc["IsWeekend"]       = isWeekend;
  doc["IsPickupDay"]     = pickDay;
  doc["LocationID"]      = locId;
  doc["Latitude"]        = 25.02654098984479;
  doc["Longitude"]       = 121.5447185774265;
  char buf[512];
  size_t n = serializeJson(doc, buf);

  client.publish(topicPub, buf, n);
  DBG_PRINTLN("Published");

  client.disconnect();
  WiFi.disconnect(true);
}

void setup() {
  #if DEBUG
    Serial.begin(115200);
    delay(100);
  #endif
  DBG_PRINTLN("Wake");

  // Init RTC
  if (!rtc.begin()) {
    DBG_PRINTLN("RTC init failed");
    while (true);
  }

  // Init HX711
  scale.begin(HX711_DOUT, HX711_SCK);
  scale.set_scale(SCALE_FACTOR);
  scale.tare();

  // HC-SR04 pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Read weight
  float weight = scale.get_units(5);
  scale.power_down();

  // Read distance
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 30000);
  float dist = dur > 0 ? (dur * 0.0343f) / 2.0f : MAX_DEPTH_CM;
  float fill = calcFill(dist);

  // Read gas sensors
  float nh3 = analogRead(MQ135_PIN);
  float ch4 = analogRead(MQ4_PIN);

  // Timestamp
  DateTime now = rtc.now();
  String ts = String(now.year()) + "-" + String(now.month()) + "-" + String(now.day()) +
              " " + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second());
  int dow = now.dayOfTheWeek();
  bool isWeekend = (dow == 0 || dow == 6);
  bool pickDay   = (fill >= FILL_THRESHOLD);

  DBG_PRINT("Fill="); DBG_PRINT(fill);
  DBG_PRINT(" LastPub="); DBG_PRINTLN(lastPublishedFill);

  // Publish if first run, significant change, or pickup threshold
  if (lastPublishedFill == 0.0f ||
      fabs(fill - lastPublishedFill) >= 0.05f ||
      pickDay) {
    publishReading(weight, fill, nh3, ch4, ts, daysOfWeek[dow], isWeekend, pickDay);
    lastPublishedFill = fill;
  } else {
    DBG_PRINTLN("Skip publish");
  }

  // —— ALIGN TO TOP OF NEXT HOUR ——
  // Compute seconds until next HH:00:00
  uint32_t secToNext =
      (60 - now.minute() - 1) * 60  // minutes left
    + (60 - now.second());          // seconds left
  DBG_PRINT("Sleeping for ");
  DBG_PRINT(secToNext / 60);
  DBG_PRINT("m ");
  DBG_PRINTLN(secToNext % 60);

  esp_sleep_enable_timer_wakeup(uint64_t(secToNext) * 1000000ULL);
  delay(100);
  esp_deep_sleep_start();
}

void loop() {
}
