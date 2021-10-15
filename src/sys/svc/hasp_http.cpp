/* MIT License - Copyright (c) 2019-2021 Francis Van Roie
   For full license information read the LICENSE file in the project folder */

//#include "webServer.h"
#include "hasplib.h"
#include "ArduinoLog.h"

#if defined(ARDUINO_ARCH_ESP32)
#include "Update.h"
#endif

#include "hasp_conf.h"
#include "dev/device.h"
#include "hal/hasp_hal.h"

#include "hasp_gui.h"
#include "hasp_debug.h"
#include "hasp_config.h"

#if HASP_USE_HTTP > 0
#include "sys/net/hasp_network.h"

#if(HASP_USE_CAPTIVE_PORTAL > 0) && (HASP_USE_WIFI > 0)
#include <DNSServer.h>
#endif

// #ifdef USE_CONFIG_OVERRIDE
// #include "user_config_override.h"
// #endif

/* clang-format off */
//default theme
#ifndef D_HTTP_COLOR_TEXT
#define D_HTTP_COLOR_TEXT               "#000"       // Global text color - Black
#endif
#ifndef D_HTTP_COLOR_BACKGROUND
#define D_HTTP_COLOR_BACKGROUND         "#fff"       // Global background color - White
#endif
#ifndef D_HTTP_COLOR_INPUT_TEXT
#define D_HTTP_COLOR_INPUT_TEXT         "#000"       // Input text color - Black
#endif
#ifndef D_HTTP_COLOR_INPUT
#define D_HTTP_COLOR_INPUT              "#fff"       // Input background color - White
#endif
#ifndef D_HTTP_COLOR_INPUT_WARNING
#define D_HTTP_COLOR_INPUT_WARNING      "#f00"       // Input warning border color - Red
#endif
#ifndef D_HTTP_COLOR_BUTTON_TEXT
#define D_HTTP_COLOR_BUTTON_TEXT        "#fff"       // Button text color - White
#endif
#ifndef D_HTTP_COLOR_BUTTON
#define D_HTTP_COLOR_BUTTON             "#1fa3ec"    // Button color - Vivid blue
#endif
#ifndef D_HTTP_COLOR_BUTTON_RESET
#define D_HTTP_COLOR_BUTTON_RESET       "#f00"       // Restart/Reset button color - red
#endif
/* clang-format on */

#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
File fsUploadFile;
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
bool webServerStarted = false;

#if(HASP_USE_CAPTIVE_PORTAL > 0) && (HASP_USE_WIFI > 0)
DNSServer dnsServer;
IPAddress apIP(192, 168, 4, 1);
#ifndef DNS_PORT
#define DNS_PORT 53
#endif // DNS_PORT
#endif // HASP_USE_CAPTIVE_PORTAL

// bool httpEnable       = true;
// uint16_t httpPort     = 80;
// char httpUser[32]     = "";
// char httpPassword[32] = "";
hasp_http_config_t http_config;

#define HTTP_PAGE_SIZE (6 * 256)

#if(defined(STM32F4xx) || defined(STM32F7xx)) && HASP_USE_ETHERNET > 0
#include <EthernetWebServer_STM32.h>
EthernetWebServer webServer(80);
#endif

#if defined(STM32F4xx) && HASP_USE_WIFI > 0
#include <EthernetWebServer_STM32.h>
// #include <WiFi.h>
EthernetWebServer webServer(80);
#endif

#if defined(ARDUINO_ARCH_ESP8266)
#include "StringStream.h"
#include <ESP8266WebServer.h>
#include <detail/mimetable.h>
ESP8266WebServer webServer(80);
#endif

#if defined(ARDUINO_ARCH_ESP32)
#include <WebServer.h>
#include <detail/mimetable.h>
WebServer webServer(80);
extern const uint8_t EDIT_HTM_GZ_START[] asm("_binary_data_edit_htm_gz_start");
extern const uint8_t EDIT_HTM_GZ_END[] asm("_binary_data_edit_htm_gz_end");
#endif // ESP32

HTTPUpload* upload;

static const char HTTP_MENU_BUTTON[] PROGMEM =
    "<p><form method='GET' action='%s'><button type='submit'>%s</button></form></p>";

const char MAIN_MENU_BUTTON[] PROGMEM =
    "</p><p><form method='GET' action='/'><button type='submit'>" D_HTTP_MAIN_MENU "</button></form>";
const char MIT_LICENSE[] PROGMEM = "</br>MIT License</p>";

const char HTTP_DOCTYPE[] PROGMEM = "<!DOCTYPE html><html lang=\"en\"><head><meta charset='utf-8'><meta "
                                    "name=\"viewport\" content=\"width=device-width,initial-scale=1\"/>";
const char HTTP_META_GO_BACK[] PROGMEM = "<meta http-equiv='refresh' content='15;url=/'/>";
const char HTTP_HEADER[] PROGMEM       = "<title>%s</title>";
const char HTTP_HEADER_END[] PROGMEM =
    "<script src=\"/js\"></script><link rel=\"stylesheet\" href=\"/css\"></head><body><div id='doc'>";
const char HTTP_FOOTER[] PROGMEM =
    "<div style='text-align:right;font-size:11px;'><hr/><a href='/about'>" D_MANUFACTURER " ";
const char HTTP_END[] PROGMEM = " " D_HTTP_FOOTER "</div></body></html>";

////////////////////////////////////////////////////////////////////////////////////////////////////

// URL for auto-update "version.json"
// const char UPDATE_URL[] PROGMEM = "http://haswitchplate.com/update/version.json";
// // Default link to compiled Arduino firmware image
// String espFirmwareUrl = "http://haswitchplate.com/update/HASwitchPlate.ino.d1_mini.bin";
// // Default link to compiled Nextion firmware images
// String lcdFirmwareUrl = "http://haswitchplate.com/update/HASwitchPlate.tft";

////////////////////////////////////////////////////////////////////////////////////////////////////
String getOption(int value, String label, int current_value)
{
    char buffer[128];
    snprintf_P(buffer, sizeof(buffer), PSTR("<option value='%d'%s>%s</option>"), value,
               (value == current_value ? PSTR(" selected") : ""), label.c_str());
    return buffer;
}

String getOption(String& value, String& label, String& current_value)
{
    char buffer[128];
    snprintf_P(buffer, sizeof(buffer), PSTR("<option value='%s'%s>%s</option>"), value.c_str(),
               (value == current_value ? PSTR(" selected") : ""), label.c_str());
    return buffer;
}

static void add_button(String& str, const __FlashStringHelper* label, const __FlashStringHelper* extra)
{
    str += F("<button type='submit' ");
    str += extra;
    str += F(">");
    str += label;
    str += F("</button>");
}

static void close_form(String& str)
{
    str += F("</form></p>");
}

static void add_form_button(String& str, const __FlashStringHelper* label, const __FlashStringHelper* action,
                            const __FlashStringHelper* extra)
{
    str += F("<p><form method='GET' action='");
    str += action;
    str += F("'>");
    add_button(str, label, extra);
    close_form(str);
}

static String getContentType(const String& path)
{
    char buff[sizeof(mime::mimeTable[0].mimeType)];
    // Check all entries but last one for match, return if found
    for(size_t i = 0; i < sizeof(mime::mimeTable) / sizeof(mime::mimeTable[0]) - 1; i++) {
        strcpy_P(buff, mime::mimeTable[i].endsWith);
        if(path.endsWith(buff)) {
            strcpy_P(buff, mime::mimeTable[i].mimeType);
            return String(buff);
        }
    }
    // Fall-through and just return default type
    strcpy_P(buff, mime::mimeTable[sizeof(mime::mimeTable) / sizeof(mime::mimeTable[0]) - 1].mimeType);
    return String(buff);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void webHandleHaspConfig();

////////////////////////////////////////////////////////////////////////////////////////////////////

bool httpIsAuthenticated()
{
    if(http_config.password[0] != '\0') { // Request HTTP auth if httpPassword is set
        if(!webServer.authenticate(http_config.user, http_config.password)) {
            webServer.requestAuthentication();
            return false;
        }
    }
    return true;
}

bool httpIsAuthenticated(const __FlashStringHelper* notused)
{
    if(!httpIsAuthenticated()) return false;

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    LOG_TRACE(TAG_HTTP, F(D_HTTP_SENDING_PAGE), webServer.uri().c_str(),
              webServer.client().remoteIP().toString().c_str());
#else
        // LOG_INFO(TAG_HTTP,F(D_HTTP_SENDING_PAGE), page,
        //             String(webServer.client().remoteIP()).c_str());
#endif

    return true;
}

void webSendFooter()
{
#if defined(STM32F4xx)
    webServer.sendContent(HTTP_FOOTER);
    webServer.sendContent(haspDevice.get_version());
    webServer.sendContent(HTTP_END);
#else
    webServer.sendContent_P(HTTP_FOOTER);
    webServer.sendContent(haspDevice.get_version());
    webServer.sendContent_P(HTTP_END);
#endif
}

static int webSendCached(int statuscode, const char* contenttype, const char* data, size_t size)
{
    webServer.sendHeader(F("Cache-Control"), F("public, max-age=604800, immutable"));
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    webServer.send_P(statuscode, contenttype, data, size);
#else
    webServer.send(statuscode, contenttype, data);
#endif
    return statuscode;
}

void webSendPage(const char* nodename, uint32_t httpdatalength, bool gohome = false)
{
    {
        char buffer[64];

        /* Calculate Content Length upfront */
        uint32_t contentLength = strlen(haspDevice.get_version()); // version length
        contentLength += sizeof(HTTP_DOCTYPE) - 1;
        contentLength += sizeof(HTTP_HEADER) - 1 - 2 + strlen(nodename); // -2 for %s
                                                                         //    contentLength += sizeof(HTTP_SCRIPT) - 1;
        if(gohome) contentLength += sizeof(HTTP_META_GO_BACK) - 1;
        contentLength += sizeof(HTTP_HEADER_END) - 1;
        contentLength += sizeof(HTTP_FOOTER) - 1;
        contentLength += sizeof(HTTP_END) - 1;

        if(httpdatalength > HTTP_PAGE_SIZE) {
            LOG_WARNING(TAG_HTTP, F("Sending page with %u static and %u dynamic bytes"), contentLength, httpdatalength);
        }

        webServer.setContentLength(contentLength + httpdatalength);
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
        webServer.send_P(200, PSTR("text/html"), HTTP_DOCTYPE); // 122
#else
        webServer.send(200, ("text/html"), HTTP_DOCTYPE); // 122
#endif

        snprintf_P(buffer, sizeof(buffer), HTTP_HEADER, nodename);
        webServer.sendContent(buffer); // 17-2+len
    }

#if defined(STM32F4xx)
    // webServer.sendContent(HTTP_SCRIPT); // 131
    // webServer.sendContent(HTTP_STYLE); // 487
    // webServer.sendContent(HASP_STYLE);                   // 145
    if(gohome) webServer.sendContent(HTTP_META_GO_BACK); // 47
    webServer.sendContent(HTTP_HEADER_END);              // 80
#else
    // webServer.sendContent_P(HTTP_SCRIPT);                 // 131
    // webServer.sendContent_P(HTTP_STYLE); // 487
    // webServer.sendContent_P(HASP_STYLE);                   // 145
    if(gohome) webServer.sendContent_P(HTTP_META_GO_BACK); // 47
    webServer.sendContent_P(HTTP_HEADER_END);              // 80
#endif
}

void saveConfig()
{
    if(webServer.method() == HTTP_POST) {
        if(webServer.hasArg(PSTR("save"))) {
            String save = webServer.arg(PSTR("save"));

            StaticJsonDocument<256> settings;
            for(int i = 0; i < webServer.args(); i++) settings[webServer.argName(i)] = webServer.arg(i);

            if(save == String(PSTR("hasp"))) {
                haspSetConfig(settings.as<JsonObject>());

#if HASP_USE_MQTT > 0
            } else if(save == String(PSTR("mqtt"))) {
                mqttSetConfig(settings.as<JsonObject>());
#endif

            } else if(save == String(PSTR("gui"))) {
                settings[FPSTR(FP_GUI_POINTER)] = webServer.hasArg(PSTR("cur"));
                settings[FPSTR(FP_GUI_INVERT)]  = webServer.hasArg(PSTR("inv"));
                guiSetConfig(settings.as<JsonObject>());

            } else if(save == String(PSTR("debug"))) {
                debugSetConfig(settings.as<JsonObject>());

            } else if(save == String(PSTR("http"))) {
                httpSetConfig(settings.as<JsonObject>());

                // Password might have changed
                if(!httpIsAuthenticated(F("config"))) return;

#if HASP_USE_WIFI > 0
            } else if(save == String(PSTR("wifi"))) {
                wifiSetConfig(settings.as<JsonObject>());
#endif
            }
        }
    }
}

void webHandleRoot()
{
    if(!httpIsAuthenticated(F("root"))) return;

    saveConfig();
    {
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        httpMessage += F("<p><form method='GET' action='/config/hasp'><button type='submit'>" D_HTTP_HASP_DESIGN
                         "</button></form></p>");

        httpMessage += F("<p><form method='GET' action='screenshot'><button type='submit'>" D_HTTP_SCREENSHOT
                         "</button></form></p>");
        httpMessage +=
            F("<p><form method='GET' action='info'><button type='submit'>" D_HTTP_INFORMATION "</button></form></p>");
        add_form_button(httpMessage, F(D_HTTP_CONFIGURATION), F("/config"), F(""));
        // httpMessage += F("<p><form method='GET' action='config'><button type='submit'>" D_HTTP_CONFIGURATION
        //                  "</button></form></p>");

        httpMessage += F("<p><form method='GET' action='firmware'><button type='submit'>" D_HTTP_FIRMWARE_UPGRADE
                         "</button></form></p>");

#if HASP_USE_SPIFFS > 0 || HASP_USE_LITTLEFS > 0
#ifdef ARDUINO_ARCH_ESP32
        bool flashfile = true;
#else
        bool flashfile = false;
#endif
        if(flashfile || HASP_FS.exists(F("/edit.htm.gz")) || HASP_FS.exists(F("/edit.htm"))) {
            httpMessage += F("<p><form method='GET' action='edit.htm?file=/'><button type='submit'>" D_HTTP_FILE_BROWSER
                             "</button></form></p>");
        }
#endif

        httpMessage += F("<p><form method='GET' action='reboot'><button class='red' type='submit'>" D_HTTP_REBOOT
                         "</button></form></p>");

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void httpHandleReboot()
{ // http://plate01/reboot
    if(!httpIsAuthenticated(F("reboot"))) return;

    {
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");
        httpMessage = F(D_DISPATCH_REBOOT);

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), true);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();

    delay(200);
    dispatch_reboot(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleScreenshot()
{ // http://plate01/screenshot
    if(!httpIsAuthenticated(F("screenshot"))) return;

    if(webServer.hasArg(F("a"))) {
        if(webServer.arg(F("a")) == F("next")) {
            dispatch_page_next(LV_SCR_LOAD_ANIM_NONE);
        } else if(webServer.arg(F("a")) == F("prev")) {
            dispatch_page_prev(LV_SCR_LOAD_ANIM_NONE);
        } else if(webServer.arg(F("a")) == F("back")) {
            dispatch_page_back(LV_SCR_LOAD_ANIM_NONE);
        }
    }

    if(webServer.hasArg(F("q"))) {
        lv_disp_t* disp = lv_disp_get_default();
        webServer.setContentLength(122 + disp->driver.hor_res * disp->driver.ver_res * sizeof(lv_color_t));
        webServer.send_P(200, PSTR("image/bmp"), "");
        guiTakeScreenshot();
        webServer.client().stop();

    } else {
        {
            String httpMessage((char*)0);
            httpMessage.reserve(HTTP_PAGE_SIZE);
            httpMessage += F("<h1>");
            httpMessage += haspDevice.get_hostname();
            httpMessage += F("</h1><hr>");

            // httpMessage +=
            //     F("<script>function aref(t){setTimeout(function() {ref('');}, t*1000)} function ref(a){ var t=new "
            //       "Date().getTime();document.getElementById('bmp').src='?a='+a+'&q='+t;return false;}</script>");
            httpMessage += F("<p class='c'><img id='bmp' src='?q=0'");

            // Automatic refresh
            httpMessage += F(" onload=\"aref(5)\" onerror=\"aref(5)\"/></p>");

            httpMessage += F("<p><form method='GET' onsubmit=\"return ref('')\"><button type='submit'>" D_HTTP_REFRESH
                             "</button></form></p>");
            httpMessage +=
                F("<p><form method='GET' onsubmit=\"return ref('prev');\"><button type='submit'>" D_HTTP_PREV_PAGE
                  "</button></form></p>");
            httpMessage +=
                F("<p><form method='GET' onsubmit=\"return ref('next');\"><button type='submit'>" D_HTTP_NEXT_PAGE
                  "</button></form></p>");
            httpMessage += FPSTR(MAIN_MENU_BUTTON);

            webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
            webServer.sendContent(httpMessage);
        }
        // httpMessage.clear();
        webSendFooter();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void webHandleAbout()
{ // http://plate01/about
    if(!httpIsAuthenticated(F("about"))) return;

    {
        String mitLicense((char*)0);
        mitLicense = FPSTR(MIT_LICENSE);

        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);

        httpMessage += F("<p><h3>openHASP</h3>Copyright&copy; 2019-2021 Francis Van Roie ");
        httpMessage += mitLicense;
        httpMessage += F("<p>Based on the previous work of the following open source developers.</p><hr>");
        httpMessage += F("<p><h3>HASwitchPlate</h3>Copyright&copy; 2019 Allen Derusha allen@derusha.org</b>");
        httpMessage += mitLicense;
        httpMessage += F("<p><h3>LVGL</h3>Copyright&copy; 2021 LVGL Kft");
        httpMessage += mitLicense;
        httpMessage += F("<p><h3>zi Font Engine</h3>Copyright&copy; 2020-2021 Francis Van Roie");
        httpMessage += mitLicense;
        httpMessage += F("<p><h3>TFT_eSPI Library</h3>Copyright&copy; 2020 Bodmer (https://github.com/Bodmer) All "
                         "rights reserved.</br>FreeBSD License</p>");
        httpMessage +=
            F("<p><i>includes parts from the <b>Adafruit_GFX library</b></br>Copyright&copy; 2012 Adafruit Industries. "
              "All rights reserved</br>BSD License</i></p>");
        httpMessage += F("<p><h3>ArduinoJson</h3>Copyright&copy; 2014-2021 Benoit BLANCHON");
        httpMessage += mitLicense;
        httpMessage += F("<p><h3>PubSubClient</h3>Copyright&copy; 2008-2015 Nicholas O'Leary");
        httpMessage += mitLicense;
        httpMessage +=
            F("<p><h3>ArduinoLog</h3>Copyright&copy; 2017,2018 Thijs Elenbaas, MrRobot62, rahuldeo2047, NOX73, "
              "dhylands, Josha blemasle, mfalkvidd");
        httpMessage += mitLicense;
#if HASP_USE_SYSLOG > 0
        // Replaced with WiFiUDP client
        // httpMessage += F("<p><h3>Syslog</h3>Copyright&copy; 2016 Martin Sloup");
        // httpMessage += mitLicense;
#endif
#if HASP_USE_QRCODE > 0
        httpMessage += F("<p><h3>QR Code generator</h3>Copyright&copy; Project Nayuki");
        httpMessage += mitLicense;
#endif
        httpMessage += F("<p><h3>AceButton</h3>Copyright&copy; 2018 Brian T. Park");
        httpMessage += mitLicense;

        httpMessage += FPSTR(MAIN_MENU_BUTTON);

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void add_json(String& data, JsonDocument& doc)
{
    char buffer[512];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if(doc.isNull()) return; // empty document

    buffer[len - 1] = ',';
    char* start     = buffer + 1;
    data += String(start);
    doc.clear();
}

void webHandleInfoJson()
{ // http://plate01/
    if(!httpIsAuthenticated(F("infojson"))) return;

    String htmldata((char*)0);
    htmldata.reserve(HTTP_PAGE_SIZE);
    DynamicJsonDocument doc(512);

    htmldata += F("<h1>");
    htmldata += haspDevice.get_hostname();
    htmldata += F("</h1><hr>");

    htmldata += "<div id=\"info\"></div><script>window.addEventListener(\"load\", function(){ var data = '{";
    //  htmldata = "{";

    hasp_get_info(doc);
    add_json(htmldata, doc);

#if HASP_USE_MQTT > 0
    mqtt_get_info(doc);
    add_json(htmldata, doc);
#endif

    network_get_info(doc);
    add_json(htmldata, doc);

    haspDevice.get_info(doc);
    add_json(htmldata, doc);

    htmldata[htmldata.length() - 1] = '}'; // Replace last comma with a bracket

    htmldata += "'; data = JSON.parse(data); var table = \"<table>\"; for(let header in data) { "
                "table += `<tr><td colspan=2></td></ tr><tr><th colspan=2>${header}</ th></ tr>`;"
                "for(let key in data[header]) { "
                "table += `<tr><td>${key}: </ td><td> ${data[header][key]}</ td></ tr>`;"
                "}} table += \"</table>\"; "
                "document.getElementById(\"info\").innerHTML = table;});</script>";

    // String path = F(".html");
    // webServer.send(200, getContentType(path), htmldata);

    htmldata += FPSTR(MAIN_MENU_BUTTON);

    webSendPage(haspDevice.get_hostname(), htmldata.length(), false);
    webServer.sendContent(htmldata);

    // htmldata.clear();
    webSendFooter();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleInfo()
{ // http://plate01/
    if(!httpIsAuthenticated(F("info"))) return;

    {
        char size_buf[32];
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        /* HASP Stats */
        httpMessage += F("<b>HASP Version: </b>");
        httpMessage += haspDevice.get_version();
        httpMessage += F("<br/><b>Build DateTime: </b>");
        httpMessage += __DATE__;
        httpMessage += F(" ");
        httpMessage += __TIME__;
        httpMessage += F(" UTC<br/><b>Uptime: </b>"); // Github buildservers are in UTC

        unsigned long time = millis() / 1000;
        uint16_t day       = time / 86400;
        time               = time % 86400;
        uint8_t hour       = time / 3600;
        time               = time % 3600;
        uint8_t min        = time / 60;
        time               = time % 60;
        uint8_t sec        = time;

        if(day > 0) {
            httpMessage += String(day);
            httpMessage += F("d ");
        }
        if(day > 0 || hour > 0) {
            httpMessage += String(hour);
            httpMessage += F("h ");
        }
        if(day > 0 || hour > 0 || min > 0) {
            httpMessage += String(min);
            httpMessage += F("m ");
        }
        httpMessage += String(sec);
        httpMessage += F("s");

        httpMessage += F("<br/><b>Free Memory: </b>");
        Parser::format_bytes(haspDevice.get_free_heap(), size_buf, sizeof(size_buf));
        httpMessage += size_buf;
        httpMessage += F("<br/><b>Memory Fragmentation: </b>");
        httpMessage += String(haspDevice.get_heap_fragmentation());

#if ARDUINO_ARCH_ESP32
        if(psramFound()) {
            httpMessage += F("<br/><b>Free PSRam: </b>");
            Parser::format_bytes(ESP.getFreePsram(), size_buf, sizeof(size_buf));
            httpMessage += size_buf;
            httpMessage += F("<br/><b>PSRam Size: </b>");
            Parser::format_bytes(ESP.getPsramSize(), size_buf, sizeof(size_buf));
            httpMessage += size_buf;
        }
#endif

        /* LVGL Stats */
        lv_mem_monitor_t mem_mon;
        lv_mem_monitor(&mem_mon);
        httpMessage += F("</p><p><b>LVGL Memory: </b>");
        Parser::format_bytes(mem_mon.total_size, size_buf, sizeof(size_buf));
        httpMessage += size_buf;
        httpMessage += F("<br/><b>LVGL Free: </b>");
        Parser::format_bytes(mem_mon.free_size, size_buf, sizeof(size_buf));
        httpMessage += size_buf;
        httpMessage += F("<br/><b>LVGL Fragmentation: </b>");
        httpMessage += mem_mon.frag_pct;

        // httpMessage += F("<br/><b>LCD Model: </b>")) + String(LV_HASP_HOR_RES_MAX) + " x " +
        // String(LV_HASP_VER_RES_MAX); httpMessage += F("<br/><b>LCD Version: </b>")) +
        // String(lcdVersion);
        httpMessage += F("</p/><p><b>LCD Active Page: </b>");
        httpMessage += String(haspPages.get());

        /* Wifi Stats */
#if HASP_USE_WIFI > 0
        httpMessage += F("</p/><p><b>SSID: </b>");
        httpMessage += String(WiFi.SSID());
        httpMessage += F("</br><b>Signal Strength: </b>");

        int8_t rssi = WiFi.RSSI();
        httpMessage += String(rssi);
        httpMessage += F("dBm (");

        if(rssi >= -50) {
            httpMessage += F("Excellent)");
        } else if(rssi >= -60) {
            httpMessage += F("Good)");
        } else if(rssi >= -70) {
            httpMessage += F("Fair)");
        } else if(rssi >= -80) {
            httpMessage += F("Weak)");
        } else {
            httpMessage += F("Very Bad)");
        }
#if defined(STM32F4xx)
        byte mac[6];
        WiFi.macAddress(mac);
        char macAddress[16];
        snprintf_P(macAddress, sizeof(macAddress), PSTR("%02x%02x%02x"), mac[0], mac[1], mac[2], mac[3], mac[4],
                   mac[5]);
        httpMessage += F("</br><b>IP Address: </b>");
        httpMessage += String(WiFi.localIP());
        httpMessage += F("</br><b>Gateway: </b>");
        httpMessage += String(WiFi.gatewayIP());
        httpMessage += F("</br><b>MAC Address: </b>");
        httpMessage += String(macAddress);
#else
        httpMessage += F("</br><b>IP Address: </b>");
        httpMessage += String(WiFi.localIP().toString());
        httpMessage += F("</br><b>Gateway: </b>");
        httpMessage += String(WiFi.gatewayIP().toString());
        httpMessage += F("</br><b>DNS Server: </b>");
        httpMessage += String(WiFi.dnsIP().toString());
        httpMessage += F("</br><b>MAC Address: </b>");
        httpMessage += String(WiFi.macAddress());
#endif
#endif
#if HASP_USE_ETHERNET > 0
#if defined(ARDUINO_ARCH_ESP32)
        httpMessage += F("</p/><p><b>Ethernet: </b>");
        httpMessage += String(ETH.linkSpeed());
        httpMessage += F(" Mbps");
        if(ETH.fullDuplex()) {
            httpMessage += F(" " D_INFO_FULL_DUPLEX);
        }
        httpMessage += F("</br><b>IP Address: </b>");
        httpMessage += String(ETH.localIP().toString());
        httpMessage += F("</br><b>Gateway: </b>");
        httpMessage += String(ETH.gatewayIP().toString());
        httpMessage += F("</br><b>DNS Server: </b>");
        httpMessage += String(ETH.dnsIP().toString());
        httpMessage += F("</br><b>MAC Address: </b>");
        httpMessage += String(ETH.macAddress());
#endif
#endif
/* Mqtt Stats */
#if HASP_USE_MQTT > 0
        httpMessage += F("</p/><p><b>MQTT Status: </b>");
        if(mqttIsConnected()) { // Check MQTT connection
            httpMessage += F("Connected");
        } else {
            httpMessage += F("<font color='red'><b>Disconnected</b></font>, return code: ");
            //     +String(mqttClient.returnCode());
        }
        httpMessage += F("<br/><b>MQTT ClientID: </b>");

        {
            char mqttClientId[64];
            String mac = halGetMacAddress(3, "");
            mac.toLowerCase();
            snprintf_P(mqttClientId, sizeof(mqttClientId), PSTR("%s-%s"), haspDevice.get_hostname(), mac.c_str());
            httpMessage += mqttClientId;
        }

#endif // MQTT

        /* ESP Stats */
        httpMessage += F("</p/><p><b>MCU Model: </b>");
        httpMessage += haspDevice.get_chip_model();
        httpMessage += F("<br/><b>CPU Frequency: </b>");
        httpMessage += String(haspDevice.get_cpu_frequency());
        httpMessage += F("MHz");

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
        httpMessage += F("<br/><b>Flash Chip Size: </b>");
        Parser::format_bytes(ESP.getFlashChipSize(), size_buf, sizeof(size_buf));
        httpMessage += size_buf;

        httpMessage += F("</br><b>Program Size Used: </b>");
        Parser::format_bytes(ESP.getSketchSize(), size_buf, sizeof(size_buf));
        httpMessage += size_buf;

        httpMessage += F("<br/><b>Program Size Free: </b>");
        Parser::format_bytes(ESP.getFreeSketchSpace(), size_buf, sizeof(size_buf));
        httpMessage += size_buf;
#endif

        //#if defined(ARDUINO_ARCH_ESP32)
        //        httpMessage += F("<br/><b>ESP SDK version: </b>");
        //        httpMessage += String(ESP.getSdkVersion());
        //#else
        httpMessage += F("<br/><b>Core version: </b>");
        httpMessage += haspDevice.get_core_version();
        //#endif
        httpMessage += F("<br/><b>Last Reset: </b>");
        //  httpMessage += halGetResetInfo();

        httpMessage += FPSTR(MAIN_MENU_BUTTON);

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();
}

/* String urldecode(String str)
{
    String encodedString = "";
    char c;
    for(unsigned int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if(c == '+') {
            encodedString += ' ';
        } else if(c == '%') {
            // char buffer[3];
            char buffer[128];
            i++;
            buffer[0] = str.charAt(i);
            i++;
            buffer[1] = str.charAt(i);
            buffer[2] = '\0';
            c         = (char)strtol((const char *)&buffer, NULL, 16);
            encodedString += c;
        } else {
            encodedString += c;
        }
        yield();
    }
    return encodedString;
} */

static unsigned long htppLastLoopTime = 0;
void webUploadProgress()
{
    long t = webServer.header("Content-Length").toInt();
    if(millis() - htppLastLoopTime >= 1250) {
        LOG_VERBOSE(TAG_HTTP, F(D_BULLET "Uploaded %u / %d bytes"), upload->totalSize + upload->currentSize, t);
        htppLastLoopTime = millis();
    }
    if(t > 0) t = (upload->totalSize + upload->currentSize) * 100 / t;
    haspProgressVal(t);
}

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
static inline void webUpdatePrintError()
{
#if defined(ARDUINO_ARCH_ESP8266)
    String output((char*)0);
    output.reserve(128);
    StringStream stream((String&)output);
    Update.printError(stream); // ESP8266 only has printError()
    LOG_ERROR(TAG_HTTP, output.c_str());
    haspProgressMsg(output.c_str());
#elif defined(ARDUINO_ARCH_ESP32)
    LOG_ERROR(TAG_HTTP, Update.errorString()); // ESP32 has errorString()
    haspProgressMsg(Update.errorString());
#endif
}

void webUpdateReboot()
{
    LOG_INFO(TAG_HTTP, F("Update Success: %u bytes received. Rebooting..."), upload->totalSize);

    {
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");
        httpMessage += F("<b>Upload complete. Rebooting device, please wait...</b>");

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), true);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();

    delay(250);
    dispatch_reboot(true); // Save the current config
}

void webHandleFirmwareUpload()
{
    upload = &webServer.upload();

    switch(upload->status) {

        case UPLOAD_FILE_START: {
            if(!httpIsAuthenticated(F("update"))) return;

            // WiFiUDP::stopAll();

            int command = webServer.arg(F("cmd")).toInt();
            size_t size = 0;
            if(command == U_FLASH) {
                LOG_TRACE(TAG_HTTP, F("Update flash: %s"), upload->filename.c_str());
                size = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
#ifdef ESP32
            } else if(command == U_SPIFFS) {
                LOG_TRACE(TAG_HTTP, F("Update filesystem: %s"), upload->filename.c_str());
                size = UPDATE_SIZE_UNKNOWN;
#endif
            }
            haspProgressMsg(upload->filename.c_str());

            // if(!Update.begin(UPDATE_SIZE_UNKNOWN)) { // start with max available size
            //  const char label[] = "spiffs";
            if(!Update.begin(size, command, -1, 0U)) { // start with max available size
                webUpdatePrintError();
            }
            break;
        }

        case UPLOAD_FILE_WRITE: // flashing firmware to ESP
            if(Update.write(upload->buf, upload->currentSize) != upload->currentSize) {
                webUpdatePrintError();
            } else {
                webUploadProgress();
            }
            break;

        case UPLOAD_FILE_END:
            haspProgressVal(100);
            if(Update.end(true)) { // true to set the size to the current progress
                haspProgressMsg(F(D_OTA_UPDATE_APPLY));
                webUpdateReboot();
            } else {
                webUpdatePrintError();
            }
            break;

        default:;
    }
}
#endif

#if HASP_USE_SPIFFS > 0 || HASP_USE_LITTLEFS > 0
int handleFileRead(String path)
{
    // if(!httpIsAuthenticated(F("fileread"))) return false;
    if(!httpIsAuthenticated()) return false;

    path = webServer.urlDecode(path).substring(0, 31);
    if(path.endsWith("/")) {
        path += F("index.htm");
    }

    String pathWithGz = path + F(".gz");
    if(HASP_FS.exists(pathWithGz) || HASP_FS.exists(path)) {

        String contentType((char*)0);
        if(webServer.hasArg(F("download")))
            contentType = F("application/octet-stream");
        else
            contentType = getContentType(path);

        if(!HASP_FS.exists(path) && HASP_FS.exists(pathWithGz))
            path = pathWithGz; // Only use .gz if normal file doesn't exist
        File file = HASP_FS.open(path, "r");

        String configFile((char*)0); // Verify if the file is config.json
        configFile = String(FPSTR(FP_HASP_CONFIG_FILE));

        if(!strncasecmp(file.name(), configFile.c_str(), configFile.length())) {
            file.close();
            DynamicJsonDocument settings(8 * 256);
            DeserializationError error = configParseFile(configFile, settings);

            if(error) return 500; // Internal Server Error

            configMaskPasswords(settings); // Output settings to the client with masked passwords!
            char buffer[1024];
            size_t len = serializeJson(settings, buffer, sizeof(buffer));
            webServer.setContentLength(len);
            webServer.send(200, contentType, buffer);

        } else {

            // Stream other files directly from filesystem
            webServer.streamFile(file, contentType);
            file.close();
        }

        return 200; // OK
    }

#ifdef ARDUINO_ARCH_ESP32
    if(path == F("/edit.htm")) {
        size_t size = EDIT_HTM_GZ_END - EDIT_HTM_GZ_START;
        webServer.sendHeader(F("Content-Encoding"), F("gzip"));
        return webSendCached(200, PSTR("text/html"), (const char*)EDIT_HTM_GZ_START, size); // OK
    }
#endif

    if(!strcasecmp_P(path.c_str(), PSTR("/favicon.ico")))
        return webSendCached(204, PSTR("image/bmp"), "", 0); // No content

    return 404; // Not found
}

void handleFileUpload()
{
    if(webServer.uri() != "/edit") {
        return;
    }
    upload = &webServer.upload();
    if(upload->status == UPLOAD_FILE_START) {
        if(!httpIsAuthenticated(F("fileupload"))) return;
        LOG_INFO(TAG_HTTP, F("Total size: %s"), webServer.headerName(0).c_str());
        String filename((char*)0);
        filename.reserve(64);
        filename = upload->filename;
        if(!filename.startsWith("/")) {
            filename = "/";
            filename += upload->filename;
        }
        if(filename.length() < 32) {
            fsUploadFile = HASP_FS.open(filename, "w");
            LOG_TRACE(TAG_HTTP, F("handleFileUpload Name: %s"), filename.c_str());
            haspProgressMsg(fsUploadFile.name());
        } else {
            LOG_ERROR(TAG_HTTP, F("Filename %s is too long"), filename.c_str());
        }
    } else if(upload->status == UPLOAD_FILE_WRITE) {
        // DBG_OUTPUT_PORT.print("handleFileUpload Data: "); debugPrintln(upload.currentSize);
        if(fsUploadFile) {
            if(fsUploadFile.write(upload->buf, upload->currentSize) != upload->currentSize) {
                LOG_ERROR(TAG_HTTP, F("Failed to write received data to file"));
            } else {
                webUploadProgress(); // Moved to httpEverySecond Loop
            }
        }
    } else if(upload->status == UPLOAD_FILE_END) {
        if(fsUploadFile) {
            LOG_INFO(TAG_HTTP, F("Uploaded %s (%u bytes)"), fsUploadFile.name(), upload->totalSize);
            fsUploadFile.close();
        }
        haspProgressVal(255);

        // Redirect to /config/hasp page. This flushes the web buffer and frees the memory
        webServer.sendHeader(String(F("Location")), String(F("/config/hasp")), true);
        webServer.send_P(302, PSTR("text/plain"), "");
        // httpReconnect();
    }
}

void handleFileDelete()
{
    if(!httpIsAuthenticated(F("filedelete"))) return;

    char mimetype[16];
    snprintf_P(mimetype, sizeof(mimetype), PSTR("text/plain"));

    if(webServer.args() == 0) {
        return webServer.send_P(500, mimetype, PSTR("BAD ARGS"));
    }
    String path = webServer.arg(0);
    LOG_TRACE(TAG_HTTP, F("handleFileDelete: %s"), path.c_str());
    if(path == "/") {
        return webServer.send_P(500, mimetype, PSTR("BAD PATH"));
    }
    if(!HASP_FS.exists(path)) {
        return webServer.send_P(404, mimetype, PSTR("FileNotFound"));
    }
    HASP_FS.remove(path);
    webServer.send_P(200, mimetype, PSTR(""));
    // path.clear();
}

void handleFileCreate()
{
    if(!httpIsAuthenticated(F("filecreate"))) return;

    if(webServer.args() == 0) {
        return webServer.send(500, PSTR("text/plain"), PSTR("BAD ARGS"));
    }

    if(webServer.hasArg(F("path"))) {
        String path = webServer.arg(F("path"));
        LOG_TRACE(TAG_HTTP, F("handleFileCreate: %s"), path.c_str());
        if(path == "/") {
            return webServer.send(500, PSTR("text/plain"), PSTR("BAD PATH"));
        }
        if(HASP_FS.exists(path)) {
            return webServer.send(500, PSTR("text/plain"), PSTR("FILE EXISTS"));
        }
        File file = HASP_FS.open(path, "w");
        if(file) {
            file.close();
        } else {
            return webServer.send(500, PSTR("text/plain"), PSTR("CREATE FAILED"));
        }
    }
    if(webServer.hasArg(F("init"))) {
        dispatch_idle(NULL, "0", TAG_HTTP);
        hasp_init();
    }
    if(webServer.hasArg(F("load"))) {
        dispatch_idle(NULL, "0", TAG_HTTP);
        hasp_load_json();
    }
    if(webServer.hasArg(F("page"))) {
        uint8_t pageid = atoi(webServer.arg(F("page")).c_str());
        dispatch_idle(NULL, "0", TAG_HTTP);
        dispatch_set_page(pageid, LV_SCR_LOAD_ANIM_NONE);
    }
    webServer.send(200, PSTR("text/plain"), "");
}

void handleFileList()
{
    if(!httpIsAuthenticated(F("filelist"))) return;

    if(!webServer.hasArg(F("dir"))) {
        webServer.send(500, PSTR("text/plain"), PSTR("BAD ARGS"));
        return;
    }

    String path = webServer.arg(F("dir"));
    //  LOG_TRACE(TAG_HTTP, F("handleFileList: %s"), path.c_str());
    path.clear();

#if defined(ARDUINO_ARCH_ESP32)
    File root = HASP_FS.open("/", FILE_READ);
    File file = root.openNextFile();
    String output((char*)0);
    output.reserve(HTTP_PAGE_SIZE);
    output = "[";

    while(file) {
        if(output != "[") {
            output += ',';
        }
        bool isDir = false;
        output += F("{\"type\":\"");
        output += (isDir) ? F("dir") : F("file");
        output += F("\",\"name\":\"");
        if(file.name()[0] == '/') {
            output += &(file.name()[1]);
        } else {
            output += file.name();
        }
        output += F("\"}");

        // file.close();
        file = root.openNextFile();
    }
    output += "]";
    webServer.send(200, PSTR("text/json"), output);
#elif defined(ARDUINO_ARCH_ESP8266)
    Dir dir = HASP_FS.openDir(path);
    String output((char*)0);
    output.reserve(HTTP_PAGE_SIZE);
    output = "[";

    while(dir.next()) {
        File entry = dir.openFile("r");
        if(output != "[") {
            output += ',';
        }
        bool isDir = false;
        output += F("{\"type\":\"");
        output += (isDir) ? F("dir") : F("file");
        output += F("\",\"name\":\"");
        if(entry.name()[0] == '/') {
            output += &(entry.name()[1]);
        } else {
            output += entry.name();
        }
        output += F("\"}");
        entry.close();
    }
    output += "]";
    webServer.send(200, PSTR("text/json"), output);
#endif
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
#if HASP_USE_CONFIG > 0
void webHandleConfig()
{ // http://plate01/config
    if(!httpIsAuthenticated(F("config"))) return;

    saveConfig();

// Reboot after saving wifi config in AP mode
#if HASP_USE_WIFI > 0 && !defined(STM32F4xx)
    if(WiFi.getMode() != WIFI_STA) {
        httpHandleReboot();
    }
#endif

    {
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

#if HASP_USE_WIFI > 0
        add_form_button(httpMessage, F(D_HTTP_WIFI_SETTINGS), F("/config/wifi"), F(""));
#endif
#if HASP_USE_MQTT > 0
        add_form_button(httpMessage, F(D_HTTP_MQTT_SETTINGS), F("/config/mqtt"), F(""));
#endif
        add_form_button(httpMessage, F(D_HTTP_HTTP_SETTINGS), F("/config/http"), F(""));
        add_form_button(httpMessage, F(D_HTTP_GUI_SETTINGS), F("/config/gui"), F(""));

        // httpMessage +=
        //     F("<p><form method='GET' action='/config/hasp'><button type='submit'>HASP
        //     Settings</button></form></p>");

#if HASP_USE_GPIO > 0
        httpMessage += F("<p><form method='GET' action='/config/gpio'><button type='submit'>" D_HTTP_GPIO_SETTINGS
                         "</button></form></p>");
#endif

        httpMessage += F("<p><form method='GET' action='/config/debug'><button type='submit'>" D_HTTP_DEBUG_SETTINGS
                         "</button></form></p>");

        httpMessage +=
            F("<p><form method='GET' action='resetConfig'><button class='red' type='submit'>" D_HTTP_FACTORY_RESET
              "</button></form>");

        httpMessage += FPSTR(MAIN_MENU_BUTTON);

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
#if HASP_USE_MQTT > 0
void webHandleMqttConfig()
{ // http://plate01/config/mqtt
    if(!httpIsAuthenticated(F("config/mqtt"))) return;

    StaticJsonDocument<256> settings;
    mqttGetConfig(settings.to<JsonObject>());

    {
        // char buffer[128];
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        httpMessage += F("<form method='POST' action='/config'>");
        httpMessage += F("<b>HASP Node Name</b> <i><small>(required. lowercase letters, numbers, and _ only)</small>"
                         "</i><input id='name' required name='name' maxlength=15 "
                         "placeholder='HASP Node Name' pattern='[a-z0-9_]*' value='");
        httpMessage += settings[FPSTR(FP_CONFIG_NAME)].as<String>();
        httpMessage += F("'><br/><br/><b>Group Name</b> <i><small>(required)</small></i><input id='group' required "
                         "name='group' maxlength=15 placeholder='Group Name' value='");
        httpMessage += settings[FPSTR(FP_CONFIG_GROUP)].as<String>();
        httpMessage += F("'><br/><br/><b>MQTT Broker</b> <i><small>(required)</small></i><input id='host' required "
                         "name='host' maxlength=63 placeholder='mqttServer' value='");
        httpMessage += settings[FPSTR(FP_CONFIG_HOST)].as<String>();
        httpMessage += F("'><br/><b>MQTT Port</b> <i><small>(required)</small></i><input id='port' required "
                         "name='port' type='number' maxlength=5 placeholder='mqttPort' value='");
        httpMessage += settings[FPSTR(FP_CONFIG_PORT)].as<uint16_t>();
        httpMessage += F("'><br/><b>MQTT User</b> <i><small>(optional)</small></i><input id='mqttUser' name='user' "
                         "maxlength=31 placeholder='user' value='");
        httpMessage += settings[FPSTR(FP_CONFIG_USER)].as<String>();
        httpMessage += F("'><br/><b>MQTT Password</b> <i><small>(optional)</small></i><input id='pass' "
                         "name='pass' type='password' maxlength=31 placeholder='mqttPassword' value='");
        if(settings[FPSTR(FP_CONFIG_PASS)].as<String>() != "") httpMessage += F(D_PASSWORD_MASK);

        httpMessage +=
            F("'><p><button type='submit' name='save' value='mqtt'>" D_HTTP_SAVE_SETTINGS "</button></form></p>");

        add_form_button(httpMessage, F(D_BACK_ICON D_HTTP_CONFIGURATION), F("/config"), F(""));
        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleGuiConfig()
{ // http://plate01/config/wifi
    if(!httpIsAuthenticated(F("config/gui"))) return;

    {
        StaticJsonDocument<256> settings;
        guiGetConfig(settings.to<JsonObject>());

        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        httpMessage += F("<form method='POST' action='/config'>");

        httpMessage += F("<p><b>Short Idle</b> <input id='idle1' required "
                         "name='idle1' type='number' min='0' max='32400' value='");
        httpMessage += settings[FPSTR(FP_GUI_IDLEPERIOD1)].as<String>();
        httpMessage += F("'></p>");

        httpMessage += F("<p><b>Long Idle</b> <input id='idle2' required "
                         "name='idle2' type='number' min='0' max='32400' value='");
        httpMessage += settings[FPSTR(FP_GUI_IDLEPERIOD2)].as<String>();
        httpMessage += F("'></p>");

        int8_t rotation = settings[FPSTR(FP_GUI_ROTATION)].as<int8_t>();
        httpMessage += F("<p><b>Orientation</b> <select id='rotate' name='rotate'>");
        httpMessage += getOption(0, F("0 degrees"), rotation);
        httpMessage += getOption(1, F("90 degrees"), rotation);
        httpMessage += getOption(2, F("180 degrees"), rotation);
        httpMessage += getOption(3, F("270 degrees"), rotation);
        httpMessage += getOption(6, F("0 degrees - mirrored"), rotation);
        httpMessage += getOption(7, F("90 degrees - mirrored"), rotation);
        httpMessage += getOption(4, F("180 degrees - mirrored"), rotation);
        httpMessage += getOption(5, F("270 degrees - mirrored"), rotation);
        httpMessage += F("</select></p>");

        httpMessage += F("<p><input id='inv' name='inv' type='checkbox' ");
        if(settings[FPSTR(FP_GUI_INVERT)].as<bool>()) httpMessage += F(" checked");
        httpMessage += F("><b>Invert Colors</b>");

        httpMessage += F("<p><input id='cur' name='cur' type='checkbox' ");
        if(settings[FPSTR(FP_GUI_POINTER)].as<bool>()) httpMessage += F(" checked");
        httpMessage += F("><b>Show Pointer</b>");

        int8_t bcklpin = settings[FPSTR(FP_GUI_BACKLIGHTPIN)].as<int8_t>();
        httpMessage += F("<p><b>Backlight Control</b> <select id='bckl' name='bckl'>");
        httpMessage += getOption(-1, F("None"), bcklpin);
#if defined(ARDUINO_ARCH_ESP32)
        char buffer[10];
        uint8_t pins[] = {5, 12, 13, 15, 16, 17, 18, 19, 21, 22, 23, 32};
        for(uint8_t i = 0; i < sizeof(pins); i++) {
            snprintf_P(buffer, sizeof(buffer), PSTR("GPIO %d"), pins[i]);
            httpMessage += getOption(pins[i], buffer, bcklpin);
        }
#else
        httpMessage += getOption(5, F("D1 - GPIO 5"), bcklpin);
        httpMessage += getOption(4, F("D2 - GPIO 4"), bcklpin);
        httpMessage += getOption(0, F("D3 - GPIO 0"), bcklpin);
        httpMessage += getOption(2, F("D4 - GPIO 2"), bcklpin);
#endif
        httpMessage += F("</select></p>");

        add_button(httpMessage, F(D_HTTP_SAVE_SETTINGS), F("name='save' value='gui'"));
        close_form(httpMessage);
        // httpMessage +=
        //     F("<p><button type='submit' name='save' value='gui'>" D_HTTP_SAVE_SETTINGS "</button></p></form>");

#if TOUCH_DRIVER == 2046 && defined(TOUCH_CS)
        add_form_button(httpMessage, F(D_HTTP_CALIBRATE), F("/config/gui"), F("name='cal' value='1'"));
#endif

        add_form_button(httpMessage, F(D_HTTP_ANTIBURN), F("/config/gui"), F("name='brn' value='1'"));
        add_form_button(httpMessage, F(D_BACK_ICON D_HTTP_CONFIGURATION), F("/config"), F(""));
        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    webSendFooter();

    if(webServer.hasArg(F("cal"))) dispatch_calibrate(NULL, NULL, TAG_HTTP);
    if(webServer.hasArg(F("brn"))) dispatch_antiburn(NULL, "on", TAG_HTTP);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
#if HASP_USE_WIFI > 0
void webHandleWifiConfig()
{ // http://plate01/config/wifi
    if(!httpIsAuthenticated(F("config/wifi"))) return;

    StaticJsonDocument<256> settings;
    wifiGetConfig(settings.to<JsonObject>());

    String httpMessage((char*)0);
    httpMessage.reserve(HTTP_PAGE_SIZE);
    httpMessage += F("<h1>");
    httpMessage += haspDevice.get_hostname();
    httpMessage += F("</h1><hr>");

    httpMessage += F("<form method='POST' action='/config'>");
    httpMessage += F("<b>WiFi SSID</b> <i><small>(required)</small></i><input id='ssid' required "
                     "name='ssid' maxlength=31 placeholder='WiFi SSID' value='");
    httpMessage += settings[FPSTR(FP_CONFIG_SSID)].as<String>();
    httpMessage += F("'><br/><b>WiFi Password</b> <i><small>(required)</small></i><input id='pass' required "
                     "name='pass' type='password' maxlength=63 placeholder='WiFi Password' value='");
    if(settings[FPSTR(FP_CONFIG_PASS)].as<String>() != "") {
        httpMessage += F(D_PASSWORD_MASK);
    }
    httpMessage +=
        F("'><p><button type='submit' name='save' value='wifi'>" D_HTTP_SAVE_SETTINGS "</button></p></form>");

#if HASP_USE_WIFI > 0 && !defined(STM32F4xx)
    if(WiFi.getMode() == WIFI_STA) {
        add_form_button(httpMessage, F(D_BACK_ICON D_HTTP_CONFIGURATION), F("/config"), F(""));
    }
#endif

    webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
    webServer.sendContent(httpMessage);
#if defined(STM32F4xx)
    httpMessage = "";
#else
    httpMessage.clear();
#endif
    webSendFooter();
}

#if HASP_USE_CAPTIVE_PORTAL > 0
// I'm not an experienced programmer, this was the only way I managed to get it to work..
void webHandleCaptivePortalWifiConfig()
{ // http://plate01/config/wifi
    if(!httpIsAuthenticated(F("config/wifi"))) return;

    StaticJsonDocument<256> settings;
    wifiGetConfig(settings.to<JsonObject>());

    String httpMessage((char*)0);
    httpMessage.reserve(HTTP_PAGE_SIZE);
    httpMessage += F("<h1>");
    httpMessage += haspDevice.get_hostname();
    httpMessage += F("</h1><hr>");

    httpMessage += F("<form method='POST' action='/config'>");
    httpMessage += F("<b>WiFi SSID</b> <i><small>(required)</small></i><input id='ssid' required "
                     "name='ssid' maxlength=31 placeholder='WiFi SSID' value='");
    httpMessage += settings[FPSTR(FP_CONFIG_SSID)].as<String>();
    httpMessage += F("'><br/><b>WiFi Password</b> <i><small>(required)</small></i><input id='pass' required "
                     "name='pass' type='password' maxlength=63 placeholder='WiFi Password' value='");
    if(settings[FPSTR(FP_CONFIG_PASS)].as<String>() != "") {
        httpMessage += F(D_PASSWORD_MASK);
    }
    httpMessage +=
        F("'><p><button type='submit' name='save' value='wifi'>" D_HTTP_SAVE_SETTINGS "</button></p></form>");

#if HASP_USE_WIFI > 0 && !defined(STM32F4xx)
    if(WiFi.getMode() == WIFI_STA) {
        add_form_button(httpMessage, F(D_BACK_ICON D_HTTP_CONFIGURATION), F("/config"), F(""));
    }
#endif

    // webServer.send(200, "text/html", httpMessage);
    webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
    webServer.sendContent(httpMessage);
#if defined(STM32F4xx)
    httpMessage = "";
#else
    httpMessage.clear();
#endif
    webSendFooter();
}
#endif // HASP_USE_CAPTIVE_PORTAL

#endif // HASP_USE_WIFI

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleHttpConfig()
{ // http://plate01/config/http
    if(!httpIsAuthenticated(F("config/http"))) return;

    {
        StaticJsonDocument<256> settings;
        httpGetConfig(settings.to<JsonObject>());

        char httpMessage[HTTP_PAGE_SIZE];

        size_t len = snprintf_P(
            httpMessage, sizeof(httpMessage),
            PSTR("<h1>%s</h1><hr>"
                 "<form method='POST' action='/config'>"
                 "<b>Web Username</b> <i><small>(optional)</small></i>"
                 "<input id='user' name='user' maxlength=31 placeholder='admin' value='%s'><br/>"
                 "<b>Web Password</b> <i><small>(optional)</small></i>"
                 "<input id='pass' name='pass' type='password' maxlength=63 placeholder='Password' value='%s'>"
                 "<p><button type='submit' name='save' value='http'>" D_HTTP_SAVE_SETTINGS "</button></p></form>"
                 "<p><form method='GET' action='/config'><button type='submit'>&#8617; " D_HTTP_CONFIGURATION
                 "</button></form></p>"),
            haspDevice.get_hostname(), settings[FPSTR(FP_CONFIG_USER)].as<String>().c_str(),
            settings[FPSTR(FP_CONFIG_PASS)].as<String>().c_str());

        // if(settings[FPSTR(FP_CONFIG_PASS)].as<String>() != "") {
        //     httpMessage += F(D_PASSWORD_MASK);
        // }

        webSendPage(haspDevice.get_hostname(), len, false);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
#if HASP_USE_GPIO > 0
void webHandleGpioConfig()
{ // http://plate01/config/gpio
    if(!httpIsAuthenticated(F("config/gpio"))) return;
    uint8_t configCount = 0;

    //   StaticJsonDocument<256> settings;
    // gpioGetConfig(settings.to<JsonObject>());

    if(webServer.hasArg(PSTR("save"))) {
        uint8_t id      = webServer.arg(F("id")).toInt();
        uint8_t pin     = webServer.arg(F("pin")).toInt();
        uint8_t type    = webServer.arg(F("type")).toInt();
        uint8_t group   = webServer.arg(F("group")).toInt();
        uint8_t pinfunc = webServer.arg(F("func")).toInt();
        bool inverted   = webServer.arg(F("state")).toInt();
        gpioSavePinConfig(id, pin, type, group, pinfunc, inverted);
    }
    if(webServer.hasArg(PSTR("del"))) {
        uint8_t id  = webServer.arg(F("id")).toInt();
        uint8_t pin = webServer.arg(F("pin")).toInt();
        gpioSavePinConfig(id, pin, hasp_gpio_type_t::FREE, 0, 0, false);
    }

    {
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        httpMessage += F("<form method='POST' action='/config'>");

        httpMessage += F("<table><tr><th>" D_GPIO_PIN "</th><th>Type</th><th>" D_GPIO_GROUP
                         "</th><th>Default</th><th>Action</th></tr>");

        for(uint8_t gpio = 0; gpio < NUM_DIGITAL_PINS; gpio++) {
            for(uint8_t id = 0; id < HASP_NUM_GPIO_CONFIG; id++) {
                hasp_gpio_config_t conf = gpioGetPinConfig(id);
                if((conf.pin == gpio) && gpioConfigInUse(id) && !gpioIsSystemPin(gpio)) {
                    httpMessage += F("<tr><td>");
                    // httpMessage += halGpioName(gpio);
                    httpMessage += haspDevice.gpio_name(gpio).c_str();
                    if(conf.type >= 0x80) {
                        httpMessage += F("</td><td><a href='/config/gpio/input?id=");
                    } else {
                        httpMessage += F("</td><td><a href='/config/gpio/options?id=");
                    }
                    httpMessage += id;
                    httpMessage += F("'>");

                    switch(conf.type) {

                        case hasp_gpio_type_t::BUTTON:
                            httpMessage += F(D_GPIO_BUTTON);
                            break;
                        case hasp_gpio_type_t::SWITCH:
                            httpMessage += F(D_GPIO_SWITCH);
                            break;
                        case hasp_gpio_type_t::DOOR:
                            httpMessage += F("door");
                            break;
                        case hasp_gpio_type_t::GARAGE_DOOR:
                            httpMessage += F("garage_door");
                            break;
                        case hasp_gpio_type_t::GAS:
                            httpMessage += F("gas");
                            break;
                        case hasp_gpio_type_t::LIGHT:
                            httpMessage += F("light");
                            break;
                        case hasp_gpio_type_t::LOCK:
                            httpMessage += F("lock");
                            break;
                        case hasp_gpio_type_t::MOISTURE:
                            httpMessage += F("moisture");
                            break;
                        case hasp_gpio_type_t::MOTION:
                            httpMessage += F("motion");
                            break;
                        case hasp_gpio_type_t::OCCUPANCY:
                            httpMessage += F("occupancy");
                            break;
                        case hasp_gpio_type_t::OPENING:
                            httpMessage += F("opening");
                            break;
                        case hasp_gpio_type_t::PRESENCE:
                            httpMessage += F("presence");
                            break;
                        case hasp_gpio_type_t::PROBLEM:
                            httpMessage += F("problem");
                            break;
                        case hasp_gpio_type_t::SAFETY:
                            httpMessage += F("Safety");
                            break;
                        case hasp_gpio_type_t::SMOKE:
                            httpMessage += F("Smoke");
                            break;
                        case hasp_gpio_type_t::VIBRATION:
                            httpMessage += F("Vibration");
                            break;
                        case hasp_gpio_type_t::WINDOW:
                            httpMessage += F("Window");
                            break;

                        case hasp_gpio_type_t::TOUCH:
                            httpMessage += F(D_GPIO_TOUCH);
                            break;
                        case hasp_gpio_type_t::LED:
                            httpMessage += F(D_GPIO_LED);
                            break;
                        case hasp_gpio_type_t::LED_R:
                            httpMessage += F(D_GPIO_LED_R);
                            break;
                        case hasp_gpio_type_t::LED_G:
                            httpMessage += F(D_GPIO_LED_G);
                            break;
                        case hasp_gpio_type_t::LED_B:
                            httpMessage += F(D_GPIO_LED_B);
                            break;
                        case hasp_gpio_type_t::LIGHT_RELAY:
                            httpMessage += F(D_GPIO_LIGHT_RELAY);
                            break;
                        case hasp_gpio_type_t::POWER_RELAY:
                            httpMessage += F(D_GPIO_POWER_RELAY);
                            break;
                        case hasp_gpio_type_t::SHUTTER_RELAY:
                            httpMessage += F("SHUTTER_RELAY");
                            break;
                        case hasp_gpio_type_t::PWM:
                            httpMessage += F(D_GPIO_PWM);
                            break;
                        case hasp_gpio_type_t::HASP_DAC:
                            httpMessage += F(D_GPIO_DAC);
                            break;

#if defined(LANBONL8)
                            // case hasp_gpio_type_t::SERIAL_DIMMER:
                            //     httpMessage += F(D_GPIO_SERIAL_DIMMER);
                            //     break;
                        case hasp_gpio_type_t::SERIAL_DIMMER_EU:
                            httpMessage += F("L8-HD (EU)");
                            break;
                        case hasp_gpio_type_t::SERIAL_DIMMER_AU:
                            httpMessage += F("L8-HD (AU)");
                            break;
#endif
                        default:
                            httpMessage += F(D_GPIO_UNKNOWN);
                    }

                    httpMessage += F("</a></td><td>");
                    httpMessage += conf.group;
                    httpMessage += F("</td><td>");
                    httpMessage += (conf.inverted) ? F(D_GPIO_STATE_INVERTED) : F(D_GPIO_STATE_NORMAL);

                    httpMessage += ("</td><td><a href='/config/gpio?del=&id=");
                    httpMessage += id;
                    httpMessage += ("&pin=");
                    httpMessage += conf.pin;
                    httpMessage += ("'>Delete</a></td><tr>");
                    configCount++;
                }
            }
        }

        httpMessage += F("</table></form>");

        if(configCount < HASP_NUM_GPIO_CONFIG) {
            httpMessage += F("<p><form method='GET' action='gpio/input'>");
            httpMessage += F("<input type='hidden' name='id' value='");
            httpMessage += gpioGetFreeConfigId();
            httpMessage += F("'><button type='submit'>" D_HTTP_ADD_GPIO " Input</button></form></p>");

            httpMessage += F("<p><form method='GET' action='gpio/options'>");
            httpMessage += F("<input type='hidden' name='id' value='");
            httpMessage += gpioGetFreeConfigId();
            httpMessage += F("'><button type='submit'>" D_HTTP_ADD_GPIO " Output</button></form></p>");
        }

        add_form_button(httpMessage, F(D_BACK_ICON D_HTTP_CONFIGURATION), F("/config"), F(""));
        //    httpMessage += F("<p><form method='GET' action='/config'><button type='submit'>&#8617; "
        //    D_HTTP_CONFIGURATION
        //                      "</button></form></p>");

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleGpioOutput()
{ // http://plate01/config/gpio/options
    if(!httpIsAuthenticated(F("config/gpio/options"))) return;

    {
        StaticJsonDocument<256> settings;
        guiGetConfig(settings.to<JsonObject>());

        uint8_t config_id = webServer.arg(F("id")).toInt();

        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        httpMessage += F("<form method='GET' action='/config/gpio'>");
        httpMessage += F("<input type='hidden' name='id' value='");
        httpMessage += config_id;
        httpMessage += F("'>");

        httpMessage += F("<p><b>GPIO Output</b></p>");

        httpMessage += F("<p><b>" D_GPIO_PIN "</b> <select id='pin' name='pin'>");
        hasp_gpio_config_t conf = gpioGetPinConfig(config_id);

        for(uint8_t io = 0; io < NUM_DIGITAL_PINS; io++) {
            if(((conf.pin == io) || !gpioInUse(io)) && !gpioIsSystemPin(io)) {
                httpMessage += getOption(io, haspDevice.gpio_name(io).c_str(), conf.pin);
            }
        }
        httpMessage += F("</select></p>");

        httpMessage += F("<p><b>Type</b> <select id='type' name='type'>");
        httpMessage += getOption(hasp_gpio_type_t::LED, F(D_GPIO_LED), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::LED_R, F(D_GPIO_LED_R), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::LED_G, F(D_GPIO_LED_G), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::LED_B, F(D_GPIO_LED_B), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::LIGHT_RELAY, F(D_GPIO_LIGHT_RELAY), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::POWER_RELAY, F(D_GPIO_POWER_RELAY), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::SHUTTER_RELAY, F("Shutter Relay"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::HASP_DAC, F(D_GPIO_DAC), conf.type);
        // httpMessage += getOption(hasp_gpio_type_t::SERIAL_DIMMER, F(D_GPIO_SERIAL_DIMMER), conf.type);
#if defined(LANBONL8)
        httpMessage += getOption(hasp_gpio_type_t::SERIAL_DIMMER_AU, F("L8-HD (AU)"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::SERIAL_DIMMER_EU, F("L8-HD (EU)"), conf.type);
#endif
        if(digitalPinHasPWM(webServer.arg(0).toInt())) {
            httpMessage += getOption(hasp_gpio_type_t::PWM, F(D_GPIO_PWM), conf.type);
        }
        httpMessage += F("</select></p>");

        httpMessage += F("<p><b>" D_GPIO_GROUP "</b> <select id='group' name='group'>");
        httpMessage += getOption(0, F(D_GPIO_GROUP_NONE), conf.group);
        String group((char*)0);
        group.reserve(10);
        for(int i = 1; i < 15; i++) {
            group = F(D_GPIO_GROUP " ");
            group += i;
            httpMessage += getOption(i, group, conf.group);
        }
        httpMessage += F("</select></p>");

        httpMessage += F("<p><b>Value</b> <select id='state' name='state'>");
        httpMessage += getOption(0, F(D_GPIO_STATE_NORMAL), conf.inverted);
        httpMessage += getOption(1, F(D_GPIO_STATE_INVERTED), conf.inverted);
        httpMessage += F("</select></p>");

        httpMessage +=
            F("<p><button type='submit' name='save' value='gpio'>" D_HTTP_SAVE_SETTINGS "</button></p></form>");

        httpMessage += PSTR("<p><form method='GET' action='/config/gpio'><button type='submit'>&#8617; " D_HTTP_BACK
                            "</button></form></p>");

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    webSendFooter();

    // if(webServer.hasArg(F("action"))) dispatch_text_line(webServer.arg(F("action")).c_str()); // Security check
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleGpioInput()
{ // http://plate01/config/gpio/options
    if(!httpIsAuthenticated(F("config/gpio/input"))) return;
    {
        StaticJsonDocument<256> settings;
        guiGetConfig(settings.to<JsonObject>());

        uint8_t config_id = webServer.arg(F("id")).toInt();

        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        httpMessage += F("<form method='GET' action='/config/gpio'>");
        httpMessage += F("<input type='hidden' name='id' value='");
        httpMessage += config_id;
        httpMessage += F("'>");

        httpMessage += F("<p><b>GPIO Input</b></p>");

        httpMessage += F("<p><b>" D_GPIO_PIN "</b> <select id='pin' name='pin'>");
        hasp_gpio_config_t conf = gpioGetPinConfig(config_id);

        for(uint8_t io = 0; io < NUM_DIGITAL_PINS; io++) {
            if(((conf.pin == io) || !gpioInUse(io)) && !gpioIsSystemPin(io)) {
                httpMessage += getOption(io, haspDevice.gpio_name(io).c_str(), conf.pin);
            }
        }
        httpMessage += F("</select></p>");

        httpMessage += F("<p><b>Type</b> <select id='type' name='type'>");
        httpMessage += getOption(hasp_gpio_type_t::BUTTON, F(D_GPIO_BUTTON), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::SWITCH, F(D_GPIO_SWITCH), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::DOOR, F("door"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::GARAGE_DOOR, F("garage_door"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::GAS, F("gas"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::LIGHT, F("light"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::LOCK, F("lock"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::MOISTURE, F("moisture"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::MOTION, F("motion"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::OCCUPANCY, F("occupancy"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::OPENING, F("opening"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::PRESENCE, F("presence"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::PROBLEM, F("problem"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::SAFETY, F("Safety"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::SMOKE, F("Smoke"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::VIBRATION, F("Vibration"), conf.type);
        httpMessage += getOption(hasp_gpio_type_t::WINDOW, F("Window"), conf.type);
        httpMessage += F("</select></p>");

        httpMessage += F("<p><b>" D_GPIO_GROUP "</b> <select id='group' name='group'>");
        httpMessage += getOption(0, F(D_GPIO_GROUP_NONE), conf.group);
        String group((char*)0);
        group.reserve(10);
        for(int i = 1; i < 15; i++) {
            group = F(D_GPIO_GROUP " ");
            group += i;
            httpMessage += getOption(i, group, conf.group);
        }
        httpMessage += F("</select></p>");

        httpMessage += F("<p><b>Default State</b> <select id='state' name='state'>");
        httpMessage += getOption(0, F("Normally Open"), conf.inverted);
        httpMessage += getOption(1, F("Normally Closed"), conf.inverted);
        httpMessage += F("</select></p>");

        httpMessage += F("<p><b>Resistor</b> <select id='func' name='func'>");
        httpMessage += getOption(hasp_gpio_function_t::INTERNAL_PULLUP, F("Internal Pullup"), conf.gpio_function);
        httpMessage += getOption(hasp_gpio_function_t::INTERNAL_PULLDOWN, F("Internal Pulldown"), conf.gpio_function);
        httpMessage += getOption(hasp_gpio_function_t::EXTERNAL_PULLUP, F("External Pullup"), conf.gpio_function);
        httpMessage += getOption(hasp_gpio_function_t::EXTERNAL_PULLDOWN, F("External Pulldown"), conf.gpio_function);
        httpMessage += F("</select></p>");

        httpMessage +=
            F("<p><button type='submit' name='save' value='gpio'>" D_HTTP_SAVE_SETTINGS "</button></p></form>");

        httpMessage += PSTR("<p><form method='GET' action='/config/gpio'><button type='submit'>&#8617; " D_HTTP_BACK
                            "</button></form></p>");

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    webSendFooter();

    // if(webServer.hasArg(F("action"))) dispatch_text_line(webServer.arg(F("action")).c_str()); // Security check
}
#endif // HASP_USE_GPIO

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleDebugConfig()
{ // http://plate01/config/debug
    if(!httpIsAuthenticated(F("config/debug"))) return;

    StaticJsonDocument<256> settings;
    debugGetConfig(settings.to<JsonObject>());

    {
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        httpMessage += F("<form method='POST' action='/config'>");

        uint16_t baudrate = settings[FPSTR(FP_CONFIG_BAUD)].as<uint16_t>();
        httpMessage += F("<p><b>Serial Port</b> <select id='baud' name='baud'>");
        httpMessage += getOption(1, F(D_SETTING_DISABLED), baudrate); // Don't use 0 here which is default 115200
        httpMessage += getOption(960, F("9600"), baudrate);
        httpMessage += getOption(1920, F("19200"), baudrate);
        httpMessage += getOption(3840, F("38400"), baudrate);
        httpMessage += getOption(5760, F("57600"), baudrate);
        httpMessage += getOption(7488, F("74880"), baudrate);
        httpMessage += getOption(11520, F("115200"), baudrate);
        httpMessage += F("</select></p><p><b>Telemetry Period</b> <i><small>(Seconds, 0=disable)</small></i> "
                         "<input id='tele' required name='tele' type='number' min='0' max='65535' value='");
        httpMessage += settings[FPSTR(FP_DEBUG_TELEPERIOD)].as<String>();
        httpMessage += F("'></p>");

#if HASP_USE_SYSLOG > 0
        httpMessage += F("<b>Syslog Hostname</b> <i><small>(optional)</small></i><input id='host' "
                         "name='host' maxlength=31 placeholder='logserver' value='");
        httpMessage += settings[FPSTR(FP_CONFIG_HOST)].as<String>();
        httpMessage += F("'><br/><b>Syslog Port</b> <i><small>(optional)</small></i> <input id='port' required "
                         "name='port' type='number' min='0' max='65535' value='");
        httpMessage += settings[FPSTR(FP_CONFIG_PORT)].as<String>();

        httpMessage += F("'><b>Syslog Facility</b> <select id='log' name='log'>");
        uint8_t logid = settings[FPSTR(FP_CONFIG_LOG)].as<uint8_t>();
        for(int i = 0; i < 8; i++) {
            httpMessage += getOption(i, String(F("Local")) + i, logid);
        }

        httpMessage += F("</select></br><b>Syslog Protocol</b> <input id='proto' name='proto' type='radio' value='0'");
        if(settings[FPSTR(FP_CONFIG_PROTOCOL)].as<uint8_t>() == 0) httpMessage += F(" checked");
        httpMessage += F(">IETF (RFC 5424) &nbsp; <input id='proto' name='proto' type='radio' value='1'");
        if(settings[FPSTR(FP_CONFIG_PROTOCOL)].as<uint8_t>() == 1) httpMessage += F(" checked");
        httpMessage += F(">BSD (RFC 3164)");
#endif

        httpMessage +=
            F("</p><p><button type='submit' name='save' value='debug'>" D_HTTP_SAVE_SETTINGS "</button></p></form>");

        add_form_button(httpMessage, F(D_BACK_ICON D_HTTP_CONFIGURATION), F("/config"), F(""));
        // httpMessage += PSTR("<p><form method='GET' action='/config'><button type='submit'>&#8617; "
        // D_HTTP_CONFIGURATION
        //                     "</button></form></p>");

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleHaspConfig()
{ // http://plate01/config/http
    if(!httpIsAuthenticated(F("config/hasp"))) return;

    StaticJsonDocument<256> settings;
    haspGetConfig(settings.to<JsonObject>());

    {
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        httpMessage += F("<p><form action='/edit' method='POST' enctype='multipart/form-data'><input type='file' "
                         "name='filename' accept='.jsonl,.png,.zi'>");
        httpMessage += F("<button type='submit'>" D_HTTP_UPLOAD_FILE "</button></form></p><hr>");

        // httpMessage += F("<form method='POST' action='/config'>");
        httpMessage += F("<form method='POST' action='/'>");
        httpMessage += F("<p><b>UI Theme</b> <i><small>(required)</small></i><select id='theme' name='theme'>");

        uint8_t themeid = settings[FPSTR(FP_CONFIG_THEME)].as<uint8_t>();
        // httpMessage += getOption(0, F("Built-in"), themeid );
#if LV_USE_THEME_HASP == 1
        httpMessage += getOption(2, F("Hasp Dark"), themeid);
        httpMessage += getOption(1, F("Hasp Light"), themeid);
#endif
#if LV_USE_THEME_EMPTY == 1
        httpMessage += getOption(0, F("Empty"), themeid);
#endif
#if LV_USE_THEME_MONO == 1
        httpMessage += getOption(3, F("Mono"), themeid);
#endif
#if LV_USE_THEME_MATERIAL == 1
        httpMessage += getOption(5, F("Material Dark"), themeid);
        httpMessage += getOption(4, F("Material Light"), themeid);
#endif
#if LV_USE_THEME_TEMPLATE == 1
        httpMessage += getOption(7, F("Template"), themeid);
#endif
        httpMessage += F("</select></br>");
        httpMessage += F("<b>Hue</b><div style='width:100%;background-image:linear-gradient(to "
                         "right,red,orange,yellow,green,blue,indigo,violet);'><input "
                         "style='align:center;padding:0px;width:100%;' "
                         "name='hue' type='range' min='0' max='360' value='");
        httpMessage += settings[FPSTR(FP_CONFIG_HUE)].as<String>();
        httpMessage += F("'></div></p>");
        httpMessage += F("<p><b>Default Font</b><select id='font' name='font'><option value=''>None</option>");

#if defined(ARDUINO_ARCH_ESP32)
        File root        = HASP_FS.open("/");
        File file        = root.openNextFile();
        String main_font = settings[FPSTR(FP_CONFIG_ZIFONT)].as<String>();

        while(file) {
            String filename = file.name();
            if(filename.endsWith(".zi")) httpMessage += getOption(filename, filename, main_font);
            file = root.openNextFile();
        }
#elif defined(ARDUINO_ARCH_ESP8266)
        Dir dir = HASP_FS.openDir("/");
        String main_font = settings[FPSTR(FP_CONFIG_ZIFONT)].as<String>();

        while(dir.next()) {
            File file = dir.openFile("r");
            String filename = file.name();
            if(filename.endsWith(".zi")) httpMessage += getOption(filename, filename, main_font);
            file.close();
        }
#endif
        httpMessage += F("</select></p>");

        httpMessage += F("<p><b>Startup Layout</b> <i><small>(optional)</small></i><input id='pages' "
                         "name='pages' maxlength=31 placeholder='/pages.jsonl' value='");

        httpMessage += settings[FPSTR(FP_CONFIG_PAGES)].as<String>();
        httpMessage += F("'></br><b>Startup Page</b> <i><small>(required)</small></i><input id='startpage' required "
                         "name='startpage' type='number' min='1' max='12' value='");
        httpMessage += settings[FPSTR(FP_CONFIG_STARTPAGE)].as<String>();
        httpMessage +=
            F("'></p><p><b>Startup Brightness</b> <i><small>(required)</small></i><input id='startpage' required "
              "name='startdim' type='number' min='0' max='255' value='");
        httpMessage += settings[FPSTR(FP_CONFIG_STARTDIM)].as<String>();
        httpMessage += F("'></p>");

        httpMessage +=
            F("<p><button type='submit' name='save' value='hasp'>" D_HTTP_SAVE_SETTINGS "</button></form></p>");

        // httpMessage +=
        //     F("<p><form method='GET' action='/config'><button
        //     type='submit'>"D_HTTP_CONFIGURATION"</button></form></p>");
        httpMessage += FPSTR(MAIN_MENU_BUTTON);

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();
}
#endif // HASP_USE_CONFIG

////////////////////////////////////////////////////////////////////////////////////////////////////
void httpHandleNotFound()
{ // webServer 404
#if HASP_USE_SPIFFS > 0 || HASP_USE_LITTLEFS > 0
    int statuscode = handleFileRead(webServer.uri());
#else
    int statuscode = 404;
#endif

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    LOG_TRACE(TAG_HTTP, F("Sending %d %s to client connected from: %s"), statuscode, webServer.uri().c_str(),
              webServer.client().remoteIP().toString().c_str());
#else
    // LOG_TRACE(TAG_HTTP,F("Sending 404 to client connected from: %s"),
    // String(webServer.client().remoteIP()).c_str());
#endif

    if(statuscode < 300) return; // OK

    String httpMessage((char*)0);
    httpMessage.reserve(HTTP_PAGE_SIZE);

    if(statuscode == 500)
        httpMessage += F("Internal Server Error");
    else
        httpMessage += F(D_FILE_NOT_FOUND);

    httpMessage += F("\n\nURI: ");
    httpMessage += webServer.uri();
    httpMessage += F("\nMethod: ");
    httpMessage += (webServer.method() == HTTP_GET) ? F("GET") : F("POST");
    httpMessage += F("\nArguments: ");
    httpMessage += webServer.args();
    httpMessage += "\n";
    for(int i = 0; i < webServer.args(); i++) {
        httpMessage += " " + webServer.argName(i) + ": " + webServer.arg(i) + "\n";
    }
    webServer.send(statuscode, PSTR("text/plain"), httpMessage.c_str());
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleFirmware()
{
    if(!httpIsAuthenticated(F("firmware"))) return;

    {
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        httpMessage += F("<p><form action='/update' method='POST' enctype='multipart/form-data'><input type='file' "
                         "name='filename' accept='.bin'>");
        // httpMessage += F("<button type='submit'>" D_HTTP_UPDATE_FIRMWARE "</button></form></p>");

        httpMessage += F("<input id='cmd' name='cmd' type='radio' value='0' checked>Firmware &nbsp; "
                         "<input id='cmd' name='cmd' type='radio' value='100'>Filesystem");

        add_button(httpMessage, F(D_HTTP_UPDATE_FIRMWARE), F(""));
        httpMessage += F("</form></p>");

        // httpMessage += F("<p><form action='/update' method='POST' enctype='multipart/form-data'><input
        // type='file' "
        //                  "name='filename' accept='.spiffs'>");
        // httpMessage += F("<button type='submit'>Replace Filesystem Image</button></form></p>");

        httpMessage += F("<form method='GET' action='/espfirmware'>");
        httpMessage += F("<br/><b>Update ESP from URL</b>");
        httpMessage += F("<br/><input id='url' name='url' value='");
        httpMessage += "";
        httpMessage += F("'><br/><br/><button type='submit'>Update ESP from URL</button></form>");

        httpMessage += FPSTR(MAIN_MENU_BUTTON);

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), false);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void httpHandleEspFirmware()
{ // http://plate01/espfirmware
    char url[4];
    memcpy_P(url, PSTR("url"), 4);

    if(!httpIsAuthenticated(F("espfirmware"))) return;

    {
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        httpMessage += F("<p><b>ESP update</b></p>Updating ESP firmware from: ");
        httpMessage += webServer.arg(url);

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), true);
        webServer.sendContent(httpMessage);
        // httpMessage.clear();
    }
    webSendFooter();

    LOG_TRACE(TAG_HTTP, F("Updating ESP firmware from: %s"), webServer.arg(url).c_str());
    dispatch_web_update(NULL, webServer.arg(url).c_str(), TAG_HTTP);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
#if HASP_USE_CONFIG > 0
void webHandleSaveConfig()
{
    if(!httpIsAuthenticated(F("saveConfig"))) return;
    configWrite();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void httpHandleResetConfig()
{ // http://plate01/resetConfig
    if(!httpIsAuthenticated(F("resetConfig"))) return;

    bool resetConfirmed = webServer.arg(F("confirm")) == F("yes");

    {
        String httpMessage((char*)0);
        httpMessage.reserve(HTTP_PAGE_SIZE);
        httpMessage += F("<h1>");
        httpMessage += haspDevice.get_hostname();
        httpMessage += F("</h1><hr>");

        if(resetConfirmed) {                           // User has confirmed, so reset everything
            bool formatted = dispatch_factory_reset(); // configClearEeprom();
            if(formatted) {
                httpMessage += F("<b>Resetting all saved settings and restarting device</b>");
            } else {
                httpMessage += F("<b>Failed to format the internal flash partition</b>");
                resetConfirmed = false;
            }
        } else {
            httpMessage +=
                F("<h2>Warning</h2><b>This process will reset all settings to the default values. The internal flash "
                  "will be erased and the device is restarted. You may need to connect to the WiFi AP displayed on "
                  "the "
                  "panel to re-configure the device before accessing it again. ALL FILES WILL BE LOST!</b>"
                  "<br/><hr><br/><form method='GET' action='resetConfig'>");

            add_button(httpMessage, F(D_HTTP_ERASE_DEVICE), F("name='confirm' value='yes'"));
            close_form(httpMessage);

            add_form_button(httpMessage, F(D_BACK_ICON D_HTTP_CONFIGURATION), F("/config"), F(""));
        }

        webSendPage(haspDevice.get_hostname(), httpMessage.length(), resetConfirmed);
        webServer.sendContent(httpMessage);
    }
    // httpMessage.clear();
    webSendFooter();

    if(resetConfirmed) {
        delay(250);
        // configClearSaved();
        dispatch_reboot(false); // Do not save the current config
    }
}
#endif // HASP_USE_CONFIG

void httpStart()
{
    webServer.begin();
    webServerStarted = true;
#if HASP_USE_WIFI > 0
#if defined(STM32F4xx)
    IPAddress ip;
    ip = WiFi.localIP();
    LOG_INFO(TAG_HTTP, F(D_SERVICE_STARTED " @ http://%d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
#else
    LOG_INFO(TAG_HTTP, F(D_SERVICE_STARTED " @ http://%s"),
             (WiFi.getMode() != WIFI_STA ? WiFi.softAPIP().toString().c_str() : WiFi.localIP().toString().c_str()));
#endif
#else
    IPAddress ip;
#if defined(ARDUINO_ARCH_ESP32)
    ip = ETH.localIP();
#else
    ip = Ethernet.localIP();
#endif
    LOG_INFO(TAG_HTTP, F(D_SERVICE_STARTED " @ http://%d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
#endif
}

void httpStop()
{
    webServer.stop();
    webServerStarted = false;
    LOG_WARNING(TAG_HTTP, F(D_SERVICE_STOPPED));
}

// Do not keep CSS in memory because it is cached in the browser
void webSendCss()
{
    String HTTP_CSS =
        F("body,.c{text-align:center;}"
          "div,input{padding:5px;font-size:1em;}"
          "a{color:" D_HTTP_COLOR_TEXT "}"
          "input:not([type=file]){width:90%;background-color:" D_HTTP_COLOR_INPUT ";color:" D_HTTP_COLOR_INPUT_TEXT ";}"
          "input[type=checkbox],input[type=radio]{width:1em;}"
          "select{background-color:" D_HTTP_COLOR_INPUT ";color:" D_HTTP_COLOR_INPUT_TEXT ";}"
          "input:invalid{border:1px solid " D_HTTP_COLOR_INPUT_WARNING ";}"
          //"#hue{width:100%;}"
          "body{font-family:verdana;width:60%;margin:auto;background:" D_HTTP_COLOR_BACKGROUND
          ";color:" D_HTTP_COLOR_TEXT ";}"
          "button{border:0;border-radius:0.6rem;background-color:" D_HTTP_COLOR_BUTTON
          ";color:" D_HTTP_COLOR_BUTTON_TEXT ";line-height:2.4rem;font-size:1.2rem;width:100%;}"
          //".q{float:right;width:64px;text-align:right;}"
          ".red{background-color:" D_HTTP_COLOR_BUTTON_RESET ";}"
          "#doc{text-align:left;display:inline-block;color:" D_HTTP_COLOR_TEXT ";min-width:260px;}"
          // ".button3{background-color:#f44336;}"
          // ".button4{background-color:#e7e7e7;color:black;}"
          // ".button5{background-color:#555555;}"
          // ".button6{background-color:#4CAF50;}"
          "td{font-size:0.87rem;padding-bottom:0px;padding-top:0px;}th{padding-top:0.5em;}");
    webSendCached(200, PSTR("text/css"), HTTP_CSS.c_str(), HTTP_CSS.length());
}

// Do not keep JS in memory because it is cached in the browser
void webSendJavascript()
{
    String javascript = F("function aref(t){setTimeout(function() {ref('');}, t*1000)}"
                          "function ref(a){ var t=new "
                          "Date().getTime();document.getElementById('bmp').src='?a='+a+'&q='+t;return false;}");
    webSendCached(200, PSTR("text/javascript"), javascript.c_str(), javascript.length());
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void httpSetup()
{
    // httpSetConfig(settings);

    // ask server to track these headers
    const char* headerkeys[] = {"Content-Length"}; // "Authentication"

    // const char HTTP_SCRIPT[] PROGMEM = "<script>function "
    //                                    "c(l){document.getElementById('s').value=l.innerText||l.textContent;document."
    //                                    "getElementById('p').focus();}</script>";
    size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
    webServer.collectHeaders(headerkeys, headerkeyssize);

    // Shared pages
    webServer.on(F("/about"), webHandleAbout);
    webServer.on(F("/css"), webSendCss);
    webServer.on(F("/js"), webSendJavascript);
    webServer.onNotFound(httpHandleNotFound);

#if HASP_USE_WIFI > 0

    // These two endpoints are needed in STA and AP mode
    webServer.on(F("/config"), webHandleConfig);

#if !defined(STM32F4xx)

#if HASP_USE_CONFIG > 0
    if(WiFi.getMode() != WIFI_STA) {
#if HASP_USE_CAPTIVE_PORTAL > 0
        // if DNSServer is started with "*" for domain name, it will reply with
        // provided IP to all DNS request
        dnsServer.start(DNS_PORT, "*", apIP);
        // replay to all requests with same HTML
        webServer.onNotFound([]() {
            webHandleCaptivePortalWifiConfig();
            // webServer.send(200, "text/html", responseHTML);
            // webServer.on(F("/"), webHandleWifiConfig);
        });
#endif
        webServer.on(F("/"), webHandleWifiConfig);
        LOG_TRACE(TAG_HTTP, F("Wifi access point"));
        return;
    }

#endif // HASP_USE_CONFIG
#endif // !STM32F4xx
#endif // HASP_USE_WIFI

    // The following endpoints are only needed in STA mode
    webServer.on(F("/page/"), []() {
        String pageid = webServer.arg(F("page"));
        webServer.send(200, PSTR("text/plain"), "Page: '" + pageid + "'");
        dispatch_set_page(pageid.toInt(), LV_SCR_LOAD_ANIM_NONE);
    });

#if HASP_USE_SPIFFS > 0 || HASP_USE_LITTLEFS > 0
    webServer.on(F("/list"), HTTP_GET, handleFileList);
    // load editor
    webServer.on(F("/edit"), HTTP_GET, []() {
        if(handleFileRead("/edit.htm") != 200) {
            char mimetype[16];
            snprintf_P(mimetype, sizeof(mimetype), PSTR("text/plain"));
            webServer.send_P(404, mimetype, PSTR("FileNotFound"));
        }
    });
    webServer.on(F("/edit"), HTTP_PUT, handleFileCreate);
    webServer.on(F("/edit"), HTTP_DELETE, handleFileDelete);
    // first callback is called after the request has ended with all parsed arguments
    // second callback handles file uploads at that location
    webServer.on(
        F("/edit"), HTTP_POST,
        []() {
            webServer.send(200, "text/plain", "");
            LOG_VERBOSE(TAG_HTTP, F("Headers: %d"), webServer.headers());
        },
        handleFileUpload);
#endif

    webServer.on(F("/"), webHandleRoot);
    webServer.on(F("/info"), webHandleInfoJson);
    // webServer.on(F("/info"), webHandleInfo);
    webServer.on(F("/screenshot"), webHandleScreenshot);
    webServer.on(F("/firmware"), webHandleFirmware);
    webServer.on(F("/reboot"), httpHandleReboot);

#if HASP_USE_CONFIG > 0
    webServer.on(F("/config/hasp"), webHandleHaspConfig);
    webServer.on(F("/config/http"), webHandleHttpConfig);
    webServer.on(F("/config/gui"), webHandleGuiConfig);
    webServer.on(F("/config/debug"), webHandleDebugConfig);
#if HASP_USE_MQTT > 0
    webServer.on(F("/config/mqtt"), webHandleMqttConfig);
#endif
#if HASP_USE_WIFI > 0
    webServer.on(F("/config/wifi"), webHandleWifiConfig);
#endif
#if HASP_USE_GPIO > 0
    webServer.on(F("/config/gpio"), webHandleGpioConfig);
    webServer.on(F("/config/gpio/options"), webHandleGpioOutput);
    webServer.on(F("/config/gpio/input"), webHandleGpioInput);
#endif
    webServer.on(F("/saveConfig"), webHandleSaveConfig);
    webServer.on(F("/resetConfig"), httpHandleResetConfig);
#endif // HASP_USE_CONFIG

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    webServer.on(
        F("/update"), HTTP_POST,
        []() {
            webServer.send(200, "text/plain", "");
            LOG_VERBOSE(TAG_HTTP, F("Total size: %s"), webServer.hostHeader().c_str());
        },
        webHandleFirmwareUpload);
    webServer.on(F("/espfirmware"), httpHandleEspFirmware);
#endif

    LOG_INFO(TAG_HTTP, F(D_SERVICE_STARTED));
    // webStart();  Wait for network connection
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// static void httpReconnect()
// {
//     if(!http_config.enable) return;

//     if(webServerStarted) {
//         httpStop();
//     } else
// #if HASP_USE_WIFI > 0 && !defined(STM32F4xx)
//         if(WiFi.status() == WL_CONNECTED || WiFi.getMode() != WIFI_STA)
// #endif
//     {
//         httpStart();
//     }
// }

////////////////////////////////////////////////////////////////////////////////////////////////////
IRAM_ATTR void httpLoop(void)
{
#if(HASP_USE_CAPTIVE_PORTAL > 0) && (HASP_USE_WIFI > 0)
    dnsServer.processNextRequest();
#endif
    webServer.handleClient();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void httpEvery5Seconds()
{
    //  if(httpEnable && !webServerStarted) httpReconnect();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
#if HASP_USE_CONFIG > 0
bool httpGetConfig(const JsonObject& settings)
{
    bool changed = false;

    settings[FPSTR(FP_CONFIG_ENABLE)] = http_config.enable;

    if(http_config.port != settings[FPSTR(FP_CONFIG_PORT)].as<uint16_t>()) changed = true;
    settings[FPSTR(FP_CONFIG_PORT)] = http_config.port;

    if(strcmp(http_config.user, settings[FPSTR(FP_CONFIG_USER)].as<String>().c_str()) != 0) changed = true;
    settings[FPSTR(FP_CONFIG_USER)] = http_config.user;

    if(strcmp(http_config.password, settings[FPSTR(FP_CONFIG_PASS)].as<String>().c_str()) != 0) changed = true;
    settings[FPSTR(FP_CONFIG_PASS)] = http_config.password;

    if(changed) configOutput(settings, TAG_HTTP);
    return changed;
}

/** Set HTTP Configuration.
 *
 * Read the settings from json and sets the application variables.
 *
 * @note: data pixel should be formated to uint32_t RGBA. Imagemagick requirements.
 *
 * @param[in] settings    JsonObject with the config settings.
 **/
bool httpSetConfig(const JsonObject& settings)
{
    configOutput(settings, TAG_HTTP);
    bool changed = false;

    changed |= configSet(http_config.port, settings[FPSTR(FP_CONFIG_PORT)], F("httpPort"));

    if(!settings[FPSTR(FP_CONFIG_USER)].isNull()) {
        changed |= strcmp(http_config.user, settings[FPSTR(FP_CONFIG_USER)]) != 0;
        strncpy(http_config.user, settings[FPSTR(FP_CONFIG_USER)], sizeof(http_config.user));
    }

    if(!settings[FPSTR(FP_CONFIG_PASS)].isNull()) {
        changed |= strcmp(http_config.password, settings[FPSTR(FP_CONFIG_PASS)]) != 0;
        strncpy(http_config.password, settings[FPSTR(FP_CONFIG_PASS)], sizeof(http_config.password));
    }

    return changed;
}
#endif // HASP_USE_CONFIG

size_t httpClientWrite(const uint8_t* buf, size_t size)
{
    /***** Sending 16Kb at once freezes on STM32 EthernetClient *****/
    size_t bytes_sent = 0;
    while(bytes_sent < size) {
        if(!webServer.client()) return bytes_sent;
        if(size - bytes_sent >= 2048) {
            bytes_sent += webServer.client().write(buf + bytes_sent, 2048);
        } else {
            bytes_sent += webServer.client().write(buf + bytes_sent, size - bytes_sent);
        }
        // Serial.println(bytes_sent);

        // stm32_eth_scheduler(); // already in write
        // webServer.client().flush();
        delay(1); // Fixes the freeze
    }
    return bytes_sent;
}

#endif
