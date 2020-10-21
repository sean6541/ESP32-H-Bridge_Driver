#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <driver/mcpwm.h>

#define PIN_OUT_A 27
#define PIN_OUT_B 26

Preferences prefs;

int single_ended;
int freq;
int duty;

String wifi_ssid;
String wifi_pass;

AsyncWebServer server(80);

static void update() {
  float f_duty = 0.0;
  if(single_ended) {
    mcpwm_set_frequency(MCPWM_UNIT_0, MCPWM_TIMER_0, freq);
    f_duty = duty * 100.0 / 1000.0;
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, f_duty);
  } else {
    mcpwm_set_frequency(MCPWM_UNIT_0, MCPWM_TIMER_0, freq * 2);
    f_duty = duty * 50.0 / 1000.0;
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 100 - f_duty);
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, f_duty);
  }
}

void setup() {
  disableCore0WDT();
  disableCore1WDT();

  prefs.begin("a", false);
  single_ended = prefs.getInt("single_ended", 0);
  freq = prefs.getInt("freq", 1000);
  duty = prefs.getInt("duty", 500);
  wifi_ssid = prefs.getString("wifi_ssid", "ESP32-H-Bridge_Driver");
  wifi_pass = prefs.getString("wifi_pass", "password");

  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, PIN_OUT_A);
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, PIN_OUT_B);
  mcpwm_config_t mcpwm_config;
  if(single_ended) {
    mcpwm_config = {
      .frequency = freq,
      .cmpr_a = 0.0,
      .cmpr_b = 0.0,
      .duty_mode = MCPWM_DUTY_MODE_0,
      .counter_mode = MCPWM_UP_COUNTER
    };
  } else {
    mcpwm_config = {
      .frequency = freq * 2,
      .cmpr_a = 100.0,
      .cmpr_b = 0.0,
      .duty_mode = MCPWM_DUTY_MODE_0,
      .counter_mode = MCPWM_UP_DOWN_COUNTER
    };
  }
  mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &mcpwm_config);
  if(!single_ended) {
    mcpwm_set_duty_type(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, MCPWM_DUTY_MODE_1);
  }
  update();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(wifi_ssid.c_str(), wifi_pass.c_str());

  server.on("/", HTTP_GET, [] (AsyncWebServerRequest *request) {
    int err_code = 200;
    String msg = "Configuration set.";
    if(request->hasParam("freq")) {
      int _freq = request->getParam("freq")->value().toInt();
      if(_freq > 0 && _freq <= 200000) {
        freq = _freq;
        prefs.putInt("freq", freq);
      } else {
        err_code = 400;
        msg = "Error: Freq " + String(_freq) + " is out of bounds (1-200000 Hertz)";
      }
    }
    if(request->hasParam("duty")) {
      int _duty = request->getParam("duty")->value().toInt();
      if(_duty >= 0 && _duty <= 1000) {
        duty = _duty;
        prefs.putInt("duty", duty);
      } else {
        err_code = 400;
        msg = "Error: Duty " + String(_duty) + " is out of bounds (0-1000)";
      }
    }
    update();
    request->send(err_code, "text/plain", msg);
  });

  server.on("/config", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "text/plain",
      + "single_ended:" + String(single_ended)
      + "\nfreq:" + String(freq)
      + "\nduty:" + String(duty)
      + "\nwifi_ssid: " + wifi_ssid
      + "\nwifi_pass: " + wifi_pass
  );
  });

  server.on("/config", HTTP_POST, [] (AsyncWebServerRequest *request) {
    bool reboot = false;
    if(request->hasParam("single_ended", true)) {
      single_ended = request->getParam("single_ended", true)->value().toInt();
      prefs.putInt("single_ended", single_ended);
    }
    if(request->hasParam("freq", true)) {
      freq = request->getParam("freq", true)->value().toInt();
      prefs.putInt("freq", freq);
    }
    if(request->hasParam("duty", true)) {
      duty = request->getParam("duty", true)->value().toInt();
      prefs.putInt("duty", duty);
    }
    if(request->hasParam("wifi_ssid", true)) {
      wifi_ssid = request->getParam("wifi_ssid", true)->value();
      prefs.putString("wifi_ssid", wifi_ssid);
      reboot = true;
    }
    if(request->hasParam("wifi_pass", true)) {
      wifi_pass = request->getParam("wifi_pass", true)->value();
      prefs.putString("wifi_pass", wifi_pass);
      reboot = true;
    }
    if(reboot) {
      request->send(200, "text/plain", "Configuration set. Rebooting...");
      ESP.restart();
    } else {
      request->send(200, "text/plain", "Configuration set.");
    }
  });

  server.begin();

  vTaskDelete(NULL);
}

void loop() {}
