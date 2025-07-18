	/*  Firmware CI
 - resolvendo problema com retorno da conexão wifi
  Data: 24/03/2025
 Guilherme Mendina


*/
#include <EEPROM.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Tone32.h>
#include <pitches.h>
#include <Update.h>


#define FIRMWARE_RELEASE "310"
#define EI_W
#define DEBUG
#define P1_LED            22
#define P2_LED            21
#define P3_LED            12
#define pinToBuzzer       17
#define pinToRelay        14
#define pinToFactoryReset 16
#define LEDC_CHANNEL  1
#define LEDC_RES      16
#define LEDC_FREQ     1000
#define STANDARD_BUZZER_DURATION  500 // ms
#define STANDARD_BUZZER_FREQUENCY 1000 // Hz
#define EEPROM_SIZE             1024
#define MQTT_MAX_BUFFER_SIZE    2048
#define MQTT_CONNECTION_TIMEOUT 5000
#define MQTT_ARGUMENTS_LEN      7
#define timeToWait                1
#define timeToPublish             3000
#define timeRelay                 3000
#define COUNTDOWN_FACTORY_RESET   3
#define SEND_FI_INTERVAL          20000
#define SCAN_FOR_FI_TIME          5
#define BLE_DEVICE_MAX_SIZE 64 // Tamanho máximo para nome + MAC
#define DEBOUNCE                  50
#define WIFI_SERVICE_UUID         "FFE0"
#define WIFI_CHARACTERISTIC_UUID  "FFE1"
#define TOKEN_SERVICE_UUID        "FFE0"
#define TOKEN_CHARACTERISTIC_UUID "FFE1"


/*----------------------------------------------------------
** Biblioteca  para adquiir IP WAN (SAIDA/NAT)            **
-----------------------------------------------------------*/          


#ifdef ESP8266
  #include <ESP8266HTTPClient.h>  // Biblioteca para ESP8266
  #include <ESP8266WiFi.h>
#else
  #include <HTTPClient.h>         // Biblioteca para ESP32
#endif

/*----------------------------------------------------------
**                                                         **
-----------------------------------------------------------*/   


typedef enum {
  WIFI = 1,
  BLE = 2
} BLINK_MODE;

typedef enum  {
  _STATUS = 1,
  _ERROR = 2
} MESSAGE_TYPE;

typedef struct {
  bool led;
  bool buzzer;
  long connect_timeout;
  long reconnect_timeout;
  long scan_timeout;
} cv_settings_t;
/** ---------------------------------------------------------------------------------------------------- **
 **                                           GLOBAL VARIABLES                                           **
 ** ---------------------------------------------------------------------------------------------------- */

const char* mqttServer        = "mqtt.chavi.com.br";
const char* getMacWithPrefix() {
  // Inicializa o NimBLE
  NimBLEDevice::init("");

  // Pega o endereço MAC do dispositivo BLE
  NimBLEAddress mac = NimBLEDevice::getAddress();

  // Converte o endereço MAC para string e adiciona o prefixo "CH"
  String macStr = "CH" + String(mac.toString().c_str());

  // Remove os ":" da string
  macStr.replace(":", "");

  // Converte as letras para maiúsculas
  macStr.toUpperCase();

  // Pega somente os 6 últimos caracteres
  String lastSixChars = macStr.substring(macStr.length() - 6);

  // Adiciona o prefixo "CH001C" na frente dos 6 últimos caracteres
  String finalStr = "CH001C" + lastSixChars;

  // Retorna a string com o prefixo "CH001C" e os últimos 6 caracteres do MAC
  return strdup(finalStr.c_str());
}
const char* hostnameWiFi;

volatile bool isFactoryResetButton                = false;
volatile unsigned long isFactoryResetButtonSince  = 0;

static unsigned long TOKENUL    = 0;
static unsigned long TOKENUL_2  = 0;
static unsigned long SALTOS     = 0;
static unsigned long SALTOS_2   = 0;

static unsigned long starterPistol_Notification = 0;

static bool doConnect         = false;
static bool doFIScan          = false;
static bool doRefreshFI       = false;
static bool doConfigWifi      = false;
static bool doSendToken       = false;
static bool doFactoryReset    = false;
static bool doRelay           = false;
static bool doPreferences     = false;
static bool hasWifiSSID       = false;
static bool hasWifiPassword   = false;
static bool hasUserID         = false;
static bool hasNotifyResponse = false;
static bool isWifiConnected   = false;
static bool isFISent          = false;
static bool isTokenSent       = false;
static bool isFactoryReset    = false;
static bool MQTTReceived      = false;
static bool isEnabledLEDFade  = false;
static cv_settings_t settings;
static int addrOffset = 0;

// LEDFade()
static uint32_t dutyCycle         = 0;
static uint32_t fadeAmount        = -1;
static uint32_t fadeInDuration    = 1000;
static uint32_t fadeOutDuration   = 1000;
static uint32_t retentionDuration = fadeInDuration / 255;

static uint32_t scanTime      = 0;
static uint16_t ServerConnID  = 0;
static uint8_t writeCCounter  = 0;
const uint16_t  nmtu               = 512;
unsigned long lastCheckTime = 0; // Guarda o tempo da última verificação

String getPublicIP();

static std::string SSID;
static std::string Password;
static std::string USERID;
static std::string FI_Address;
static std::string WI_Address;
static std::string TOKEN;
static std::string TOKEN_2;
static std::string SEED;
static std::string SEED_2;
static std::string TIMESTAMP;
static std::string ACTION;
static std::string PREFERENCES;
static std::string RELEASE;
static std::string scannedFI;
static std::string scannedFIMessage;
static std::string errorMessage;
static std::string statusMessage;
static std::string notificationPayload;
static std::string _NimBLEDevice_Name;
static std::string arguments[MQTT_ARGUMENTS_LEN];

const char* ca_cert           = \
                                "-----BEGIN CERTIFICATE-----\n" \
                                "MIIEkjCCA3qgAwIBAgIQCgFBQgAAAVOFc2oLheynCDANBgkqhkiG9w0BAQsFADA/\n" \
                                "MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT\n" \
                                "DkRTVCBSb290IENBIFgzMB4XDTE2MDMxNzE2NDA0NloXDTIxMDMxNzE2NDA0Nlow\n" \
                                "SjELMAkGA1UEBhMCVVMxFjAUBgNVBAoTDUxldCdzIEVuY3J5cHQxIzAhBgNVBAMT\n" \
                                "GkxldCdzIEVuY3J5cHQgQXV0aG9yaXR5IFgzMIIBIjANBgkqhkiG9w0BAQEFAAOC\n" \
                                "AQ8AMIIBCgKCAQEAnNMM8FrlLke3cl03g7NoYzDq1zUmGSXhvb418XCSL7e4S0EF\n" \
                                "q6meNQhY7LEqxGiHC6PjdeTm86dicbp5gWAf15Gan/PQeGdxyGkOlZHP/uaZ6WA8\n" \
                                "SMx+yk13EiSdRxta67nsHjcAHJyse6cF6s5K671B5TaYucv9bTyWaN8jKkKQDIZ0\n" \
                                "Z8h/pZq4UmEUEz9l6YKHy9v6Dlb2honzhT+Xhq+w3Brvaw2VFn3EK6BlspkENnWA\n" \
                                "a6xK8xuQSXgvopZPKiAlKQTGdMDQMc2PMTiVFrqoM7hD8bEfwzB/onkxEz0tNvjj\n" \
                                "/PIzark5McWvxI0NHWQWM6r6hCm21AvA2H3DkwIDAQABo4IBfTCCAXkwEgYDVR0T\n" \
                                "AQH/BAgwBgEB/wIBADAOBgNVHQ8BAf8EBAMCAYYwfwYIKwYBBQUHAQEEczBxMDIG\n" \
                                "CCsGAQUFBzABhiZodHRwOi8vaXNyZy50cnVzdGlkLm9jc3AuaWRlbnRydXN0LmNv\n" \
                                "bTA7BggrBgEFBQcwAoYvaHR0cDovL2FwcHMuaWRlbnRydXN0LmNvbS9yb290cy9k\n" \
                                "c3Ryb290Y2F4My5wN2MwHwYDVR0jBBgwFoAUxKexpHsscfrb4UuQdf/EFWCFiRAw\n" \
                                "VAYDVR0gBE0wSzAIBgZngQwBAgEwPwYLKwYBBAGC3xMBAQEwMDAuBggrBgEFBQcC\n" \
                                "ARYiaHR0cDovL2Nwcy5yb290LXgxLmxldHNlbmNyeXB0Lm9yZzA8BgNVHR8ENTAz\n" \
                                "MDGgL6AthitodHRwOi8vY3JsLmlkZW50cnVzdC5jb20vRFNUUk9PVENBWDNDUkwu\n" \
                                "Y3JsMB0GA1UdDgQWBBSoSmpjBH3duubRObemRWXv86jsoTANBgkqhkiG9w0BAQsF\n" \
                                "AAOCAQEA3TPXEfNjWDjdGBX7CVW+dla5cEilaUcne8IkCJLxWh9KEik3JHRRHGJo\n" \
                                "uM2VcGfl96S8TihRzZvoroed6ti6WqEBmtzw3Wodatg+VyOeph4EYpr/1wXKtx8/\n" \
                                "wApIvJSwtmVi4MFU5aMqrSDE6ea73Mj2tcMyo5jMd6jmeWUHK8so/joWUoHOUgwu\n" \
                                "X4Po1QYz+3dszkDqMp4fklxBwXRsW10KXzPMTZ+sOPAveyxindmjkW8lGy+QsRlG\n" \
                                "PfZ+G6Z6h7mjem0Y+iWlkYcV4PIWL1iwBi8saCbGS5jN2p8M+X+Q7UNKEkROb3N6\n" \
                                "KOqkqm57TH2H3eDJAkSnh6/DNFu0Qg==\n" \
                                "-----END CERTIFICATE-----\n" ;
//-----BEGIN CERTIFICATE-----
//"MIIEkjCCA3qgAwIBAgIQCgFBQgAAAVOFc2oLheynCDANBgkqhkiG9w0BAQsFADA/\n" \
//"MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT\n" \
//"DkRTVCBSb290IENBIFgzMB4XDTE2MDMxNzE2NDA0NloXDTIxMDMxNzE2NDA0Nlow\n" \
//"SjELMAkGA1UEBhMCVVMxFjAUBgNVBAoTDUxldCdzIEVuY3J5cHQxIzAhBgNVBAMT\n" \
//"GkxldCdzIEVuY3J5cHQgQXV0aG9yaXR5IFgzMIIBIjANBgkqhkiG9w0BAQEFAAOC\n" \
//"AQ8AMIIBCgKCAQEAnNMM8FrlLke3cl03g7NoYzDq1zUmGSXhvb418XCSL7e4S0EF\n" \
//"q6meNQhY7LEqxGiHC6PjdeTm86dicbp5gWAf15Gan/PQeGdxyGkOlZHP/uaZ6WA8\n" \
//"SMx+yk13EiSdRxta67nsHjcAHJyse6cF6s5K671B5TaYucv9bTyWaN8jKkKQDIZ0\n" \
//"Z8h/pZq4UmEUEz9l6YKHy9v6Dlb2honzhT+Xhq+w3Brvaw2VFn3EK6BlspkENnWA\n" \
//"a6xK8xuQSXgvopZPKiAlKQTGdMDQMc2PMTiVFrqoM7hD8bEfwzB/onkxEz0tNvjj\n" \
//"/PIzark5McWvxI0NHWQWM6r6hCm21AvA2H3DkwIDAQABo4IBfTCCAXkwEgYDVR0T\n" \
//"AQH/BAgwBgEB/wIBADAOBgNVHQ8BAf8EBAMCAYYwfwYIKwYBBQUHAQEEczBxMDIG\n" \
//"CCsGAQUFBzABhiZodHRwOi8vaXNyZy50cnVzdGlkLm9jc3AuaWRlbnRydXN0LmNv\n" \
//"bTA7BggrBgEFBQcwAoYvaHR0cDovL2FwcHMuaWRlbnRydXN0LmNvbS9yb290cy9k\n" \
//"c3Ryb290Y2F4My5wN2MwHwYDVR0jBBgwFoAUxKexpHsscfrb4UuQdf/EFWCFiRAw\n" \
//"VAYDVR0gBE0wSzAIBgZngQwBAgEwPwYLKwYBBAGC3xMBAQEwMDAuBggrBgEFBQcC\n" \
//"ARYiaHR0cDovL2Nwcy5yb290LXgxLmxldHNlbmNyeXB0Lm9yZzA8BgNVHR8ENTAz\n" \
//"MDGgL6AthitodHRwOi8vY3JsLmlkZW50cnVzdC5jb20vRFNUUk9PVENBWDNDUkwu\n" \
//"Y3JsMB0GA1UdDgQWBBSoSmpjBH3duubRObemRWXv86jsoTANBgkqhkiG9w0BAQsF\n" \
//"AAOCAQEA3TPXEfNjWDjdGBX7CVW+dla5cEilaUcne8IkCJLxWh9KEik3JHRRHGJo\n" \
//"uM2VcGfl96S8TihRzZvoroed6ti6WqEBmtzw3Wodatg+VyOeph4EYpr/1wXKtx8/\n" \
//"wApIvJSwtmVi4MFU5aMqrSDE6ea73Mj2tcMyo5jMd6jmeWUHK8so/joWUoHOUgwu\n" \
//"X4Po1QYz+3dszkDqMp4fklxBwXRsW10KXzPMTZ+sOPAveyxindmjkW8lGy+QsRlG\n" \
//"PfZ+G6Z6h7mjem0Y+iWlkYcV4PIWL1iwBi8saCbGS5jN2p8M+X+Q7UNKEkROb3N6\n" \
//"KOqkqm57TH2H3eDJAkSnh6/DNFu0Qg==\n" \
//-----END CERTIFICATE-----
//"-----BEGIN PRIVATE KEY-----
//MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDVOHMHrsHPFY2z
//aERkmv8dC2hFqx62puuIdsfAFm/pj5nFUZN1WXRLqI5Ur9LqsW1oXN2WJqeVXx1B
//Ozni8Y5+sA/OKUE54pCMICMJQaTZ+OOtp8yie9uwgbypLtIr+PHfOnA7i6fIR8oB
//isUxywjxap52RrlWy5IgXWV73PILtMUaBmCaSqPpn3HcWAdtmX6WGx5mzrksRDoh
//r/BQAbbkc9vxRIv5csSOVzp5l6AcwjyegoIZ/fYcPVcNSPGiueP4WddyjAHNvIfl
//84Una6QhCRQWn9GPMYRBH9bDD6SmIjyTEF62RG10Ud9hLH4Hhbra93WgjoIafgM6
//vqIw/VfjAgMBAAECggEBALHXtqJW0hKvEJTux/T+DwGjzSIm+6Qvj88v+6YtUMUH
//4KNjSlIb/dNJUQYz7QGSYgYlRzGBJlCBl/4br0mqX0cuBNDZOi1U22n1OCP1W9Hv
//NcBXcGiqVHBhlgGrKuRAbUXbEVepQZXcwxIMNDQ2/EuYuXk/vkE15LkYDZiFjmHR
//rHlibfOljrxfa9Qe3VGpbQqO16bBzMCWXzk2fQUrZdGM71JVuH3LPu85T2nCroRK
//96gASNy6R3qbMBLegnIHi86FpoE9TFY37cGBnSY6JLEbLxfIueaaWjw9r7CSspxu
//JwHItjGKeIbWy7jvqAoydp2uF1Qohu1UYA8M/x2XyDkCgYEA8sS/Ps5FeWU1AUwW
//R5QFDsFwEobJBNkDNL6ZvzFQgAVENChA9PMN7gi+ZkErYZBD27jbN+VwHsZPH2+8
//ZXivYiH2Jau8AWDEldGvY7lyVGSzWZW7iCycIQUepyMB8q2/Zt5IzVJaHgOWWJgY
//2mcxH/e8AdK1j1VDa2cD1IUejb0CgYEA4NduJYKqCHmf9qkv6d/220YSyKwmCi+Z
//kpkb9eWwyJYfwtbKu0VWf8k5/YkeixsZy/mP7QI8lgFHIBnBVLK1GYSYMFKu39i7
//vO1RjeroRjMOPOiR91QXqEzQKrh78UhK6lmmUlt8rOI0uLItty8nJn6O15RRtvO6
//NHON7YTyxh8CgYAasXNtznRpIq2vVNRmTmo38yEFiHh15f6qQALbuOpnCS00pvBt
//foQbli6JQ8UnVxt+/1ZrUPkBCUEN7dVDOv/dxGoyPi3P4Pn8ly+3wV5G1UO7J/GU
//yYLpRozWR80hB+Wxw+MxYEq0XiSb3S1uZkZKg4zSjn6UIgXlu0/6gCCZ6QKBgE0y
//6sjKUmNSj0/7y0277mBfcPeh0/XPXiVtmKIXWVml4gXeBgHCzu5VQyoAOJJ+nZ7h
//cz9nczH2AlvNPAo+yduXIUIGQbDuE5fMzCG0NEhWI19aYzPOlcjdhuQEL9oqfj50
//xZyleOtLR9raosOw1vpqndT7QgtVJ+v2eRbCfTD1AoGAT3E2CUNyc7z/eEw1RpKc
//p+1lnKl7/wQjGBJ0sSVtX0dmyk2GfCMNiryPw88xXZrYptoO2fntaGKfHSXD47ds
//z80Dnobvpytbk1WfJS7rcXjp/tZzXjJzlpijT7lAU9aOeA6OoXcQUiAZLZcgkhDM
//bm+33vPUG/yWpldc4uRW0dc=
//-----END PRIVATE KEY-----"


/** ---------------------------------------------------------------------------------------------------- **
 **                                                OBJECTS                                               **
 ** ---------------------------------------------------------------------------------------------------- */


static NimBLEAdvertisedDevice* advDevice;
static NimBLEServer* pServer;

WiFiClientSecure espClient;
PubSubClient client(espClient);




/** ---------------------------------------------------------------------------------------------------- **
 **                                              PROTOTYPES                                              **
 ** ---------------------------------------------------------------------------------------------------- */


void SetupNimBLE();
void SetupNimBLE_Client();
void NotifyCallbackNimBLE_Client(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify);
void ScanCallbackNimBLE_Client(NimBLEScanResults results);
bool ConnectNimBLE();
bool DeinitNimBLE();
void SetupWifi(bool _Config);
bool ConnectWifi(bool FirstConnection);
void SetupMQTT();
void ConnectMQTT();
void PublishMQTT(std::string s, MESSAGE_TYPE mt);
void CallbackMQTT(char* topic, byte* payload, unsigned int length);
std::string macAddressNumber (std::string macAddress);
void Blink(BLINK_MODE bm, int Delay);
void LEDFade(uint32_t fadeInDuration, uint32_t fadeOutDuration);

int EepRead(int offsetAddress, std::string *str);
int EepWrite(int offsetAddress, const std::string &str);
void handleMQTTCommand(std::string sPayload);


/** ---------------------------------------------------------------------------------------------------- **
 **                                               CALLBACKS                                              **
 ** ---------------------------------------------------------------------------------------------------- */

class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
      NimBLEDevice::startAdvertising();
    };
};



class AdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
      void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
      std::string _Device = advertisedDevice->getName();
      std::string MAC_Device = advertisedDevice->getAddress().toString();  // Endereço MAC ;
      _Device.pop_back();



      #ifdef DEBUG
      Serial.println("");
      Serial.print("DEVICE ");
      Serial.print(_Device.c_str());
      Serial.print(" (");
      Serial.print(advertisedDevice->getAddress().toString().c_str());
      Serial.println(") FOUND");
      #endif

      MAC_Device = macAddressNumber(advertisedDevice->getAddress().toString());

      if (doFIScan) {
        if (scannedFI.find(MAC_Device) == std::string::npos) {
                  WiFi.mode(WIFI_OFF); // Desativa o Wi-Fi para melhorar a performance BLE

          scannedFI.append("\"");
          scannedFI.append(MAC_Device);
          scannedFI.append("\",");
          } else {
          #ifdef DEBUG
          Serial.println("");
          Serial.print("DEVICE ");
          Serial.print(_Device.c_str());
          Serial.print(" (");
          Serial.print(advertisedDevice->getAddress().toString().c_str());
          Serial.println(") ALREADY FOUND");
          #endif
        }
      }

      if (_Device.compare(FI_Address) == 0) {
        Serial.println("Dispositivo alvo encontrado! Parando scan...");
        NimBLEDevice::getScan()->stop();

        advDevice = advertisedDevice;

        doConnect = true;
      }
    };
};

class CharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
      std::string PACKET;

      switch (writeCCounter) {
        case 0:
          if (!hasUserID)  {

            PACKET = pCharacteristic->getValue();
            if (PACKET[PACKET.size() - 1] == '+') {
              USERID = PACKET;
            } else {
              if (USERID.size()) {
                if (USERID[USERID.size() - 1] == '+') {
                  USERID.pop_back();
                  USERID.append(PACKET);
                } else {
                  USERID = PACKET;
                }
              } else {
                USERID = PACKET;
              }

              hasUserID = true;
              writeCCounter++;

#ifdef DEBUG
              Serial.println("");
              Serial.print("User ID: ");
              Serial.println(USERID.c_str());
#endif
            }
          }
          return;
        case 1:
          if (!hasWifiSSID) {
            SSID = pCharacteristic->getValue();
            hasWifiSSID = true;
            writeCCounter++;

#ifdef DEBUG
            Serial.println("");
            Serial.print("SSID: ");
            Serial.println(pCharacteristic->getValue().c_str());
            Serial.println("");
            Serial.print(WI_Address.c_str());
#endif
          }
          return;
        case 2:
          if (!hasWifiPassword) {
            Password = pCharacteristic->getValue();
            hasWifiPassword = true;
            writeCCounter = 0;

#ifdef DEBUG
            Serial.print("Password: ");
            Serial.println(pCharacteristic->getValue().c_str());
#endif
          }
          return;
      }
    };
};


static CharacteristicCallbacks chrCallbacks;

/** ---------------------------------------------------------------------------------------------------- **
 **                                              IP PUBLIC                                               **
 ** Função para adquerir IP publico (WAN)                                                                **
 ** ---------------------------------------------------------------------------------------------------- */


String getPublicIP() {
    WiFiClient client;
    HTTPClient http;

    http.begin(client, "http://ipinfo.io/ip");  // Outra API de IP público
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String ip = http.getString();
        ip.trim();
        http.end();
        return ip;
    } else {
        Serial.print("Erro HTTP: ");
        Serial.println(httpCode);
        http.end();
        return "Erro ao obter IP";
    }
}






/** ---------------------------------------------------------------------------------------------------- **
 **                                               MAIN LOOP                                              **
 ** ---------------------------------------------------------------------------------------------------- */


void setup() {
    EEPROM.begin(EEPROM_SIZE);
  isFactoryReset = !EEPROM.readBool(0);
  EEPROM.end();

   hostnameWiFi      = getMacWithPrefix();

  Serial.begin(115200);
  WiFi.disconnect(true);   // Desconectar Wi-Fi para evitar problemas na reconexão
  WiFi.setHostname(hostnameWiFi); // Definir hostname antes da conexão


  pinMode(P1_LED, OUTPUT);
  pinMode(P2_LED, OUTPUT);
  pinMode(P3_LED, OUTPUT);
  pinMode(pinToBuzzer, OUTPUT);
  pinMode(pinToRelay, OUTPUT);
  WiFi.setTxPower(WIFI_POWER_11dBm);


  if (isFactoryReset) {
    doFIScan = true;
    isFISent = false;

    #ifdef EI_W
    doFIScan = false;
    #endif

    settings.led                = true;
    settings.buzzer             = true;
    settings.connect_timeout    = 30000;
    settings.reconnect_timeout  = -1;
    settings.scan_timeout       = 10000;

    digitalWrite(pinToRelay, LOW);

    digitalWrite(P1_LED, HIGH);
    digitalWrite(P2_LED, HIGH);
    digitalWrite(P3_LED, HIGH);

   // tone(pinToBuzzer, STANDARD_BUZZER_FREQUENCY, STANDARD_BUZZER_DURATION, 0);

    digitalWrite(P1_LED, LOW);
    digitalWrite(P2_LED, LOW);
    digitalWrite(P3_LED, LOW);

    SetupNimBLE_Client();
  



#ifdef DEBUG
    Serial.println("");
    Serial.println("BLE CLIENT: ENABLED");
    Serial.println();
#endif

    EnableLEDFade(1000, 1000);
    while (doFIScan) {
      if (settings.led)
        LEDFade();
      delay(1);
    }
    DisableLEDFade();
    delay(500);

    while (!DeinitNimBLE()) {
      delay(1);
    }

#ifdef DEBUG
    Serial.println("");
    Serial.println("BLE CLIENT: DISABLED");
#endif

    SetupWifi(true);
  
    isFactoryReset = false;

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.writeBool(0, 1);
    EEPROM.writeBytes(1, (const void*)&settings, sizeof(cv_settings_t));
    EEPROM.commit();
    EEPROM.end();
  } else {
    doFIScan = false;
    isFISent = true;

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.readBytes(1, (void*)&settings, sizeof(cv_settings_t));
    EEPROM.end();

    digitalWrite(pinToRelay, LOW);

    if (settings.led) {
      digitalWrite(P1_LED, HIGH);
      digitalWrite(P2_LED, HIGH);
      digitalWrite(P3_LED, HIGH);
    }

    if (settings.buzzer)
      //tone(pinToBuzzer, STANDARD_BUZZER_FREQUENCY, STANDARD_BUZZER_DURATION, 0);
    //else
      //delay(STANDARD_BUZZER_DURATION);

    digitalWrite(P1_LED, LOW);
    digitalWrite(P2_LED, LOW);
    digitalWrite(P3_LED, LOW);

    EepRead(sizeof(cv_settings_t) + 1, &WI_Address);

    SetupWifi(false);
   


  }

  SetupMQTT();
 
}



void loop() {


  Serial.print("Versão OTA: ");
  Serial.println(FIRMWARE_RELEASE);

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n'); // Lê até Enter
    command.trim(); // Remove espaços/brancos

    Serial.print("Comando recebido pela Serial: ");
    Serial.println(command);

    // Transforma em payload para simular MQTT
    std::string sPayload = command.c_str();

    // Chama função igual ao MQTT
    handleMQTTCommand(sPayload);
  }


  CheckFactoryReset();

      int max_tent = 3; // Número máximo de tentativas de reconexão
      int tent = 0;   // Contador de tentativas

          if (!client.connected()) {
            Serial.println("MQTT desconectado, tentando reconectar...");
              if (client.connect(WI_Address.c_str())) {
                Serial.println("Reconectado ao MQTT!");
                client.subscribe(WI_Address.c_str()); // <--- REFAZ O SUBSCRIBE AQUI
                } else {
            Serial.println("Falha ao reconectar MQTT.");
                }
          }

            // Se não conseguir conectar após 3 tentativas, reinicia a ESP

        if (WiFi.status() != WL_CONNECTED) {
             Serial.println("Falha na conexão Wi-Fi após 3 tentativas. Reiniciando...");
             CheckFactoryReset();
              ESP.restart();
          }

          // No loop(), substitua a verificação atual por:
        if (millis() - lastCheckTime > 15000) {  // Verifica a cada 15 segundos
          lastCheckTime = millis();
    
          if (!client.connected()) {
          Serial.println("MQTT desconectado! Tentando reconectar...");
          ConnectMQTT();
        
            if (!client.connected()) {
                Serial.println("Falha na reconexão MQTT. Reiniciando...");
                ESP.restart();
              }
          }
        }

    ConnectMQTT();  // Mantém a conexão MQTT ativa
    client.loop();  // Processa mensagens MQTT
   


  if (settings.led) {
    digitalWrite(P1_LED, HIGH);
    digitalWrite(P2_LED, HIGH);
    digitalWrite(P3_LED, HIGH);
  }
      

      if (MQTTReceived) {
        /** ---------------------------------------------------------------------------------------------------- **
        **                            INSTRUCTION(S) FOR MANUAL WI-FI CONFIGURATION                             **
        ** ---------------------------------------------------------------------------------------------------- */
        if (doConfigWifi) {
          isWifiConnected = !WiFi.mode(WIFI_OFF);
      
          digitalWrite(P1_LED, LOW);
          digitalWrite(P2_LED, LOW);
          digitalWrite(P3_LED, LOW);

          SetupWifi(true);

          if (settings.led) {
            digitalWrite(P1_LED, HIGH);
            digitalWrite(P2_LED, HIGH);
            digitalWrite(P3_LED, HIGH);
          }

          doConfigWifi  = false;
          MQTTReceived  = false;
          /** ---------------------------------------------------------------------------------------------------- **
          **                                 INSTRUCTION(S) FOR MANUAL FI REFRESH                                 **
          ** ---------------------------------------------------------------------------------------------------- */
        } else if (doRefreshFI) {
          isWifiConnected = !WiFi.mode(WIFI_OFF);

          #ifdef DEBUG
            Serial.println("");
            Serial.println("WIFI: DISABLED");
          #endif

          digitalWrite(P1_LED, LOW);
          digitalWrite(P2_LED, LOW);
          digitalWrite(P3_LED, LOW);

          doFIScan = true;
          isFISent = false;

          SetupNimBLE_Client();

          #ifdef DEBUG
            Serial.println("");
            Serial.println("BLE CLIENT: ENABLED");
          #endif

          EnableLEDFade(1000, 1000);

          while (doFIScan) {
            if (settings.led) {
              LEDFade();
            }
            delay(1);
          }

          DisableLEDFade();
          delay(500);

          while (!DeinitNimBLE()) {
            delay(1);
          }

          #ifdef DEBUG
            Serial.println("");
            Serial.println("BLE CLIENT: DISABLED");
          #endif

          #ifdef DEBUG
            Serial.println("");
            Serial.println("WIFI: ENABLED");
          #endif

          SetupWifi(false);

          if (settings.led) {
            digitalWrite(P1_LED, HIGH);
            digitalWrite(P2_LED, HIGH);
            digitalWrite(P3_LED, HIGH);
          }

          doRefreshFI = false;
          MQTTReceived = false;

          /** ---------------------------------------------------------------------------------------------------- **
          **                                INSTRUCTION(S) FOR THE OPENING OF A FI                                **
          ** ---------------------------------------------------------------------------------------------------- */

        } else if (doSendToken) {
          errorMessage.clear();

          isWifiConnected = !WiFi.mode(WIFI_OFF);

          #ifdef DEBUG
            Serial.println("");
            Serial.println("WIFI: DISABLED");
          #endif

          digitalWrite(P1_LED, LOW);
          digitalWrite(P2_LED, LOW);
          digitalWrite(P3_LED, LOW);

          SetupNimBLE_Client();

          #ifdef DEBUG
            Serial.println("");
            Serial.println("BLE CLIENT: ENABLED");
          #endif

          unsigned long starterPistol = millis();

          EnableLEDFade(1000, 1000);

          while ((!doConnect) && (millis() - starterPistol < 10000)) {
            if (settings.led) {
              LEDFade();
            }
            delay(1);
          }

          DisableLEDFade();

          if (doConnect) {
            doConnect = false;
            isTokenSent = false;

            starterPistol                       = millis();
            unsigned long lastAttemptToConnect  = millis();
            bool firstAttemptToConnect          = true;

            EnableLEDFade(1000, 1000);

            while ((!isTokenSent) && (millis() - starterPistol < 10000)) {
              if (settings.led) {
                LEDFade();
              }

              if ((millis() - lastAttemptToConnect > 1000) || firstAttemptToConnect) {
                if (ConnectNimBLE()) {
                  isTokenSent = true;
                } else {
                  if (firstAttemptToConnect) {
                    firstAttemptToConnect = false;
                  }
                  lastAttemptToConnect = millis();
                }
              }
            }

            DisableLEDFade();

            if (!isTokenSent) {

              #ifdef DEBUG
                Serial.println("");
                Serial.println("FAILED TO SEND TOKEN");
              #endif

              errorMessage = "FAILED TO SEND TOKEN TO DEVICE ";

              errorMessage.append(FI_Address.c_str());
            } else {
              #ifdef DEBUG
                Serial.println("");
                Serial.println("SEND TOKEN: SUCCEEDED");
              #endif

              for (int i = 0; i < 10; i++) {
                Blink(BLINK_MODE::BLE, 125);
                delay(125);
              }
            }

          } else {
            #ifdef DEBUG
              Serial.println("");
              Serial.print("DEVICE ");
              Serial.print(FI_Address.c_str());
              Serial.print(" NOT FOUND");
              Serial.println("");
            #endif

            errorMessage = "DEVICE ";
            errorMessage.append(FI_Address.c_str());
            errorMessage.append(" NOT FOUND");
          }

          while (!DeinitNimBLE()) {
            delay(1);
          }

          #ifdef DEBUG
            Serial.println("");
            Serial.println("BLE CLIENT: DISABLED");
          #endif

          #ifdef DEBUG
            Serial.println("");
            Serial.println("WIFI: ENABLED");
          #endif

          SetupWifi(false);

          doSendToken   = false;
          isTokenSent   = false;
          MQTTReceived  = false;

          if (!statusMessage.empty()) {
            PublishMQTT(statusMessage, MESSAGE_TYPE::_STATUS);
          } else {
            if (errorMessage.empty()) {
              errorMessage = "DEVICE ";
              errorMessage.append(FI_Address.c_str());
              errorMessage.append(" NOTIFICATION TIMEOUT EXCEEDED");
            }
          }

          if (!errorMessage.empty()) {
            PublishMQTT(errorMessage, MESSAGE_TYPE::_ERROR);
          }

          if (settings.led) {
            digitalWrite(P1_LED, HIGH);
            digitalWrite(P2_LED, HIGH);
            digitalWrite(P3_LED, HIGH);
          }

          /** ---------------------------------------------------------------------------------------------------- **
          **                                       INSTRUCTION(S) FOR RELAY                                       **
          ** ---------------------------------------------------------------------------------------------------- */

        } else if (doRelay) {
          digitalWrite(pinToRelay, HIGH);
          delay(timeRelay * timeToWait);
          digitalWrite(pinToRelay, LOW);

          doRelay       = false;
          MQTTReceived  = false;

          /** ---------------------------------------------------------------------------------------------------- **
          **                                INSTRUCTION(S) FOR MANUAL FACTORY RESET                               **
          ** ---------------------------------------------------------------------------------------------------- */

        } else if (doFactoryReset) {
          EEPROM.begin(EEPROM_SIZE);
          for (int i = 0; i < EEPROM_SIZE; i++) {
            EEPROM.write(i, 0);
            EEPROM.commit();
          }
          EEPROM.end();

          doFactoryReset  = false;
          MQTTReceived    = false;

          //tone(pinToBuzzer, STANDARD_BUZZER_FREQUENCY, 4 * STANDARD_BUZZER_DURATION, 0);

          ESP.restart();
        } else if (doPreferences) {
          doPreferences = false;
          MQTTReceived  = false;

          size_t fieldDelimiterPos = PREFERENCES.find_first_of('|');

          std::string key;
          std::string value;

          while (fieldDelimiterPos != std::string::npos) {
            fieldDelimiterPos = PREFERENCES.find_first_of('|');
            key               = PREFERENCES.substr(0, fieldDelimiterPos);
            PREFERENCES       = PREFERENCES.substr(fieldDelimiterPos + 1);

            fieldDelimiterPos = PREFERENCES.find_first_of('|');
            value             = PREFERENCES.substr(0, fieldDelimiterPos);
            PREFERENCES       = PREFERENCES.substr(fieldDelimiterPos + 1);

            if (key.compare("led") == 0) {
              settings.led = (value.compare("true") == 0) ? true : false;
            } else if (key.compare("buzzer") == 0) {
              settings.buzzer = (value.compare("true") == 0) ? true : false;
            } else if (key.compare("connect_timeout") == 0) {
              settings.connect_timeout = atoi(value.c_str());
            } else if (key.compare("reconnect_timeout") == 0) {
              settings.reconnect_timeout = atoi(value.c_str());
            } else if (key.compare("scan_timeout") == 0) {
              settings.scan_timeout = atoi(value.c_str());
            }

            #ifdef DEBUG
              Serial.print("\n");
              Serial.print("KEY: " + String(key.c_str()) + "\n");
              Serial.print("VALUE: " + String(value.c_str()) + "\n");
            #endif
          }

          EEPROM.begin(EEPROM_SIZE);
          EEPROM.writeBytes(1, (const void*)&settings, sizeof(cv_settings_t));
          EEPROM.commit();
          EEPROM.end();

          digitalWrite(pinToBuzzer, LOW);

          ESP.restart();
        }
      }

    }

  



/** ---------------------------------------------------------------------------------------------------- **
 **                                  FUNCTIONS, PROCEDURES AND METHODS                                   **
 ** ---------------------------------------------------------------------------------------------------- */

// Modificação Christian - Correção Bug do Espaço

// Função para ler dados na EEPROM do ESP
//
// Argumentos:
//      offsetAddress : o número do endereço no qual a leitura deve iniciar
//      str           : string que receberá o que for lido da EEPROM
//
// Retorno:
//      O próximo endereço disponível para leitura
//
int EepRead(int offsetAddress, std::string *str) {
  EEPROM.begin(EEPROM_SIZE);

  int lenSTR = EEPROM.read(offsetAddress); // lê o tamanho da string a seguir
  char data[lenSTR + 1]; // cria local para armazenar a string

  for (int i = 0; i < lenSTR; ++i) {
    data[i] = EEPROM.read(offsetAddress + 1 + i); // leitura byte a byte
  }
  data[lenSTR] = '\0'; // fim da string

  EEPROM.end();

  *str = data; // cópia da string no endereço passado

  return offsetAddress + 1 + lenSTR; 
}

// Função para escrever dados na EEPROM do ESP
//
// Argumentos:
//      offsetAddress : o número do endereço no qual a escrita deverá começar
//      str           : string a ser escrita na EEPROM
//
// Retorno:
//      O próximo endereço disponível para escrita
//
int EepWrite(int offsetAddress, const std::string &str) {
  byte len = str.length();

  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(offsetAddress, len); // escreve o tamanho da string que será copiada
  for (int i = 0; i < len; ++i) {
    EEPROM.write(offsetAddress + i + 1, str[i]); // escreve byte a byte
  }
  EEPROM.commit();
  EEPROM.end();

  return offsetAddress + 1 + len;
}

// FIM Modificação Christian - Correção Bug do Espaço

void SetupNimBLE() {


  if (!NimBLEDevice::getInitialized()) {
    NimBLEDevice::init(_NimBLEDevice_Name);
     NimBLEDevice::setMTU(nmtu);  // Aumenta o tamanho do pacote
     

    WI_Address = macAddressNumber(NimBLEDevice::toString());

    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND | /*BLE_SM_PAIR_AUTHREQ_MITM |*/ BLE_SM_PAIR_AUTHREQ_SC);

#ifdef DEBUG
    Serial.println("");
    Serial.println("BLE SERVER INITIALIZED");
    Serial.print("Hostname: ");
    Serial.println(hostnameWiFi);
#endif
  }

  pServer = NimBLEDevice::createServer();
  pServer->advertiseOnDisconnect(false);

  NimBLEService* pWifiService = pServer->createService(WIFI_SERVICE_UUID);
  NimBLECharacteristic* pWifiSSIDCharacteristic = pWifiService->createCharacteristic(
        WIFI_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
        NIMBLE_PROPERTY::WRITE |
        NIMBLE_PROPERTY::NOTIFY |
        NIMBLE_PROPERTY::WRITE_NR
      );

  pWifiSSIDCharacteristic->setValue("");
  pWifiSSIDCharacteristic->setCallbacks(&chrCallbacks);

  pWifiService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

  pAdvertising->addServiceUUID(pWifiService->getUUID());
  pAdvertising->setScanResponse(true);

  pAdvertising->start();
}

void SetupNimBLE_Client() {
  if (!NimBLEDevice::getInitialized() || isFactoryReset) {
    NimBLEDevice::init("");
    NimBLEDevice::setMTU(nmtu);  // Define MTU maior para melhor desempenho

    WI_Address = macAddressNumber(NimBLEDevice::toString());

    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND | /*BLE_SM_PAIR_AUTHREQ_MITM |*/ BLE_SM_PAIR_AUTHREQ_SC);

#ifdef DEBUG

    Serial.println("");
    Serial.println("BLE CLIENT INITIALIZED");
#endif
  }

  NimBLEScan* pScan = NimBLEDevice::getScan();

  pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks(), false);
  pScan->setInterval(15);
  pScan->setWindow(8);

  if (doFIScan) {
    scannedFI.clear();

    pScan->start(settings.scan_timeout/500, ScanCallbackNimBLE_Client);
  } else {
    pScan->start(scanTime, ScanCallbackNimBLE_Client);
  }
}

bool DeinitNimBLE() {
  if (pServer != nullptr) {
    pServer->stopAdvertising();
    NimBLEDevice::deinit(true);
    if (!NimBLEDevice::getInitialized()) {
      pServer = nullptr;
      return true;
    } else {
      return false;
    }
  } else {
    NimBLEDevice::deinit(true);
    if (!NimBLEDevice::getInitialized()) {
      return true;
    } else {
      return false;
    }
  }
}

bool ConnectNimBLE() {
  NimBLEClient* pClient;
  NimBLERemoteService* pSvc;
  NimBLERemoteCharacteristic* pChr;
  size_t fieldDelimiterPos;
  uint8_t writeBuffer[256];
  uint32_t releaseNumber;
  uint32_t randomToBeSent;
  uint32_t starterPistol;

  pClient = nullptr;
  pSvc    = nullptr;
  pChr    = nullptr;

  if (!pClient) {
    if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS)
      return false;

    pClient = NimBLEDevice::createClient();

    pClient->setConnectionParams(6, 6, 0, 30);
    pClient->setConnectTimeout(3); // Timeout reduzido para conexão BLE


    if (!pClient->connect(advDevice)) {
      NimBLEDevice::deleteClient(pClient);

#ifdef DEBUG
      Serial.println("");
      Serial.print("FAILED TO CONNECT TO ");
      Serial.println(FI_Address.c_str());
#endif

      return false;
    }
  }

  if (!pClient->isConnected()) {
    if (!pClient->connect(advDevice)) {

#ifdef DEBUG
      Serial.println("");
      Serial.print("FAILED TO CONNECT TO ");
      Serial.println(FI_Address.c_str());
#endif

      return false;
    }
  }

#ifdef DEBUG
  Serial.println("");
  Serial.print("CONNECTED TO ");
  Serial.print(pClient->getPeerAddress().toString().c_str());
  Serial.print(" WITH RSSI ");
  Serial.println(pClient->getRssi());
#endif

  pSvc = pClient->getService(TOKEN_SERVICE_UUID);

  if (!pSvc) {
#ifdef DEBUG
    Serial.println("");
    Serial.print("SERVICE ");
    Serial.print(TOKEN_SERVICE_UUID);
    Serial.print(" NOT FOUND AT ");
    Serial.println(pClient->getPeerAddress().toString().c_str());
#endif

    pClient->disconnect();

    return false;
  }

  pChr = pSvc->getCharacteristic(TOKEN_CHARACTERISTIC_UUID);

  if (!pChr) {
#ifdef DEBUG
    Serial.println("");
    Serial.print("CHARACTERISTIC ");
    Serial.print(TOKEN_CHARACTERISTIC_UUID);
    Serial.print(" NOT FOUND AT SERVICE ");
    Serial.print(pSvc->getUUID().toString().c_str());
    Serial.print(" FROM ");
    Serial.println(pClient->getPeerAddress().toString().c_str());
#endif

    pClient->disconnect();

    return false;
  }

  if (pChr->canWrite()) {
    if (RELEASE.empty())
      return false;

    releaseNumber = strtoul(RELEASE.c_str(), nullptr, 0);

    if ((0 <= releaseNumber) && (releaseNumber < 100)) {
      if (TOKEN.empty())
        return false;

      if (!(pChr->writeValue(TOKEN))) {
        pClient->disconnect();

        return false;
      }
    }

    if ((100 <= releaseNumber) && (releaseNumber < 200)) {
      if (TOKEN.empty()   ||
          TOKEN_2.empty() ||
          TIMESTAMP.empty())
        return false;

      if (!(pChr->writeValue(TOKEN) && pChr->writeValue(TOKEN_2) && pChr->writeValue(TIMESTAMP))) {
        pClient->disconnect();
        return false;
      }
    }

    if ((200 <= releaseNumber) && (releaseNumber < 300)) {
      if (TOKEN.empty()     ||
          TOKEN_2.empty()   ||
          TIMESTAMP.empty() ||
          ACTION.empty())
        return false;

      if (!(pChr->writeValue(TOKEN) && pChr->writeValue(TOKEN_2) && pChr->writeValue(TIMESTAMP) && pChr->writeValue(ACTION))) {
        pClient->disconnect();
        return false;
      }
    }

    if (releaseNumber >= 300) {
      SEED    = TOKEN;
      SEED_2  = TOKEN_2;

      randomToBeSent = random(0, 9999);

      sprintf((char*)writeBuffer, "%lu", randomToBeSent);

      if (!(pChr->writeValue(std::string((char*)writeBuffer)))) {
        pClient->disconnect();
        return false;
      }

      statusMessage = ReceiveNotificationFromClient(pClient, pChr);

      if (statusMessage.empty()) {
        pClient->disconnect();

        return false;
      }

      fieldDelimiterPos = statusMessage.find_first_of(0x0A);

      if (fieldDelimiterPos != std::string::npos) {
        SALTOS   = strtoul((statusMessage.substr(0, fieldDelimiterPos)).c_str(), nullptr, 0) - randomToBeSent - strtoul(SEED.c_str(), nullptr, 0);
        SALTOS_2 = strtoul((statusMessage.substr(fieldDelimiterPos + 1)).c_str(), nullptr, 0) - randomToBeSent - strtoul(SEED_2.c_str(), nullptr, 0);
      } else {
        SALTOS   = strtoul(statusMessage.c_str(), nullptr, 0) - randomToBeSent - strtoul(SEED.c_str(), nullptr, 0);
        SALTOS_2 = SALTOS;
      }

#ifdef DEBUG
      Serial.println("");
      Serial.print("SALTOS = ");
      Serial.println(SALTOS);
      Serial.print("SALTOS_2 = ");
      Serial.println(SALTOS_2);
#endif

      TOKENUL   = getpass_do_lolis(SALTOS, strtoul(SEED.c_str(), nullptr, 0));
      TOKENUL_2 = getpass_do_lolis(SALTOS_2, strtoul(SEED_2.c_str(), nullptr, 0));

      sprintf((char*)writeBuffer, "%lu", TOKENUL);
      TOKEN = std::string((char*)writeBuffer);

      sprintf((char*)writeBuffer, "%lu", TOKENUL_2);
      TOKEN_2 = std::string((char*)writeBuffer);

      if (!(pChr->writeValue(TOKEN) && pChr->writeValue(TOKEN_2) && pChr->writeValue(ACTION))) {
#ifdef DEBUG
        Serial.println("");
        Serial.print("FAILED TO WRITE ");
        Serial.print(TOKEN.c_str());
        Serial.print(", ");
        Serial.print(TOKEN_2.c_str());
        Serial.print(" OR ");
        Serial.print(ACTION.c_str());
        Serial.print(" TO ");
        Serial.print(pChr->getUUID().toString().c_str());
        Serial.print(" AT ");
        Serial.println(pClient->getPeerAddress().toString().c_str());
#endif

        pClient->disconnect();
        return false;
      }

#ifdef DEBUG
      Serial.println("");
      Serial.print("WROTE ");
      Serial.print(TOKEN.c_str());
      Serial.print(", ");
      Serial.print(TOKEN_2.c_str());
      Serial.print(" AND ");
      Serial.print(ACTION.c_str());
      Serial.print(" TO ");
      Serial.print(pChr->getUUID().toString().c_str());
      Serial.print(" AT ");
      Serial.println(pClient->getPeerAddress().toString().c_str());
#endif
    }
  }

  statusMessage = ReceiveNotificationFromClient(pClient, pChr);

  if (statusMessage.empty()) {
    pClient->disconnect();

    return false;
  }

  return true;
}

void NotifyCallbackNimBLE_Client(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  if (millis() - starterPistol_Notification > DEBOUNCE)
    notificationPayload.clear();

  notificationPayload.append((char*)pData, length);

#ifdef DEBUG
  Serial.println("");
  Serial.print("NOTIFICATION RECEIVED FROM ");
  Serial.print(pRemoteCharacteristic->getUUID().toString().c_str());
  Serial.print(" IN ");
  Serial.print(pRemoteCharacteristic->getRemoteService()->getUUID().toString().c_str());
  Serial.print(" AT ");
  Serial.print(pRemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().toString().c_str());
  Serial.print(" WITH ");
  Serial.println(notificationPayload.c_str());
#endif

  hasNotifyResponse = true;

  starterPistol_Notification = millis();
}

void ScanCallbackNimBLE_Client(NimBLEScanResults results) {
  if (doFIScan) {
    scannedFI.pop_back();

    doFIScan = false;
  }
}

void SetupWifi(bool _Config) {
 
    WiFi.disconnect(true);  // Garante que qualquer conexão anterior seja encerrada
    delay(100);

    WiFi.mode(WIFI_STA);  // Define o ESP como estação Wi-Fi
    WiFi.setHostname(hostnameWiFi); // **Definir hostname ANTES de conectar!**


    bool _InvalidNetwork;

  _InvalidNetwork = false;

  while (!isWifiConnected) {
    
    if (_Config || _InvalidNetwork) {
      _NimBLEDevice_Name = "CHAVIWIFI";

      hasUserID       = false;
      hasWifiSSID     = false;
      hasWifiPassword = false;

      SetupNimBLE();

#ifdef DEBUG
      Serial.println("");
      Serial.println("BLE SERVER: ENABLED ");
      Serial.print("MAC-WIFI: ");
      Serial.println(WI_Address.c_str());

#endif

      if (settings.buzzer)
        // tone(pinToBuzzer, STANDARD_BUZZER_FREQUENCY, 0.25 * STANDARD_BUZZER_DURATION, 0);

      while ((!hasWifiSSID) || (!hasWifiPassword)) {
        CheckFactoryReset();
      }

 /*     if (settings.buzzer)
        tone(pinToBuzzer, STANDARD_BUZZER_FREQUENCY, 0.25 * STANDARD_BUZZER_DURATION, 0);
*/
      std::string Notification;

      Notification.append(WI_Address);
      Notification.push_back('|');
      Notification.append(FIRMWARE_RELEASE);

      if (pServer->getConnectedCount()) {
        NimBLEService* pSvc = pServer->getServiceByUUID(WIFI_SERVICE_UUID);
        if (pSvc) {
          NimBLECharacteristic* pChr = pSvc->getCharacteristic(WIFI_CHARACTERISTIC_UUID);
          if (pChr) {
            pChr->setValue(Notification);
            pChr->notify(true);

            delay(1000);
          }
        }
      }

      while (!DeinitNimBLE()) {
        delay(1);
      }

#ifdef DEBUG
      Serial.println("");
      Serial.println("BLE SERVER: DISABLED");
      Serial.println(WI_Address.c_str());
#endif
    } else {
      
      addrOffset = sizeof(cv_settings_t) + 1;
      
      addrOffset = EepRead(addrOffset, &_NimBLEDevice_Name);
      addrOffset = EepRead(addrOffset, &SSID);
      addrOffset = EepRead(addrOffset, &Password);
      addrOffset = EepRead(addrOffset, &USERID);

    }

    if (SSID.size()) {

#ifdef DEBUG
      Serial.println("");
      Serial.print(WI_Address.c_str());
      Serial.println("");
      Serial.print("SSID: ");
      Serial.println(SSID.c_str());
      Serial.print("Password: ");
      Serial.println(Password.c_str());
#endif

      isWifiConnected = ConnectWifi(_Config);


      if (isWifiConnected) {
        _NimBLEDevice_Name = WI_Address;

        addrOffset = sizeof(cv_settings_t) + 1;

        addrOffset = EepWrite(addrOffset, _NimBLEDevice_Name);
        addrOffset = EepWrite(addrOffset, SSID);
        addrOffset = EepWrite(addrOffset, Password);
        addrOffset = EepWrite(addrOffset, USERID);

        if (settings.led) {
          digitalWrite(P1_LED, HIGH);
          digitalWrite(P2_LED, HIGH);
          digitalWrite(P3_LED, HIGH);
        }


      } else {
        _InvalidNetwork = true;
      }
    } else {
      _InvalidNetwork = true;
    }
  }
}

bool ConnectWifi(bool FirstConnection) {



  String getPublicIP(); // 

   
 #ifdef DEBUG
  Serial.println("");
  Serial.print("CONNECTING TO FirstConnection ");
  Serial.print(SSID.c_str());
  Serial.print("...");
  Serial.println("");

#endif
int maxTentativas = 3;  // Número máximo de tentativas
    unsigned long timeoutConexao = 15000;  // 15 segundos por tentativa
    int tentativas = 0;

    while (tentativas < maxTentativas) {
        Serial.print("Tentativa ");
        Serial.print(tentativas + 1);
        Serial.println(" de conexão ao Wi-Fi...");
        CheckFactoryReset();
        
        if (Password.empty()) {
            WiFi.begin(SSID.c_str());
            Serial.println("Senha vazia, tentando conexão sem senha...");
            CheckFactoryReset();
        } else {
            WiFi.begin(SSID.c_str(), Password.c_str());
            Serial.println("Tentando conexão com senha...");
            CheckFactoryReset();
        }

        unsigned long tempoInicio = millis();

        // Aguarda até 30 segundos para conectar
        while ((WiFi.status() != WL_CONNECTED) && (millis() - tempoInicio < timeoutConexao)) {
            delay(500);  // Pequeno atraso para evitar sobrecarga do loop
            Serial.print(".");
            LEDFIRSTFIVE();
            CheckFactoryReset();
           
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("");
            Serial.print("Hostname: ");
            Serial.print(hostnameWiFi);
            Serial.println("");
            Serial.print("User ID: ");
            Serial.println(USERID.c_str());
            Serial.println("");
            Serial.print("CONNECTED TO ");
            Serial.println(SSID.c_str());
            Serial.println("Wi-Fi conectado!");
            Serial.print("IP LAN (LOCAL): ");
            Serial.println(WiFi.localIP());
            Serial.print("IP WAN (Saída/NAT): ");
            Serial.println(getPublicIP());
            
                    
            return true;
        }

        Serial.println("\nFalha na conexão. Tentando novamente...");
        tentativas++;
    }

    CheckFactoryReset();
    // Se não conseguir conectar após todas as tentativas, reinicia a ESP
    Serial.println("Não foi possível conectar ao Wi-Fi após 5 tentativas. Reiniciando...");
    ESP.restart();
    return false;  // Nunca será atingido, pois a ESP será reiniciada antes

  if (FirstConnection) {
    unsigned long starterPistol = millis();
    unsigned long millisSince   = starterPistol;

    EnableLEDFade(1000, 1000);
    do {
      CheckFactoryReset();

      if (settings.led)
        LEDFade();

      if (millis() - millisSince > 10000) {
      

        if (Password.empty()) {
          WiFi.begin(SSID.c_str());
        } else {
          WiFi.begin(SSID.c_str(), Password.c_str());
        }

        millisSince = millis();
      }
    } while ((WiFi.status() != WL_CONNECTED) && ((millis() - starterPistol) < settings.connect_timeout));
    DisableLEDFade();
  } else {
    unsigned long starterPistol = millis();
    unsigned long millisSince   = starterPistol;

    EnableLEDFade(1000, 1000);
    do {
      CheckFactoryReset();




      if (settings.led)
        LEDFade();

      if (millis() - millisSince > 10000) {


        if (Password.empty()) {
          WiFi.begin(SSID.c_str());
        } else {
          WiFi.begin(SSID.c_str(), Password.c_str());
        }

        millisSince = millis();
      }
    } while ((WiFi.status() != WL_CONNECTED) && ((millis() - starterPistol) < settings.reconnect_timeout));
    DisableLEDFade();
  }

  if (WiFi.status() == WL_CONNECTED) {





#ifdef DEBUG
    Serial.print("Hostname: ");
    Serial.println(WiFi.getHostname()); // Verifica se o hostname foi configurado corretamente
    Serial.println("");
    Serial.print("CONNECTED TO ");
    Serial.println(SSID.c_str());
    Serial.println("Wi-Fi conectado!");
    Serial.print("IP LAN (LOCAL): ");
    Serial.println(WiFi.localIP());
    Serial.print("IP WAN (Saída/NAT): ");
    Serial.println(getPublicIP());

#endif

    return true;
  } else {
#ifdef DEBUG
    Serial.println("");
    Serial.print("FAILED TO CONNECT TO ");
    Serial.println(SSID.c_str());
#endif

    return false;
  }
}

void SetupMQTT() {
  espClient.setCACert(ca_cert);
  client.setServer(mqttServer, 8883);
  client.setCallback(CallbackMQTT);
  client.setBufferSize(MQTT_MAX_BUFFER_SIZE);
}

void ConnectMQTT() {
  
  // Verificação mais robusta da conexão
  if (!client.connected()) {
    #ifdef DEBUG
    Serial.println("\nAttempting MQTT connection...");
    #endif

    // Tentativa de conexão com tratamento de erro
    int mqtt_retries = 0;
    while (!client.connect(WI_Address.c_str()) && mqtt_retries < 3) {
      #ifdef DEBUG
      Serial.print("MQTT connection failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 2 seconds...");
      #endif
      
      delay(2000);
      mqtt_retries++;
      
      // Verifica se houve alteração no estado WiFi durante as tentativas
      if (WiFi.status() != WL_CONNECTED) {
        #ifdef DEBUG
        Serial.println("WiFi connection lost during MQTT retries!");
        #endif
        return; // Sai da função para tentar reconexão WiFi primeiro
      }
    }

    if (client.connected()) {
      #ifdef DEBUG
      Serial.println("\nMQTT connected!");
      #endif

      // Inscrição no tópico com verificação
      if (isFISent) {
        bool sub_result = client.subscribe(WI_Address.c_str());
        #ifdef DEBUG
        if (sub_result) {
          Serial.print("Subscribed to: ");
          Serial.println(WI_Address.c_str());
        } else {
          Serial.println("Subscription failed!");
        }
        #endif
      }
    } else {
      #ifdef DEBUG
      Serial.println("MQTT connection failed after retries!");
      #endif
      return;
    }
  }

  // Publicação da mensagem inicial se necessário
  if (!isFISent && client.connected()) {
    scannedFIMessage.clear();
    scannedFIMessage.append("{\"type\":\"list\",\"macWifi\":\"");
    scannedFIMessage.append(WI_Address);
    scannedFIMessage.append("\",\"macBt\":[");
    scannedFIMessage.append(scannedFI.c_str());
    scannedFIMessage.append("],\"userId\":\"");
    scannedFIMessage.append(USERID);
    scannedFIMessage.append("\"}");

    bool pub_result = client.publish(WI_Address.c_str(), scannedFIMessage.c_str());
    
    #ifdef DEBUG
    if (pub_result) {
      Serial.print("Published initial message to ");
      Serial.println(WI_Address.c_str());
    } else {
      Serial.println("Failed to publish initial message!");
    }
    #endif

    if (pub_result) {
      isFISent = true;
      // Não desconecta imediatamente após publicação bem-sucedida
      // Mantém conexão para receber comandos
    }
  }
}
void CallbackMQTT(char* topic, byte* payload, unsigned int length) {

  if ((char)payload[0]=='{') {
    return;
 }

      Serial.print("Mensagem recebida no tópico: ");
    Serial.println(topic);
    Serial.print("Payload: ");
    for (int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();
  std::string sPayload;
  uint8_t i, j, k;
  uint32_t releaseNumber;

  sPayload.assign((char*)payload, length);

#ifdef DEBUG
  Serial.println("");
  Serial.println("TAMANHO PAYLOAD: ");
  Serial.println(sPayload.length());
  Serial.println("");
  Serial.println("MESSAGE RECEIVED");
  Serial.println("PAYLOAD: ");
  Serial.println(sPayload.c_str());
#endif


  // Se não tiver delimitador esperado, aborta
  if (sPayload.find('|') == std::string::npos) {
    Serial.println("Mensagem sem delimitador '|'. Ignorando.");
    return;
  }

  // Parse Payload
  k = 0;      
  for (i = 0; i < sPayload.length(); i++) {


    for (j = i; j < sPayload.length(); j++) {

      if (sPayload[j] != '|')
        continue;

      break;
    }

    if (k < MQTT_ARGUMENTS_LEN)
      arguments[k++] = sPayload.substr(i, j - i);

    i = j;
  }

  if (arguments[0].empty())
    return;

  if (arguments[0].compare("token") == 0) {
    if (arguments[1].empty() ||
        arguments[2].empty())
      return;

    FI_Address  = arguments[1];
    RELEASE     = arguments[2];

#ifdef DEBUG
    Serial.println("");
    Serial.print("MAC ADDRESS: ");
    Serial.println(FI_Address.c_str());
    Serial.print("RELEASE: ");
    Serial.println(RELEASE.c_str());
#endif

    releaseNumber = strtoul(RELEASE.c_str(), nullptr, 0);

    if ((0 <= releaseNumber) && (releaseNumber < 100)) {
      if (arguments[3].empty())
        return;

      TOKEN = arguments[3];

#ifdef DEBUG
      Serial.print("TOKEN: ");
      Serial.println(TOKEN.c_str());
#endif

      MQTTReceived  = true;
      doSendToken   = true;

      return;
    }

    if ((100 <= releaseNumber) && (releaseNumber < 200)) {
      if (arguments[3].empty() ||
          arguments[4].empty() ||
          arguments[5].empty())
        return;

      TOKEN     = arguments[3];
      TOKEN_2   = arguments[4];
      TIMESTAMP = arguments[5];

#ifdef DEBUG
      Serial.print("TOKEN: ");
      Serial.println(TOKEN.c_str());
      Serial.print("TOKEN 2: ");
      Serial.println(TOKEN_2.c_str());
      Serial.print("TIMESTAMP: ");
      Serial.println(TIMESTAMP.c_str());
#endif

      MQTTReceived  = true;
      doSendToken   = true;

      return;
    }

    if ((200 <= releaseNumber) && (releaseNumber < 300)) {
      if (arguments[3].empty() ||
          arguments[4].empty() ||
          arguments[5].empty() ||
          arguments[6].empty())
        return;

      TOKEN     = arguments[3];
      TOKEN_2   = arguments[4];
      TIMESTAMP = arguments[5];
      ACTION    = arguments[6];

#ifdef DEBUG
      Serial.print("TOKEN: ");
      Serial.println(TOKEN.c_str());
      Serial.print("TOKEN 2: ");
      Serial.println(TOKEN_2.c_str());
      Serial.print("TIMESTAMP: ");
      Serial.println(TIMESTAMP.c_str());
      Serial.print("ACTION: ");
      Serial.println(ACTION.c_str());
#endif

      MQTTReceived  = true;
      doSendToken   = true;

      return;
    }

    if (300 <= releaseNumber) {
      if (arguments[3].empty() ||
          arguments[4].empty() ||
          arguments[5].empty())
        return;

      TOKEN   = arguments[3];
      TOKEN_2 = arguments[4];
      ACTION  = arguments[5];

#ifdef DEBUG
      Serial.print("SEED: ");
      Serial.println(TOKEN.c_str());
      Serial.print("SEED 2: ");
      Serial.println(TOKEN_2.c_str());
      Serial.print("ACTION: ");
      Serial.println(ACTION.c_str());
#endif

      MQTTReceived  = true;
      doSendToken   = true;

      return;
    }

    return;
  }

  if (arguments[0].compare("config") == 0) {
    if (arguments[1].empty())
      return;

    if (arguments[1].compare("wifi") == 0) {
      MQTTReceived = true;
      doConfigWifi = true;

      return;
    }

    if (arguments[1].compare("refresh") == 0) {
      MQTTReceived  = true;
      doRefreshFI   = true;

      return;
    }

    
    if (arguments[1].compare("ota") == 0) {
      MQTTReceived = false;
      checkAndUpdateOTA();
      return;
    }

    if (arguments[1].compare("factory") == 0) {
      MQTTReceived    = true;
      doFactoryReset  = true;

      return;
    }

    if (arguments[1].compare("preferences") == 0) {
      PREFERENCES = sPayload.substr(sPayload.find(arguments[1]) + arguments[1].length() + 1);

      MQTTReceived  = true;
      doPreferences = true;

      return;
    }

    return;
  }
  #ifdef EI_W
  if (arguments[0].compare("cmd") == 0) {
    if (arguments[1].compare("relay") == 0) {
      MQTTReceived  = true;
      doRelay       = true;

      return;
    }
  }
  #endif
}

void PublishMQTT(std::string s, MESSAGE_TYPE mt) {
  std::string msg;

  if (!client.connected()) {
    ConnectMQTT();
  }

    // Processa a string `s` para modificar YZW
    if (s.length() >= 4) {
        char x = s[0];  // Obtém o primeiro caractere (X)
        int yzw = atoi(s.substr(1).c_str());  // Converte os últimos três caracteres (YZW) para inteiro

        // Faz a equação: YZW - 1, garantindo que não fique negativo
        yzw = std::max(yzw - 1, 0);

        // Formata de volta com três dígitos
        char buffer[5];  
        snprintf(buffer, sizeof(buffer), "%c%03d", x, yzw);
        s = std::string(buffer);
    }




  msg = "{\"macWifi\":\"" + WI_Address + "\",\"macBluetooth\":\"" + FI_Address + "\",\"type\":\"";

  

  if (mt == MESSAGE_TYPE::_STATUS) {
    msg += "status";
  } else if (mt == MESSAGE_TYPE::_ERROR) {
    msg += "error";
  }


  msg += "\",\"message\":\"" + s + "\"}";
  //msg += "\",\"message\":\"2070\"}";

#ifndef DEBUG
  client.publish(WI_Address.c_str(), msg.c_str());
#endif

#ifdef DEBUG
  if (client.publish(WI_Address.c_str(), msg.c_str())) {
    Serial.println("");
    Serial.print("PUBLISHED ");
    Serial.print(msg.c_str());
    Serial.print(" TO ");
    Serial.print(WI_Address.c_str());
    Serial.print(" AT ");
    Serial.println(mqttServer);
  }
#endif
}

std::string macAddressNumber (std::string macAddress) {
  std::string macAddressNumber;

  macAddressNumber.append(macAddress.substr(0, 2));
  macAddressNumber.append(macAddress.substr(3, 2));
  macAddressNumber.append(macAddress.substr(6, 2));
  macAddressNumber.append(macAddress.substr(9, 2));
  macAddressNumber.append(macAddress.substr(12, 2));
  macAddressNumber.append(macAddress.substr(15, 2));

  return macAddressNumber;
}

std::string ReceiveNotificationFromClient (NimBLEClient *pClient, NimBLERemoteCharacteristic* pChr) {
  std::string payload;
  uint32_t starterPistol;

  if (pChr->canNotify()) {
    if (!pChr->subscribe(true, NotifyCallbackNimBLE_Client)) {
#ifdef DEBUG
      Serial.println("");
      Serial.print("FAILED TO SUBSCRIBE TO ");
      Serial.print(pChr->getUUID().toString().c_str());
      Serial.print(" AT ");
      Serial.println(pClient->getPeerAddress().toString().c_str());
#endif

      payload.clear();

      return payload;
    }
  } else if (pChr->canIndicate()) {
    if (!pChr->subscribe(false, NotifyCallbackNimBLE_Client)) {
#ifdef DEBUG
      Serial.println("");
      Serial.print("FAILED TO SUBSCRIBE TO ");
      Serial.print(pChr->getUUID().toString().c_str());
      Serial.print(" AT ");
      Serial.println(pClient->getPeerAddress().toString().c_str());
#endif

      payload.clear();

      return payload;
    }
  } else {
#ifdef DEBUG
    Serial.println("");
    Serial.print("FAILED TO SUBSCRIBE TO ");
    Serial.print(pChr->getUUID().toString().c_str());
    Serial.print(" AT ");
    Serial.println(pClient->getPeerAddress().toString().c_str());
#endif

    payload.clear();

    return payload;
  }

#ifdef DEBUG
  Serial.println("");
  Serial.print("WAITING FOR NOTIFICATION...");
#endif

  if (hasNotifyResponse)
    hasNotifyResponse = false;

  starterPistol = millis();

  if (!isEnabledLEDFade)
    EnableLEDFade(1000, 1000);

  while ((!hasNotifyResponse) && (millis() - starterPistol < 10000)) {
    if (settings.led)
      LEDFade();
  }

  if (!hasNotifyResponse) {
#ifdef DEBUG
    Serial.println("");
    Serial.println("NOTIFICATION TIMEOUT EXCEEDED");
#endif

    payload.clear();

    return payload;
  }

  while (millis() - starterPistol_Notification < DEBOUNCE) {
    if (settings.led)
      LEDFade();
  }

  payload.clear();
  payload.assign(notificationPayload);

  pChr->unsubscribe();

  hasNotifyResponse = false;

  return payload;
}

bool CheckFactoryReset() {
  if (digitalRead(pinToFactoryReset) == LOW) {
    if (!isFactoryResetButton) {
      isFactoryResetButton = true;
      isFactoryResetButtonSince = millis();
    }
    if (isFactoryResetButton) {
      if (millis() - isFactoryResetButtonSince > COUNTDOWN_FACTORY_RESET * 50) {
        EEPROM.begin(EEPROM_SIZE);
        for (int i = 0; i < EEPROM_SIZE; i++) {
          EEPROM.write(i, 0);
          EEPROM.commit();
        }
        EEPROM.end();

        tone(pinToBuzzer, STANDARD_BUZZER_FREQUENCY, 4 * STANDARD_BUZZER_DURATION, 0);

        ESP.restart();
      }
    }
  } else {
    isFactoryResetButton = false;
  }

  return isFactoryResetButton;
}

void Blink (BLINK_MODE bm, int Delay) {
  if (!settings.led)
    return;
  if (bm == 1) {
    digitalWrite(P1_LED, HIGH);
    delay(Delay);
    digitalWrite(P2_LED, HIGH);
    delay(Delay);
    digitalWrite(P3_LED, HIGH);
    delay(Delay);
    digitalWrite(P1_LED, LOW);
    delay(Delay);
    digitalWrite(P2_LED, LOW);
    delay(Delay);
    digitalWrite(P3_LED, LOW);
  }
  else if (bm == 2) {
    digitalWrite(P1_LED, HIGH);
    digitalWrite(P2_LED, HIGH);
    digitalWrite(P3_LED, HIGH);
    delay(Delay);
    digitalWrite(P1_LED, LOW);
    digitalWrite(P2_LED, LOW);
    digitalWrite(P3_LED, LOW);
  }
}

void LEDFade() {
  static uint32_t starterPistol     = millis();

  if (fadeInDuration < 255 || fadeOutDuration < 255) return;

  if (millis() - starterPistol > retentionDuration) {
    if (dutyCycle <= 0 || dutyCycle >= 255) {
      fadeAmount = -fadeAmount;
      retentionDuration = (retentionDuration == fadeInDuration / 255) ? fadeOutDuration / 255 : fadeInDuration / 255;
    }

    dutyCycle += fadeAmount;

    starterPistol = millis();
  }

  ledcWrite(LEDC_CHANNEL, (65535 / 255)*dutyCycle);

  digitalWrite(pinToBuzzer, LOW);
}

void EnableLEDFade(uint32_t fInDur, uint32_t fOutDur) {
  ledcSetup(LEDC_CHANNEL, LEDC_FREQ, LEDC_RES);

  ledcAttachPin(P1_LED, LEDC_CHANNEL);
  ledcAttachPin(P2_LED, LEDC_CHANNEL);
  ledcAttachPin(P3_LED, LEDC_CHANNEL);

  dutyCycle   = 0;
  fadeAmount  = -1;
  fadeInDuration = fInDur;
  fadeOutDuration = fOutDur;
  retentionDuration = fadeInDuration / 255;

  isEnabledLEDFade = true;
}

void LEDFIRSTFIVE(){

  int delayTime = 250; // Tempo em milissegundos que cada LED ficará ligado

    // Liga P1_LED e desliga os outros
    digitalWrite(P1_LED, HIGH);
    digitalWrite(P2_LED, LOW);
    digitalWrite(P3_LED, LOW);
    delay(delayTime);

    // Liga P2_LED e desliga os outros
    digitalWrite(P1_LED, LOW);
    digitalWrite(P2_LED, HIGH);
    digitalWrite(P3_LED, LOW);
    delay(delayTime);

    // Liga P3_LED e desliga os outros
    digitalWrite(P1_LED, LOW);
    digitalWrite(P2_LED, LOW);
    digitalWrite(P3_LED, HIGH);
    delay(delayTime);

        // Liga desliga tudo
    digitalWrite(P1_LED, LOW);
    digitalWrite(P2_LED, LOW);
    digitalWrite(P3_LED, LOW);
    delay(delayTime);
 
}

void DisableLEDFade() {
  while ((dutyCycle > 0) && (dutyCycle < 255)) {
    dutyCycle += fadeAmount;

    ledcWrite(LEDC_CHANNEL, (65535 / 255)*dutyCycle);

    delay(retentionDuration);
  }

  if (dutyCycle == 255) {
    while (dutyCycle > 0) {
      dutyCycle--;

      ledcWrite(LEDC_CHANNEL, (65535 / 255)*dutyCycle);

      delay(retentionDuration);
    }
  }

  ledcDetachPin(P1_LED);
  ledcDetachPin(P2_LED);
  ledcDetachPin(P3_LED);

  pinMode(P1_LED, OUTPUT);
  pinMode(P2_LED, OUTPUT);
  pinMode(P3_LED, OUTPUT);

  digitalWrite(P1_LED, LOW);
  digitalWrite(P2_LED, LOW);
  digitalWrite(P3_LED, LOW);

  digitalWrite(pinToBuzzer, LOW);

  isEnabledLEDFade = false;
}

unsigned long getpass_do_lolis (unsigned long difference, unsigned long seed) {
  unsigned long b[32] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  unsigned long y[4];

  for (int i = 1; i < 32; i++) {
    b[i] = b[i - 1] << 1;
  }

  for (int j = 0; j < difference; j++) {
    y[0] = (b[31] & seed) ? 1 : 0;
    y[1] = (b[21] & seed) ? 1 : 0;
    y[2] = (b[1]  & seed) ? 1 : 0;
    y[3] = (b[0]  & seed) ? 1 : 0;
    seed = seed << 1 | (y[0] ^ y[1] ^ y[2] ^ y[3]);
  }

  return seed;
}



void handleMQTTCommand(std::string sPayload) {
  uint8_t i, j, k;
  uint32_t releaseNumber;

  if (sPayload.find('|') == std::string::npos) {
    // Comando sem separador: pode ser algo como "check"
    sPayload.erase(std::remove_if(sPayload.begin(), sPayload.end(), ::isspace), sPayload.end());


    Serial.println("Mensagem sem delimitador '|'. Ignorando.");
    return;
  }




  k = 0;
  for (i = 0; i < sPayload.length(); i++) {
    for (j = i; j < sPayload.length(); j++) {
      if (sPayload[j] != '|')
        continue;
      break;
    }

    if (k < MQTT_ARGUMENTS_LEN)
      arguments[k++] = sPayload.substr(i, j - i);

    i = j;
  }

  if (arguments[0].empty())
    return;


  if (arguments[0] == "config") {
    if (arguments[1] == "refresh") {
      Serial.println("Comando 'config|refresh' via Serial.");
      MQTTReceived = true;
      doRefreshFI = true;
      return;
    }
    if (arguments[1] == "wifi") {
      Serial.println("Comando 'config|wifi' via Serial.");
      MQTTReceived = true;
      doConfigWifi = true;
      return;
    }
    if (arguments[1] == "factory") {
      Serial.println("Comando 'config|factory' via Serial.");
      MQTTReceived = true;
      doFactoryReset = true;
      return;
    }
    if (arguments[1] == "preferences") {
      Serial.println("Comando 'config|preferences' via Serial.");
      PREFERENCES = sPayload.substr(sPayload.find(arguments[1]) + arguments[1].length() + 1);
      MQTTReceived = true;
      doPreferences = true;
      return;
    }
          if (arguments[1].compare("ota") == 0) {
      MQTTReceived = false;
      checkAndUpdateOTA();
      return;
    }
  }

  if (arguments[0] == "token") {
    if (arguments[1].empty() || arguments[2].empty()) return;

    FI_Address = arguments[1];
    RELEASE = arguments[2];
    releaseNumber = strtoul(RELEASE.c_str(), nullptr, 0);

    if (releaseNumber >= 300) {
      if (arguments[3].empty() || arguments[4].empty() || arguments[5].empty()) return;

      TOKEN   = arguments[3];
      TOKEN_2 = arguments[4];
      ACTION  = arguments[5];

      Serial.println("Comando 'token' reconhecido via Serial.");
      MQTTReceived = true;
      doSendToken = true;
      return;
    }
  }
}




void checkAndUpdateOTA() {
  const char* firmwareUrl = "https://ota-ci.vercel.app/CI_311.bin";

  WiFiClientSecure client;
  // Removido: client.setInsecure();  // Sua versão não tem isso

  HTTPClient https;

  // Removido: https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  Serial.println("[OTA] Conectando ao servidor...");
  if (https.begin(client, firmwareUrl)) {
    int httpCode = https.GET();

    if (httpCode == HTTP_CODE_OK) {
      int contentLength = https.getSize();
      if (contentLength > 0) {
        bool canBegin = Update.begin(contentLength);
        if (canBegin) {
          Serial.println("[OTA] Iniciando atualização...");
          WiFiClient* stream = https.getStreamPtr();
          size_t written = Update.writeStream(*stream);
          Serial.printf("[OTA] Bytes escritos: %d\n", written);

          if (written == contentLength) {
            if (Update.end()) {
              if (Update.isFinished()) {
                Serial.println("[OTA] Atualização concluída. Reiniciando...");
                ESP.restart();
              } else {
                Serial.println("[OTA] Erro: Update não finalizado.");
              }
            } else {
              Serial.printf("[OTA] Erro: %s\n", Update.errorString());
            }
          } else {
            Serial.println("[OTA] Erro na escrita do firmware.");
          }
        } else {
          Serial.println("[OTA] Erro: não foi possível iniciar a atualização.");
        }
      } else {
        Serial.println("[OTA] Tamanho do conteúdo inválido.");
      }
    } else {
      Serial.printf("[OTA] Erro HTTP: %d\n", httpCode);
    }
    https.end();
  } else {
    Serial.println("[OTA] Falha ao conectar à URL");
  }
}

