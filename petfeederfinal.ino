#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "RTClib.h"
#include <ESP32Servo.h>

// ======= WIFI CONFIG =======
const char* WIFI_SSID = "GALAXY16";
const char* WIFI_PASS = "kalash13";

// ======= PIN CONFIG ========
const int SERVO_PIN = 13;
const int LED_PIN   = 2;

// Servo angles
const int SERVO_CLOSED_ANGLE = 0;
const int SERVO_OPEN_ANGLE   = 45;

// Timing
const unsigned long AUTO_INTERVAL_MS = 30000;  // 30s
const unsigned long DISPENSE_TIME_MS = 1000;   // 1s

WebServer server(80);
RTC_DS3231 rtc;
Servo feederServo;
unsigned long lastAutoDispenseMs = 0;

// ======= SMALL, CLEAN BOOTSTRAP UI =======
const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <title>Smart Pet Feeder</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />

  <link
    href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css"
    rel="stylesheet"
  />

  <style>
    body {
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #eef2ff;
    }
    .card-rounded {
      border-radius: 16px;
      box-shadow: 0 8px 20px rgba(15, 23, 42, 0.08);
    }
    #time, #date {
      font-size: 1.9rem;
      font-weight: 700;
    }
    .btn-feeder {
      border-radius: 999px;
      font-size: 1.2rem;
      font-weight: 700;
      padding: 0.8rem 1.2rem;
    }
    .center-container {
      max-width: 800px;
      margin: auto;
    }
  </style>
</head>
<body>

  <div class="container py-4 center-container">

    <div class="text-center mb-4">
      <span class="badge rounded-pill bg-primary-subtle text-primary fw-semibold">
        Smart IoT Device
      </span>
      <h1 class="fw-bold mt-3">Automatic Pet Feeder</h1>
      
    </div>

    <div class="row g-3 justify-content-center">

      <!-- Project Card -->
      <div class="col-md-12">
        <div class="card card-rounded p-3 text-center">
          <h6 class="text-uppercase text-muted small mb-1">Project</h6>
          <h3 class="fw-bold mb-2">Automatic Pet Feeder</h3>
          <p class="text-muted mb-0">ESP32 + DS3231 + Servo based feeder with web control.</p>
        </div>
      </div>

      <!-- TIME Card -->
      <div class="col-md-6">
        <div class="card card-rounded p-3 text-center">
          <h6 class="text-uppercase text-muted small mb-1">Time (RTC)</h6>
          <div id="time">Loading...</div>
          <p class="text-muted mb-0 mt-2">Nepal Time (UTC + 5:45).</p>
        </div>
      </div>

      <!-- DATE Card -->
      <div class="col-md-6">
        <div class="card card-rounded p-3 text-center">
          <h6 class="text-uppercase text-muted small mb-1">Date (RTC)</h6>
          <div id="date">Loading...</div>
          <p class="text-muted mb-0 mt-2">Live date from DS3231 module.</p>
        </div>
      </div>

      <!-- Manual Control Card -->
      <div class="col-md-12">
        <div class="card card-rounded p-3 text-center">
          <h6 class="text-uppercase text-muted small mb-2">Manual Control</h6>
          <button class="btn btn-primary btn-feeder w-100" onclick="dispenseNow()">Dispense Now</button>
          <div id="status" class="text-secondary mt-2">Waiting for command...</div>
         
        </div>
      </div>

      <!-- EXTRA CARD -->
      <div class="col-md-12">
        <div class="card card-rounded p-3 text-center">
          <h6 class="text-uppercase text-muted small mb-1">PROJECT MEMBERS</h6>
          <p class="mb-0">
          <strong>Kalash Manandhar, 
          Regam Khadka, 
          Dikshyanta Khulal<strong>
          </p>
        </div>
      </div>

    </div>
  </div>

  <script>
    function updateTime() {
      fetch('/time')
        .then(r => r.text())
        .then(t => {
          const parts = t.split(" ");
          document.getElementById('time').textContent = parts[0];
          document.getElementById('date').textContent = parts[1];
        })
        .catch(err => {
          document.getElementById('time').textContent = "Error";
          document.getElementById('date').textContent = "Error";
        });
    }

    function dispenseNow() {
      const statusEl = document.getElementById('status');
      statusEl.textContent = "Dispensing...";
      statusEl.className = "mt-2 text-primary";

      fetch('/dispense')
        .then(r => r.text())
        .then(txt => {
          statusEl.textContent = txt;
          statusEl.className = "mt-2 text-success";
        })
        .catch(err => {
          statusEl.textContent = "Error sending command.";
          statusEl.className = "mt-2 text-danger";
        });
    }

    setInterval(updateTime, 1000);
    updateTime();
  </script>

</body>
</html>
)rawliteral";

// ======= HELPERS =======
String getRTCTimeString() {
  DateTime now = rtc.now();   // We set RTC to Nepal time in setup
  char buf[32];
  snprintf(buf, sizeof(buf),
           "%02u:%02u:%02u %04u-%02u-%02u",
           now.hour(), now.minute(), now.second(),
           now.year(), now.month(), now.day());
  return String(buf);
}

void doDispense(const char* sourceLabel) {
  Serial.println();
  Serial.println("========== DISPENSE START ==========");
  Serial.print("Source       : "); Serial.println(sourceLabel);
  Serial.print("RTC time     : "); Serial.println(getRTCTimeString());
  Serial.print("Servo angles : CLOSED="); Serial.print(SERVO_CLOSED_ANGLE);
  Serial.print(" -> OPEN="); Serial.println(SERVO_OPEN_ANGLE);

  feederServo.write(SERVO_OPEN_ANGLE);
  digitalWrite(LED_PIN, HIGH);
  Serial.println("Servo OPEN. Waiting 1s...");
  delay(DISPENSE_TIME_MS);

  feederServo.write(SERVO_CLOSED_ANGLE);
  digitalWrite(LED_PIN, LOW);
  Serial.println("Servo CLOSED.");
  Serial.println("=========== DISPENSE END ===========");
}

// ======= HTTP HANDLERS =======
void handleRoot() {
  Serial.print("[HTTP] GET "); Serial.println(server.uri());
  server.send(200, "text/html", INDEX_HTML);
}

void handleTime() {
  String t = getRTCTimeString();
  Serial.print("[HTTP] GET /time -> "); Serial.println(t);
  server.send(200, "text/plain", t);
}

void handleDispense() {
  Serial.println("[HTTP] GET /dispense -> Manual dispense requested");
  doDispense("MANUAL_BUTTON");
  server.send(200, "text/plain", "Manual dispense completed.");
}

void handleNotFound() {
  Serial.print("[HTTP] 404 -> "); Serial.println(server.uri());
  server.send(404, "text/plain", "Not found");
}

// ======= SETUP =======
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("===== Smart Pet Feeder (Stable UI) Booting =====");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // RTC
  Serial.println("[RTC] Initializing DS3231...");
  Wire.begin();
  if (!rtc.begin()) {
    Serial.println("[RTC] ERROR: RTC not found! Halting.");
    while (1) delay(1000);
  }

  // Set RTC to compile time on every boot (your PC's local time, i.e. Nepal)
  Serial.println("[RTC] Setting RTC to compile time (Nepal local).");
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));//eakchoti chalauda panui pugni function rtc bigreko xaina vane
  Serial.print("[RTC] Current RTC time: ");
  Serial.println(getRTCTimeString());

  // Servo
  Serial.println("[SERVO] Attaching...");
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  int ch = feederServo.attach(SERVO_PIN, 500, 2400);
  if (ch >= 0) {
    Serial.print("[SERVO] Attached on pin "); Serial.print(SERVO_PIN);
    Serial.print(" (channel "); Serial.print(ch); Serial.println(")");
  } else {
    Serial.println("[SERVO] ERROR: attach failed!");
  }
  feederServo.write(SERVO_CLOSED_ANGLE);

  // WiFi
  Serial.print("[WIFI] Connecting to "); Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint8_t retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 60) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Connected!");
    Serial.print("[WIFI] IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WIFI] FAILED to connect.");
  }

  // Web server
  Serial.println("[HTTP] Setting up routes...");
  server.on("/", HTTP_GET, handleRoot);
  server.on("/time", HTTP_GET, handleTime);
  server.on("/dispense", HTTP_GET, handleDispense);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[HTTP] Server started on port 80.");

  lastAutoDispenseMs = millis();
  Serial.println("===== Setup complete. Entering loop. =====");
}

// ======= LOOP =======
void loop() {
  server.handleClient();

  unsigned long nowMs = millis();
  if (nowMs - lastAutoDispenseMs >= AUTO_INTERVAL_MS) {
    lastAutoDispenseMs = nowMs;
    Serial.println("[AUTO] 30s interval reached – auto dispense.");
    doDispense("AUTO_INTERVAL");
  }
}
