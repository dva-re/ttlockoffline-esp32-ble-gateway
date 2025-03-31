#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

#include "gw_settings.h"
#include "web.h"
#include "noble_api.h"
#include "util.h"

#define WIFI_CONFIGURE_DNS_PORT 53

DNSServer *dnsServer = nullptr;
bool connected = false;
int failed_wifi_connects = 0;

void setupWifi()
{
  // wifi logic:
  // - are credentials set then try to connect
  // - credentials not set, enter config mode

  // we have stored wifi credentials, use them
  if (GwSettings::isConfigured()) {
    Serial.printf("Connecting to configured WiFi [%s][%s]\n", GwSettings::getSsid(), GwSettings::getPass());
    WiFi.mode(WIFI_STA);
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
      failed_wifi_connects ++;
      Serial.print("WiFi disconnected, reconnecting... ");
      Serial.println(failed_wifi_connects);
      connected = false;
      if (failed_wifi_connects > 30) {
        Serial.println("Too many failed attempts, rebooting");
        ESP.restart();
      } else {
        WiFi.disconnect(); //just in case
        vTaskDelay(500 / portTICK_PERIOD_MS);
        WiFi.begin(GwSettings::getSsid(), GwSettings::getPass());
        // WiFi.reconnect();
      }
    }, WiFiEvent_t::SYSTEM_EVENT_STA_DISCONNECTED);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
      Serial.println("WiFi connected, Huuuuraaah");
      connected = true;
      failed_wifi_connects = 0;
      Serial.print("IP Address: ");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      Serial.println(WiFi.localIP());
    }, WiFiEvent_t::SYSTEM_EVENT_STA_CONNECTED);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
      Serial.println("WiFi lost IP, so we are go to force disconnect it for new connection attempts");
      WiFi.disconnect();
    }, WiFiEvent_t::SYSTEM_EVENT_STA_LOST_IP);

    uint8_t wifiResult;
    WiFi.begin(GwSettings::getSsid(), GwSettings::getPass());
    wifiResult = WiFi.waitForConnectResult();

    if (wifiResult == WL_CONNECTED) {
      // Serial.print("IP Address: ");
      // Serial.println(WiFi.localIP());
      connected = true;
      return;
    }

    Serial.printf("Failed to connect to configured WiFi [%s]\n", GwSettings::getSsid());
  }

  if (!GwSettings::isConfigured()) { // no credentials
    Serial.println("Starting AP for configuration");
    WiFi.softAP("ESP32GW", "87654321");
    std::string dnsName = "";
    dnsName += GwSettings::getName();
    dnsName += ".local";
    dnsServer = new DNSServer();
    dnsServer->start(WIFI_CONFIGURE_DNS_PORT, dnsName.c_str(), WiFi.softAPIP());
  }
}

bool setupWeb() {
  return WebManager::init();
}

void setup()
{
  Serial.begin(460800);
  delay(200);
  Serial.println();
  // esp_log_level_set("*", ESP_LOG_VERBOSE);
  GwSettings::init();

  setupWifi();

  setupWeb();

  bool mdnsSuccess = false;
  do {
    mdnsSuccess = MDNS.begin(GwSettings::getName());
  } while (mdnsSuccess == false);

  MDNS.addService("http", "tcp", ESP_GW_WEBSERVER_PORT);

  NobleApi::init();

  MDNS.addService("ws", "tcp", ESP_GW_WEBSOCKET_PORT);

  Serial.println("Setup complete");
  meminfo();
}

void loop()
{
  if (dnsServer != nullptr) {
    // when in configuration mode handle DNS requests
    dnsServer->processNextRequest();
  }
  NobleApi::loop();
  WebManager::loop();
}