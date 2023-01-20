#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <Ticker.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>

// * Includes
#include "settings.h"
#include "p1.h"

// * Initiate led blinker library
Ticker ticker;

// * Initiate WIFI client
// WiFiClientSecure espClient;
WiFiClient espClient;

// * Homey
HTTPClient http;
String homeyConnectHost;
String homeyEndpoint;

// * P1
P1Reader p1reader(&Serial, RX);
unsigned long p1_last_update_timestamp;

// **********************************
// * WIFI                           *
// **********************************

// * Gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager)
{
    Serial.println(F("Entered config mode"));
    Serial.println(WiFi.softAPIP());

    // * If you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());

    // * Entered config mode, make led toggle faster
    ticker.attach(0.2, tick);
}

// **********************************
// * Ticker (System LED Blinker)    *
// **********************************

// * Blink on-board Led
void tick()
{
    // * Toggle state
    int state = digitalRead(LED_BUILTIN); // * Get the current state of GPIO1 pin
    digitalWrite(LED_BUILTIN, !state);    // * Set pin to the opposite state
}

// **********************************
// * Homey                          *
// **********************************

String toJson(TelegramData data)
{
    // Prepare JSON
    DynamicJsonDocument doc(2048);
    doc["meterType"] = data.identification;
    doc["version"] = toMajorVersion(data.p1_version);
    doc["timestamp"] = toUnixTime(data.timestamp);
    doc["equipmentId"] = data.equipment_id;

    // Electricity
    JsonObject electricity = doc.createNestedObject("electricity");
    electricity["switchPosition"] = data.electricity_switch_position;

    // Power received (DSMR: delivered)
    JsonObject received = electricity.createNestedObject("received");
    received["tariff1"]["reading"] = data.energy_delivered_tariff1.val();
    received["tariff1"]["unit"] = "kWh";
    received["tariff2"]["reading"] = data.energy_delivered_tariff2.val();
    received["tariff2"]["unit"] = "kWh";
    received["actual"]["reading"] = data.power_delivered.val();
    received["actual"]["unit"] = "kW";

    // Power delivered (DSMR: returned)
    JsonObject delivered = electricity.createNestedObject("delivered");
    delivered["tariff1"]["reading"] = data.energy_returned_tariff1.val();
    delivered["tariff1"]["unit"] = "kWh";
    delivered["tariff2"]["reading"] = data.energy_returned_tariff2.val();
    delivered["tariff2"]["unit"] = "kWh";
    delivered["actual"]["reading"] = data.power_returned.val();
    delivered["actual"]["unit"] = "kW";

    // Tariff
    electricity["tariffIndicator"] = data.electricity_tariff.toInt();

    // Voltage sags
    JsonObject voltageSags = electricity.createNestedObject("voltageSags");
    voltageSags["L1"] = data.electricity_sags_l1;
    voltageSags["L2"] = data.electricity_sags_l2;
    voltageSags["L3"] = data.electricity_sags_l3;

    // Voltage swells
    JsonObject voltageSwell = electricity.createNestedObject("voltageSwell");
    voltageSwell["L1"] = data.electricity_swells_l1;
    voltageSwell["L2"] = data.electricity_swells_l2;
    voltageSwell["L3"] = data.electricity_swells_l3;

    // Instantaneous current
    JsonObject instantaneous = electricity.createNestedObject("instantaneous");
    instantaneous["current"]["L1"]["reading"] = data.current_l1.val();
    instantaneous["current"]["L1"]["unit"] = "A";
    instantaneous["current"]["L2"]["reading"] = data.current_l2.val();
    instantaneous["current"]["L2"]["unit"] = "A";
    instantaneous["current"]["L3"]["reading"] = data.current_l3.val();
    instantaneous["current"]["L3"]["unit"] = "A";

    // Instantaneous power positive
    instantaneous["power"]["positive"]["L1"]["reading"] = data.power_delivered_l1.val();
    instantaneous["power"]["positive"]["L1"]["unit"] = "kW";
    instantaneous["power"]["positive"]["L2"]["reading"] = data.power_delivered_l2.val();
    instantaneous["power"]["positive"]["L2"]["unit"] = "kW";
    instantaneous["power"]["positive"]["L3"]["reading"] = data.power_delivered_l3.val();
    instantaneous["power"]["positive"]["L3"]["unit"] = "kW";

    // Instantaneous power negative
    instantaneous["power"]["negative"]["L1"]["reading"] = data.power_returned_l1.val();
    instantaneous["power"]["negative"]["L1"]["unit"] = "kW";
    instantaneous["power"]["negative"]["L2"]["reading"] = data.power_returned_l2.val();
    instantaneous["power"]["negative"]["L2"]["unit"] = "kW";
    instantaneous["power"]["negative"]["L3"]["reading"] = data.power_returned_l3.val();
    instantaneous["power"]["negative"]["L3"]["unit"] = "kW";

    // Gas
    JsonObject gas = doc.createNestedObject("gas");
    gas["deviceType"] = "003";
    gas["equipmentId"] = data.gas_equipment_id;
    gas["reading"] = data.gas_delivered.val();
    gas["reportedPeriod"] = getGasReportedPeriod(data);
    gas["timestamp"] = toUnixTime(data.gas_delivered.timestamp);
    gas["unit"] = "m3";
    gas["valvePosition"] = data.gas_valve_position;

    String json;
    serializeJson(doc, json);

    return json;
}

void uploadJsonToHomeyConnectApi(String json)
{
    Serial.println(json);
    http.begin(espClient, homeyEndpoint);
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(json);
    if (httpResponseCode != 200)
    {
        Serial.println(httpResponseCode);
    }
    http.end();
}

// **********************************
// * EEPROM helpers                 *
// **********************************

String read_eeprom(int offset, int len)
{
    Serial.println(F("read_eeprom()"));

    String res = "";
    for (int i = 0; i < len; ++i)
    {
        res += char(EEPROM.read(i + offset));
    }
    return res;
}

void write_eeprom(int offset, int len, String value)
{
    Serial.println(F("write_eeprom()"));
    for (int i = 0; i < len; ++i)
    {
        if ((unsigned)i < value.length())
        {
            EEPROM.write(i + offset, value[i]);
        }
        else
        {
            EEPROM.write(i + offset, 0);
        }
    }
}

// ******************************************
// * Callback for saving WIFI config        *
// ******************************************

bool shouldSaveConfig = false;

// * Callback notifying us of the need to save config
void save_wifi_config_callback()
{
    Serial.println(F("Should save config"));
    shouldSaveConfig = true;
}

// **********************************
// * Setup OTA                      *
// **********************************

void setup_ota()
{
    Serial.println(F("Arduino OTA activated."));

    // * Port defaults to 8266
    ArduinoOTA.setPort(8266);

    // * Set hostname for OTA
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]()
                       { Serial.println(F("Arduino OTA: Start")); });

    ArduinoOTA.onEnd([]()
                     { Serial.println(F("Arduino OTA: End (Running reboot)")); });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                          { Serial.printf("Arduino OTA Progress: %u%%\r", (progress / (total / 100))); });

    ArduinoOTA.onError([](ota_error_t error)
                       {
        Serial.printf("Arduino OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println(F("Arduino OTA: Auth Failed"));
        else if (error == OTA_BEGIN_ERROR)
            Serial.println(F("Arduino OTA: Begin Failed"));
        else if (error == OTA_CONNECT_ERROR)
            Serial.println(F("Arduino OTA: Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println(F("Arduino OTA: Receive Failed"));
        else if (error == OTA_END_ERROR)
            Serial.println(F("Arduino OTA: End Failed")); });

    ArduinoOTA.begin();
    Serial.println(F("Arduino OTA finished"));
}

// **********************************
// * Setup MDNS discovery service   *
// **********************************

void setup_mdns()
{
    Serial.println(F("Starting MDNS responder service"));

    bool mdns_result = MDNS.begin(HOSTNAME);
    if (mdns_result)
    {
        MDNS.addService("http", "tcp", 80);
    }
}

// **********************************
// * Setup Main                     *
// **********************************

void setup()
{
    // * Configure EEPROM
    EEPROM.begin(512);

    // Setup a hw serial connection for communication with the P1 meter and logging (not using inversion)
    Serial.begin(BAUD_RATE, SERIAL_8N1, SERIAL_FULL);
    Serial.println("");
    Serial.println("Swapping UART0 RX to inverted");
    Serial.flush();

    // Invert the RX serialport by setting a register value, this way the TX might continue normally allowing the serial monitor to read println's
    USC0(UART0) = USC0(UART0) | BIT(UCRXI);
    Serial.println("Serial port is ready to receive.");

    // * Set led pin as output
    pinMode(LED_BUILTIN, OUTPUT);

    // * Start ticker with 0.5 because we start in AP mode and try to connect
    ticker.attach(0.6, tick);

    // * Get settings
    String settings_available = read_eeprom(24, 1);
    if (settings_available == "1")
    {
        read_eeprom(0, 24).toCharArray(HOMEY_ID, 24); // * 0-23
    }

    WiFiManagerParameter CUSTOM_HOMEY_ID("homey identifier", "Homey ID", HOMEY_ID, 24);

    // * WiFiManager local initialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    // * Reset settings - uncomment for testing
    // wifiManager.resetSettings();

    // * Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);

    // * Set timeout
    wifiManager.setConfigPortalTimeout(WIFI_TIMEOUT);

    // * Set save config callback
    wifiManager.setSaveConfigCallback(save_wifi_config_callback);

    // * Add all your parameters here
    wifiManager.addParameter(&CUSTOM_HOMEY_ID);

    // * Fetches SSID and pass and tries to connect
    // * Reset when no connection after 10 seconds
    if (!wifiManager.autoConnect())
    {
        Serial.println(F("Failed to connect to WIFI and hit timeout"));

        // * Reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(WIFI_TIMEOUT);
    }

    // * Read updated parameters
    strcpy(HOMEY_ID, CUSTOM_HOMEY_ID.getValue());

    // * Save the custom parameters to FS
    if (shouldSaveConfig)
    {
        Serial.println(F("Saving WiFiManager config"));

        write_eeprom(0, 24, HOMEY_ID); // * 0-23
        write_eeprom(24, 1, "1");      // * 24 --> always "1"
        EEPROM.commit();
    }

    // * If you get here you have connected to the WiFi
    Serial.println(F("Connected to WiFi."));

    // * Keep LED on
    ticker.detach();
    digitalWrite(LED_BUILTIN, HIGH);

    // * Configure OTA
    setup_ota();

    // * Startup MDNS Service
    setup_mdns();

    // * Set Homey identifier
    String homeyId(HOMEY_ID);
    // homeyEndpoint = "https://" + homeyId + ".connect.athom.com/api/app/com.p1/update";
    homeyEndpoint = "http://homey-" + homeyId + "/api/app/com.p1/update";
    Serial.println("Homey endpoint: " + homeyEndpoint);

    // * P1 reader
    p1reader.enable(true);
    p1_last_update_timestamp = millis();

    // * Allow HTTPS traffic
    // espClient.setInsecure();
}

// **********************************
// * Loop                           *
// **********************************

void loop()
{
    ArduinoOTA.handle();
    p1reader.loop();

    // Determine time since last update
    unsigned long now = millis();
    if (now - p1_last_update_timestamp > UPDATE_INTERVAL)
    {
        p1reader.enable(true);
        p1_last_update_timestamp = now;
    }

    // Read new P1 data
    if (p1reader.available())
    {
        TelegramData data;
        String err;
        if (p1reader.parse(&data, &err))
        {
            // Led on
            digitalWrite(LED_BUILTIN, LOW);

            // Upload data to Homey
            uploadJsonToHomeyConnectApi(toJson(data));

            // Led off
            digitalWrite(LED_BUILTIN, HIGH);
        }
        else
        {
            Serial.println(err);
        }
    }
}
