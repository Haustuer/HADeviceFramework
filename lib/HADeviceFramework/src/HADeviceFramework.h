#pragma once

#include <Arduino.h>
#include <ArduinoHA.h>
#include <WiFiClient.h>
#include <WiFiManager.h>

#if defined(ESP8266)
#include <ESP8266WebServer.h>
using HADeviceWebServer = ESP8266WebServer;
#elif defined(ESP32)
#include <WebServer.h>
using HADeviceWebServer = WebServer;
#else
#error "HADeviceFramework supports ESP8266 and ESP32 targets only."
#endif

namespace ha_device {

struct DeviceInfo {
  const char* defaultDeviceNamePrefix;
  const char* manufacturer;
  const char* model;
  const char* softwareVersion;
  const char* configurationAccessPoint;
  const char* defaultBroker;
  uint16_t mqttPort;

  DeviceInfo(
      const char* deviceNamePrefix,
      const char* deviceManufacturer,
      const char* deviceModel,
      const char* deviceSoftwareVersion,
      const char* accessPoint,
      const char* broker,
      uint16_t port = 1883
  )
      : defaultDeviceNamePrefix(deviceNamePrefix),
        manufacturer(deviceManufacturer),
        model(deviceModel),
        softwareVersion(deviceSoftwareVersion),
        configurationAccessPoint(accessPoint),
        defaultBroker(broker),
        mqttPort(port) {}
};

class PortalWiFiManager : public WiFiManager {
 public:
  HADeviceWebServer* webServer() {
    return server.get();
  }

  void showManagerMenu() {
    handleRoot();
  }
};

class Framework {
 public:
  explicit Framework(const DeviceInfo& deviceInfo);

  bool begin();
  void loop();

  HADevice& device();
  HAMqtt& mqtt();
  PortalWiFiManager& wifiManager();
  HADeviceWebServer* webServer();

  const char* deviceName() const;
  const char* brokerAddress() const;
  uint16_t brokerPort() const;
  bool mqttStarted() const;

  void setWebServerCallback(std::function<void()> callback);
  void addMdnsService(const char* service, const char* protocol, uint16_t port);
  void setDebugStream(Print* stream);
  void resetConfiguration();

 private:
  static constexpr uint32_t kMqttCredentialsMagic = 0x43525957;
  static constexpr uint32_t kDeviceNameMagic = 0x4352594E;
  static constexpr size_t kBrokerLength = 32;
  static constexpr size_t kUsernameLength = 32;
  static constexpr size_t kPasswordLength = 64;
  static constexpr size_t kDeviceNameLength = 32;
  static constexpr size_t kMaxMdnsServices = 4;
  static constexpr unsigned long kMqttRetryIntervalMs = 10000;

  // This layout preserves Crystalised's persisted MQTT record. Reserved fields
  // are intentionally ignored and cleared on future configuration saves.
  struct MqttCredentials {
    uint32_t magic;
    char brokerAddress[kBrokerLength];
    char username[kUsernameLength];
    char password[kPasswordLength];
    uint8_t reserved[5];
    uint32_t checksum;
  };

  struct DeviceNameSettings {
    uint32_t magic;
    char name[kDeviceNameLength];
    uint32_t checksum;
  };

  struct MdnsService {
    const char* service;
    const char* protocol;
    uint16_t port;
  };

  static Framework* activeInstance_;

  static void saveParametersCallback();
  static uint32_t checksum(const void* data, size_t length);
  static bool containsTerminator(const char* value, size_t length);
  static bool brokerAddressIsValid(const char* address);
  static bool deviceNameIsValid(const char* name);

  bool credentialsAreValid(const MqttCredentials& credentials) const;
  bool deviceNameSettingsAreValid(const DeviceNameSettings& settings) const;
  bool loadSettings();
  bool saveSettings();
  void createDefaultDeviceName();
  void copyPortalValues();
  void configureDevice();
  void startMdns();
  void connectMqtt();
  void log_(const char* format, ...) const;

  DeviceInfo deviceInfo_;
  MqttCredentials credentials_ = {};
  DeviceNameSettings deviceNameSettings_ = {};
  PortalWiFiManager wifiManager_;
  WiFiManagerParameter deviceNameParameter_;
  WiFiManagerParameter brokerParameter_;
  WiFiManagerParameter usernameParameter_;
  WiFiManagerParameter passwordParameter_;
  WiFiClient wifiClient_;
  HADevice device_;
  HAMqtt mqtt_;
  MdnsService mdnsServices_[kMaxMdnsServices] = {};
  size_t mdnsServiceCount_ = 0;
  char mdnsHostname_[kDeviceNameLength] = {};
  char configurationUrl_[64] = {};
  Print* debugStream_ = &Serial;
  bool started_ = false;
  bool mqttStarted_ = false;
  bool mdnsStarted_ = false;
  wl_status_t previousWifiStatus_ = WL_IDLE_STATUS;
  unsigned long lastMqttAttemptMs_ = 0;
};

}  // namespace ha_device
