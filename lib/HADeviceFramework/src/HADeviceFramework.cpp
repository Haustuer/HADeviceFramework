#include "HADeviceFramework.h"

#include <EEPROM.h>

#if defined(ESP8266)
#include <ESP8266mDNS.h>
#elif defined(ESP32)
#include <ESPmDNS.h>
#endif

namespace ha_device {

Framework* Framework::activeInstance_ = nullptr;

Framework::Framework(const DeviceInfo& deviceInfo)
    : deviceInfo_(deviceInfo),
      deviceNameParameter_("device_name", "Device name", "", kDeviceNameLength - 1),
      brokerParameter_("mqtt_broker", "MQTT broker host or IP", "", kBrokerLength - 1),
      usernameParameter_("mqtt_username", "MQTT username", "", kUsernameLength - 1),
      passwordParameter_(
          "mqtt_password",
          "MQTT password",
          "",
          kPasswordLength - 1,
          "type=\"password\""
      ),
      mqtt_(wifiClient_, device_) {}

bool Framework::begin() {
  if (started_) {
    return false;
  }

  Serial.begin(115200);
  EEPROM.begin(sizeof(MqttCredentials) + sizeof(DeviceNameSettings));
  createDefaultDeviceName();
  loadSettings();

  deviceNameParameter_.setValue(deviceNameSettings_.name, sizeof(deviceNameSettings_.name));
  brokerParameter_.setValue(credentials_.brokerAddress, sizeof(credentials_.brokerAddress));
  usernameParameter_.setValue(credentials_.username, sizeof(credentials_.username));
  passwordParameter_.setValue(credentials_.password, sizeof(credentials_.password));

  activeInstance_ = this;
  wifiManager_.setConfigPortalBlocking(false);
  wifiManager_.setSaveParamsCallback(saveParametersCallback);
  wifiManager_.addParameter(&deviceNameParameter_);
  wifiManager_.addParameter(&brokerParameter_);
  wifiManager_.addParameter(&usernameParameter_);
  wifiManager_.addParameter(&passwordParameter_);
  configureDevice();
  started_ = true;
  previousWifiStatus_ = WiFi.status();
  wifiManager_.autoConnect(deviceInfo_.configurationAccessPoint);
  wifiManager_.startWebPortal();
  return WiFi.status() == WL_CONNECTED;
}

void Framework::loop() {
  if (!started_) {
    return;
  }

  wifiManager_.process();
  const wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus != previousWifiStatus_) {
    previousWifiStatus_ = wifiStatus;
    if (wifiStatus != WL_CONNECTED) {
      mqtt_.disconnect();
      mqttStarted_ = false;
      mdnsStarted_ = false;
    }
  }

  if (wifiStatus != WL_CONNECTED) {
    return;
  }

  startMdns();
  if (mdnsStarted_) {
#if defined(ESP8266)
    MDNS.update();
#endif
  }

  if (!mqttStarted_) {
    connectMqtt();
  }
  if (mqttStarted_) {
    mqtt_.loop();
  }
}

HADevice& Framework::device() {
  return device_;
}

HAMqtt& Framework::mqtt() {
  return mqtt_;
}

PortalWiFiManager& Framework::wifiManager() {
  return wifiManager_;
}

HADeviceWebServer* Framework::webServer() {
  return wifiManager_.webServer();
}

const char* Framework::deviceName() const {
  return deviceNameSettings_.name;
}

const char* Framework::brokerAddress() const {
  return credentials_.brokerAddress;
}

uint16_t Framework::brokerPort() const {
  return deviceInfo_.mqttPort;
}

bool Framework::mqttStarted() const {
  return mqttStarted_;
}

void Framework::setWebServerCallback(std::function<void()> callback) {
  wifiManager_.setWebServerCallback(callback);
}

void Framework::addMdnsService(
    const char* service,
    const char* protocol,
    uint16_t port
) {
  if (started_ || mdnsServiceCount_ == kMaxMdnsServices) {
    return;
  }

  mdnsServices_[mdnsServiceCount_++] = {service, protocol, port};
}

void Framework::resetConfiguration() {
  MqttCredentials emptyCredentials = {};
  DeviceNameSettings emptyDeviceName = {};
  EEPROM.put(0, emptyCredentials);
  EEPROM.put(sizeof(MqttCredentials), emptyDeviceName);
  EEPROM.commit();
  wifiManager_.resetSettings();
  mqtt_.disconnect();
  mqttStarted_ = false;
}

void Framework::saveParametersCallback() {
  if (activeInstance_ == nullptr || !activeInstance_->saveSettings()) {
    Serial.println("Failed to save device configuration.");
    return;
  }

  activeInstance_->configureDevice();
  activeInstance_->mqtt_.disconnect();
  activeInstance_->mqttStarted_ = false;
  Serial.println("Saved device configuration.");
}

uint32_t Framework::checksum(const void* data, size_t length) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  uint32_t value = 2166136261UL;
  for (size_t index = 0; index < length; ++index) {
    value ^= bytes[index];
    value *= 16777619UL;
  }
  return value;
}

bool Framework::containsTerminator(const char* value, size_t length) {
  return memchr(value, '\0', length) != nullptr;
}

bool Framework::brokerAddressIsValid(const char* address) {
  IPAddress ipAddress;
  if (ipAddress.fromString(address)) {
    return true;
  }

  size_t labelLength = 0;
  for (const char* character = address; *character != '\0'; ++character) {
    if (*character == '.') {
      if (labelLength == 0 || *(character - 1) == '-') {
        return false;
      }
      labelLength = 0;
      continue;
    }

    const bool isAlphaNumeric =
        (*character >= 'a' && *character <= 'z') ||
        (*character >= 'A' && *character <= 'Z') ||
        (*character >= '0' && *character <= '9');
    if (!isAlphaNumeric && *character != '-') {
      return false;
    }
    if (*character == '-' && labelLength == 0) {
      return false;
    }
    ++labelLength;
  }
  return labelLength > 0 && address[strlen(address) - 1] != '-';
}

bool Framework::deviceNameIsValid(const char* name) {
  const size_t length = strlen(name);
  if (
      length == 0 ||
      length >= kDeviceNameLength ||
      !isalnum(name[0]) ||
      !isalnum(name[length - 1])
  ) {
    return false;
  }

  for (size_t index = 0; index < length; ++index) {
    if (!isalnum(name[index]) && name[index] != '_' && name[index] != '-') {
      return false;
    }
  }
  return true;
}

bool Framework::credentialsAreValid(const MqttCredentials& credentials) const {
  return credentials.magic == kMqttCredentialsMagic &&
      containsTerminator(credentials.brokerAddress, sizeof(credentials.brokerAddress)) &&
      containsTerminator(credentials.username, sizeof(credentials.username)) &&
      containsTerminator(credentials.password, sizeof(credentials.password)) &&
      brokerAddressIsValid(credentials.brokerAddress) &&
      credentials.checksum == checksum(
          &credentials,
          sizeof(credentials) - sizeof(credentials.checksum)
      );
}

bool Framework::deviceNameSettingsAreValid(const DeviceNameSettings& settings) const {
  return settings.magic == kDeviceNameMagic &&
      containsTerminator(settings.name, sizeof(settings.name)) &&
      deviceNameIsValid(settings.name) &&
      settings.checksum == checksum(&settings, sizeof(settings) - sizeof(settings.checksum));
}

bool Framework::loadSettings() {
  MqttCredentials storedCredentials = {};
  DeviceNameSettings storedDeviceName = {};
  EEPROM.get(0, storedCredentials);
  EEPROM.get(sizeof(storedCredentials), storedDeviceName);

  if (credentialsAreValid(storedCredentials)) {
    credentials_ = storedCredentials;
  } else {
    memset(&credentials_, 0, sizeof(credentials_));
    snprintf(
        credentials_.brokerAddress,
        sizeof(credentials_.brokerAddress),
        "%s",
        deviceInfo_.defaultBroker
    );
  }

  if (deviceNameSettingsAreValid(storedDeviceName)) {
    deviceNameSettings_ = storedDeviceName;
  } else {
    memset(&deviceNameSettings_, 0, sizeof(deviceNameSettings_));
    createDefaultDeviceName();
  }

  for (size_t index = 0; index < sizeof(mdnsHostname_) - 1; ++index) {
    const char character = deviceNameSettings_.name[index];
    if (character == '\0') {
      mdnsHostname_[index] = '\0';
      break;
    }
    mdnsHostname_[index] = character >= 'A' && character <= 'Z'
        ? character - 'A' + 'a'
        : (character == '_' ? '-' : character);
  }
  mdnsHostname_[sizeof(mdnsHostname_) - 1] = '\0';
  return true;
}

bool Framework::saveSettings() {
  if (
      !brokerAddressIsValid(brokerParameter_.getValue()) ||
      !deviceNameIsValid(deviceNameParameter_.getValue())
  ) {
    Serial.println("Device name or MQTT broker host is invalid.");
    return false;
  }

  MqttCredentials credentials = {};
  credentials.magic = kMqttCredentialsMagic;
  snprintf(
      credentials.brokerAddress,
      sizeof(credentials.brokerAddress),
      "%s",
      brokerParameter_.getValue()
  );
  snprintf(credentials.username, sizeof(credentials.username), "%s", usernameParameter_.getValue());
  snprintf(credentials.password, sizeof(credentials.password), "%s", passwordParameter_.getValue());
  credentials.checksum = checksum(
      &credentials,
      sizeof(credentials) - sizeof(credentials.checksum)
  );

  DeviceNameSettings deviceName = {};
  deviceName.magic = kDeviceNameMagic;
  snprintf(deviceName.name, sizeof(deviceName.name), "%s", deviceNameParameter_.getValue());
  deviceName.checksum = checksum(&deviceName, sizeof(deviceName) - sizeof(deviceName.checksum));

  EEPROM.put(0, credentials);
  EEPROM.put(sizeof(credentials), deviceName);
  if (!EEPROM.commit()) {
    return false;
  }

  credentials_ = credentials;
  deviceNameSettings_ = deviceName;
  for (size_t index = 0; index < sizeof(mdnsHostname_); ++index) {
    const char character = deviceName.name[index];
    if (character == '\0') {
      mdnsHostname_[index] = '\0';
      break;
    }
    mdnsHostname_[index] = character >= 'A' && character <= 'Z'
        ? character - 'A' + 'a'
        : (character == '_' ? '-' : character);
  }
  if (mdnsStarted_) {
    MDNS.end();
    mdnsStarted_ = false;
  }
  return true;
}

void Framework::createDefaultDeviceName() {
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  snprintf(
      deviceNameSettings_.name,
      sizeof(deviceNameSettings_.name),
      "%s_%02x%02x%02x",
      deviceInfo_.defaultDeviceNamePrefix,
      mac[3],
      mac[4],
      mac[5]
  );
}

void Framework::configureDevice() {
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  device_.setUniqueId(mac, sizeof(mac));
  device_.enableExtendedUniqueIds();
  device_.setName(deviceNameSettings_.name);
  device_.setManufacturer(deviceInfo_.manufacturer);
  device_.setModel(deviceInfo_.model);
  device_.setSoftwareVersion(deviceInfo_.softwareVersion);
  device_.enableSharedAvailability();
  device_.enableLastWill();
}

void Framework::startMdns() {
  if (mdnsStarted_ || !MDNS.begin(mdnsHostname_)) {
    return;
  }

  MDNS.addService("http", "tcp", 80);
  for (size_t index = 0; index < mdnsServiceCount_; ++index) {
    MDNS.addService(
        mdnsServices_[index].service,
        mdnsServices_[index].protocol,
        mdnsServices_[index].port
    );
  }

  const int length = snprintf(
      configurationUrl_,
      sizeof(configurationUrl_),
      "http://%s.local/",
      mdnsHostname_
  );
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(configurationUrl_)) {
    return;
  }
  device_.setConfigurationUrl(configurationUrl_);
  mdnsStarted_ = true;
  Serial.printf("mDNS started: %s\n", configurationUrl_);
}

void Framework::connectMqtt() {
  if (credentials_.username[0] == '\0' || credentials_.password[0] == '\0') {
    return;
  }

  mqttStarted_ = mqtt_.begin(
      credentials_.brokerAddress,
      deviceInfo_.mqttPort,
      credentials_.username,
      credentials_.password
  );
  Serial.printf(
      "MQTT initialization for %s:%u: %s\n",
      credentials_.brokerAddress,
      deviceInfo_.mqttPort,
      mqttStarted_ ? "started" : "failed"
  );
}

}  // namespace ha_device
