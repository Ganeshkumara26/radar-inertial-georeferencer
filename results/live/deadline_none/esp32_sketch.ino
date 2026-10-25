// ESP32 radar-sensor node: sends radar detections as uav-msglayer TELEMETRY frames over UDP (Wi-Fi) to the Pi gateway.
// Frame bytes come from msglayer_core.h, cross-checked against the Python reference.
// Arduino IDE: board "ESP32 Dev Module", keep the 3 files in one folder.
#include <WiFi.h>
#include <WiFiUdp.h>
#include "msglayer_core.h"
#include "stimulus.h"

const char *WIFI_SSID = "uavpipe_lab";
const char *WIFI_PASS = "radar_pipe_2026";
IPAddress GATEWAY_IP(192, 168, 1, 20);
const uint16_t GATEWAY_PORT = 5005;
const uint32_t HZ = 50;
const uint32_t MAX_AGE_US = 30000;
const int PIN_TX_MARK = 4;

WiFiUDP udp; uint16_t seq = 0; uint32_t k = 0, next_us = 0;

void setup() {
  pinMode(PIN_TX_MARK, OUTPUT); Serial.begin(115200);
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(200);
  Serial.println(WiFi.localIP()); next_us = micros() + 200000;
}
void loop() {
  uint32_t now = micros();
  if ((int32_t)(now - next_us) >= 0) {
    uint8_t pl[20], fr[ML_MAX_FRAME]; uint32_t idx = k % STIM_N;
    ml_put32(pl, idx); for (int i = 0; i < 4; i++) ml_put32(pl + 4 + 4 * i, STIM[idx][i]);
    size_t n = ml_pack(fr, ML_TELEM, seq++, now, MAX_AGE_US, 1, pl, sizeof pl);
    digitalWrite(PIN_TX_MARK, HIGH);
    udp.beginPacket(GATEWAY_IP, GATEWAY_PORT); udp.write(fr, n); udp.endPacket();
    digitalWrite(PIN_TX_MARK, LOW);
    k++; next_us += 1000000UL / HZ;
  }
}
