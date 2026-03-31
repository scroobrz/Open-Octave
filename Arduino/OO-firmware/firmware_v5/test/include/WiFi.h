#ifndef MOCK_WIFI_H
#define MOCK_WIFI_H

#include <cstdint>
#include <string>

// WiFi modes
#define WIFI_OFF    (0)
#define WIFI_STA    (1)
#define WIFI_AP     (2)
#define WIFI_AP_STA (3)

// WiFi status
#define WL_NO_SHIELD       255
#define WL_IDLE_STATUS     0
#define WL_NO_SSID_AVAIL   1
#define WL_SCAN_COMPLETED  2
#define WL_CONNECTED       3
#define WL_CONNECT_FAILED  4
#define WL_CONNECTION_LOST 5
#define WL_WRONG_PASSWORD  6
#define WL_DISCONNECTED    7

class WiFiClass {
public:
    int begin(const char* ssid, const char *passphrase = nullptr, int32_t channel = 0, const uint8_t* bssid = nullptr, bool connect = true);
    int begin(char* ssid, char *passphrase = nullptr, int32_t channel = 0, const uint8_t* bssid = nullptr, bool connect = true);

    const char* macAddress(void);
    const char* macAddress(uint8_t *mac);

    const char* localIP(void);

    uint8_t* BSSID(void);
    uint8_t* BSSID(uint8_t *bssid);

    int32_t RSSI(void);
    int8_t scanNetworks(void);

    const char* SSID(void) { return "TestNetwork"; }

    int hostByName(const char* aHostname, void* aResult);

    void mode(uint8_t mode);
    void disconnect(bool wifioff = false);

    void beginSmartConfig(void);
    bool smartConfigDone(void);
    void stopSmartConfig(void);

    void __setStatus(int status);

    uint8_t beginProvision(void);
};

extern WiFiClass WiFi;

#endif // MOCK_WIFI_H
