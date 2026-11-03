/**
 * Detailed usage of the example can be found in the [README.md](./README.md) file
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <Arduino.h>
#include "nvs.h"
#include "nvs_flash.h"

#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <ui.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "lvgl_v8_port.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

/* Here are some macros need to be filled by users */
#if __has_include("secrets.h")
#include "secrets.h"
#endif

// Fallback defaults if secrets.h is missing
#ifndef WEATHER_API_KEY
#define WEATHER_API_KEY         ""
#endif
#ifndef WEATHER_CITY
#define WEATHER_CITY            ""
#endif
#ifndef TIMEZONE_OFFSET
/* India Standard Time (UTC+5:30). POSIX TZ offsets are the time added to
 * local time to reach UTC, hence the minus sign for an eastern zone. */
#define TIMEZONE_OFFSET         "IST-5:30"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID               ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD           ""
#endif

/* Forward declarations (the sketch .ino preprocessing is not applied to .cpp files) */
void getWeather(void);

/* Here are some macros can be changed */
#define GEO_API_BASE_URL        "http://api.openweathermap.org/geo/1.0/direct?q="
#define API_KEY_SUFFIX          "&limit=1&appid="
#define OPENWEATHER_BASE_URL    "http://api.openweathermap.org/data/2.5/weather?lat="
#define LAT_PARAM               "&lon="
#define API_KEY_PARAM           "&appid="
/* Open-Meteo (https://open-meteo.com) needs no API key and serves plain HTTP,
 * which keeps the firmware free of TLS credential handling. It is the default
 * provider; set WEATHER_API_KEY in secrets.h to use OpenWeatherMap instead. */
#define OPENMETEO_GEO_BASE_URL  "http://geocoding-api.open-meteo.com/v1/search?name="
#define OPENMETEO_GEO_SUFFIX    "&count=1&language=en&format=json"
#define OPENMETEO_API_BASE_URL  "http://api.open-meteo.com/v1/forecast?latitude="
#define OPENMETEO_LON_PARAM     "&longitude="
#define OPENMETEO_QUERY_SUFFIX  "&current=temperature_2m,weather_code"
#define NVS_PART_NAME           "nvs"
#define NVS_PART_NAMESPACE      "result"
#define WIFI_NAME_LEN_MAX       33         // Maximum length for Wi-Fi name (32 chars + NUL)
#define WIFI_PASSWORD_LEN_MAX   65         // Maximum length for Wi-Fi password (64 chars + NUL)
#define ALARM_ROL_LEN           10         // Maximum length for alarm time rollers
#define ALARM_TIME_LEN          30         // Maximum length for alarm time
#define TIME_INFO_LEN           16         // Maximum length for time information
#define DATE_DISPLAY_LEN        16         // DD-MMM-YYYY plus NUL
#define WEEK_INFO_LEN           20         // Maximum length for week information
#define TEMPERATURE_LEN         20         // Maximum length for temperature information
#define LOOP_DELAY_MS           500        // Delay in milliseconds for the loop
#define WIFI_RECONNECT_INTERVAL_MS 30000   // Delay between automatic Wi-Fi reconnect attempts

/* Flags employed for monitoring the status of various functions */
bool wifi_connected_flag = false;   // Flag indicating whether Wi-Fi is connected
bool wifi_connect_pending = false;
bool wifi_using_secrets = false;
uint32_t wifi_connect_started_ms = 0;
uint32_t wifi_reconnect_due_ms = 0;     // millis() when the next auto-reconnect may start
bool wifi_reconnect_disabled = false;   // Set by the disconnect (factory) action
bool wifi_list_switch = false;      // Flag indicating whether Wi-Fi list is opened
bool password_valid_flag = false;   // Flag indicating whether Wi-Fi password is being authenticated
bool alarm_flag = false;            // Flag indicating whether the alarm has been triggered

/* 12-hour (true) vs 24-hour (false) clock display, toggled by on-screen button */
bool time_format_12h = false;

/* Variables for storing WiFi credentials */
char st_wifi_name[WIFI_NAME_LEN_MAX];
char st_wifi_password[WIFI_PASSWORD_LEN_MAX];
const char *selected_wifi_name = NULL;
const char *wifi_password = NULL;

/* Variables for alarm time in hours, minutes, and seconds */
char hours_rol[ALARM_ROL_LEN];
char min_rol[ALARM_ROL_LEN];
char sec_rol[ALARM_ROL_LEN];
char init_hours_rol[ALARM_ROL_LEN];
char init_min_rol[ALARM_ROL_LEN];
char init_sec_rol[ALARM_ROL_LEN];
uint16_t init_hour_roll_set = 0;
uint16_t init_min_roll_set = 0;
uint16_t init_sec_roll_set = 0;
char alarm_time[ALARM_TIME_LEN];

/* Variables for storing WiFi network information */
lv_obj_t * wifi_list = NULL;
lv_obj_t *wifi_list_msg = NULL;
lv_obj_t *ui_LabelTimeFormat = NULL;
lv_obj_t *ui_LabelTimeAmPm = NULL;
int num_wifi = 0;
char date_string[DATE_DISPLAY_LEN] = "01-JAN-1970";
int connected_count = 0;

/* HTTP Client for making HTTP requests */
HTTPClient http;

/* Weather data source. OpenWeatherMap is used only when WEATHER_API_KEY is
 * set in secrets.h; without a key the key-free Open-Meteo service is used. */
typedef enum {
    WEATHER_PROVIDER_NONE = 0,
    WEATHER_PROVIDER_OPEN_METEO,
    WEATHER_PROVIDER_OPEN_WEATHER,
} WeatherProvider_t;

WeatherProvider_t weather_provider = WEATHER_PROVIDER_NONE;
String weather_refresh_url = "";    /* Fully built current-weather URL */

/* OpenWeatherMap API configuration */
String openweather_api_key = WEATHER_API_KEY;
String openweather_city = WEATHER_CITY;
String Weather = "";
String prev_weather = "";
String lat = "";
String lon = "";
int temperature = 999;
char temperature_buffer[TEMPERATURE_LEN];
int get_weather_count = 0;

/* SSID currently chosen in the Wi-Fi list; kept in its own buffer because the
 * LVGL list text is deleted when the list is rebuilt mid-connection. */
char pending_wifi_ssid[WIFI_NAME_LEN_MAX];

/* Variables for time and date information */
time_t now;
struct tm timeinfo;
char time_str[TIME_INFO_LEN] = "12:59:59";
char time_str_24[TIME_INFO_LEN] = "12:59:59";
char weekday_str[WEEK_INFO_LEN];

struct WeatherImg_t {
    lv_obj_t *img;
    String weather;
};

/**
 * NVS Utility Functions
 *
 * This set of functions provides a simple interface for reading and writing data to NVS memory on ESP32
 *
*/
/*  Write a string to the NVS */
void writeStrToNVS(const char* key, const char* value)
{
    nvs_handle_t handle;
    if(nvs_open_from_partition(NVS_PART_NAME, NVS_PART_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_str(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
}

/* Read a string from NVS; leave an empty string if the key does not exist. */
void readStrFromNVS(const char* key, char* nvs_str, size_t max_length)
{
    if(max_length == 0) {
        return;
    }
    nvs_str[0] = '\0';

    nvs_handle_t handle;
    if(nvs_open_from_partition(NVS_PART_NAME, NVS_PART_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    size_t length = max_length;
    if(nvs_get_str(handle, key, nvs_str, &length) != ESP_OK) {
        nvs_str[0] = '\0';
    } else {
        nvs_str[max_length - 1] = '\0';
    }
    nvs_close(handle);
}

/* Write an 8-bit integer to NVS */
void writeIntToNVS(const char* key, int8_t value)
{
    nvs_handle_t handle;
    if(nvs_open_from_partition(NVS_PART_NAME, NVS_PART_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_i8(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
}

int8_t readIntFromNVS(const char* key)
{
    int8_t nvs_int = 0;
    nvs_handle_t handle;
    if(nvs_open_from_partition(NVS_PART_NAME, NVS_PART_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return nvs_int;
    }

    nvs_get_i8(handle, key, &nvs_int);
    nvs_close(handle);
    return nvs_int;
}

/* Start a non-blocking Wi-Fi connection attempt with the given credentials. */
void startWiFiConnection(const char *ssid, const char *password, bool from_secrets)
{
    if(ssid == NULL || ssid[0] == '\0') {
        return;
    }

    selected_wifi_name = ssid;
    wifi_password = (password != NULL) ? password : "";
    wifi_using_secrets = from_secrets;
    wifi_connect_started_ms = millis();
    wifi_connect_pending = true;
    WiFi.begin(ssid, (password != NULL) ? password : "");
    Serial.printf("Starting Wi-Fi connection to %s\n", ssid);
}

/* Start a non-blocking connection using saved credentials first and secrets
 * as fallback. Keeping retries out of setup prevents the display task starving. */
void initializeNVSRead()
{
    st_wifi_name[0] = '\0';
    st_wifi_password[0] = '\0';
    if(readIntFromNVS("NVS_WifiFg") != 0) {
        readStrFromNVS("NVS_WifiNa", st_wifi_name, sizeof(st_wifi_name));
        readStrFromNVS("NVS_WifiPw", st_wifi_password, sizeof(st_wifi_password));
    }

    if(st_wifi_name[0] != '\0') {
        startWiFiConnection(st_wifi_name, st_wifi_password, false);
    } else if(WIFI_SSID[0] != '\0') {
        startWiFiConnection(WIFI_SSID, WIFI_PASSWORD, true);
    } else {
        Serial.println("Wi-Fi credentials are not configured");
    }
}

void serviceWiFiConnection()
{
    /* Periodically retry connecting after a drop or a failed attempt */
    if(!wifi_connect_pending && !wifi_connected_flag && !wifi_reconnect_disabled &&
            st_wifi_name[0] != '\0' && wifi_reconnect_due_ms != 0 &&
            (int32_t)(millis() - wifi_reconnect_due_ms) >= 0) {
        wifi_reconnect_due_ms = 0;
        Serial.printf("Reconnecting to Wi-Fi %s\n", st_wifi_name);
        startWiFiConnection(st_wifi_name, st_wifi_password, false);
    }

    if(!wifi_connect_pending) {
        return;
    }

    if(WiFi.status() == WL_CONNECTED) {
        wifi_connect_pending = false;
        wifi_connected_flag = true;
        writeIntToNVS("NVS_WifiFg", 1);
        if(wifi_using_secrets) {
            writeStrToNVS("NVS_WifiNa", selected_wifi_name);
            writeStrToNVS("NVS_WifiPw", wifi_password);
        }
        Serial.printf("Wi-Fi connected to %s, IP: %s\n", selected_wifi_name, WiFi.localIP().toString().c_str());

        /* Remember the working credentials so a later drop can reconnect */
        strlcpy(st_wifi_name, selected_wifi_name, sizeof(st_wifi_name));
        strlcpy(st_wifi_password, wifi_password, sizeof(st_wifi_password));

        if(openweather_city.length() > 0) {
            getWeather();
        }
        return;
    }

    if(millis() - wifi_connect_started_ms >= 15000) {
        wifi_connect_pending = false;
        WiFi.disconnect();
        if(!wifi_using_secrets && WIFI_SSID[0] != '\0') {
            Serial.println("Saved Wi-Fi did not connect; trying secrets.h credentials");
            startWiFiConnection(WIFI_SSID, WIFI_PASSWORD, true);
        } else {
            wifi_connected_flag = false;
            Serial.println("Wi-Fi connection failed");
            wifi_reconnect_due_ms = millis() + WIFI_RECONNECT_INTERVAL_MS;
        }
    }
}

/**
 * Weather Data and Utility Functions
 *
 * This set of functions is designed for retrieving weather data from the OpenWeatherMap API and parsing it
*/
/* Percent-encode a URL query parameter (city names may contain spaces etc.) */
String urlEncode(const String &value)
{
    String encoded;
    encoded.reserve(value.length() * 3);
    const char *hex = "0123456789ABCDEF";
    for(size_t i = 0; i < value.length(); i++) {
        char c = value.charAt(i);
        if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            encoded += '%';
            encoded += hex[(c >> 4) & 0xF];
            encoded += hex[c & 0xF];
        }
    }
    return encoded;
}

String Getlatlon(const String &api, const String &city)
{
    String url = GEO_API_BASE_URL;
    url += urlEncode(city);
    url += API_KEY_SUFFIX;
    url += urlEncode(api);
    return url;
}

String GetURL(const String &api, const String &lat, const String &lon)
{
    String url = OPENWEATHER_BASE_URL;
    url += urlEncode(lat);
    url += LAT_PARAM;
    url += urlEncode(lon);
    url += API_KEY_PARAM;
    url += urlEncode(api);
    return url;
}

void ParseWeather(const String &url)
{
    if(url.length() == 0 || WiFi.status() != WL_CONNECTED) {
        return;
    }

    DynamicJsonDocument doc(6144); //Allocate memory dynamically

    if(!http.begin(url)) {
        Serial.println("Weather skipped: invalid request URL");
        return;
    }
    http.setUserAgent("ESP32-WiFiClock/1.0");

    int httpGet = http.GET();
    if(httpGet == HTTP_CODE_OK) {
        DeserializationError error = deserializeJson(doc, http.getString());
        if(error) {
            Serial.printf("ERROR: [ParseWeather] JSON parse failed: %s\n", error.c_str());
        } else {
            const char *weather_main = doc["weather"][0]["main"];
            if(weather_main != nullptr) {
                Weather = weather_main;
            }
            float kelvin = doc["main"]["temp"] | -1.0f;
            if(kelvin > 0) {
                temperature = (int)lroundf(kelvin - 273.15f);
            }
            Serial.printf("Weather: %s\n", Weather.c_str());
            Serial.printf("temperature: %d\n", temperature);
        }
    } else {
        Serial.printf("ERROR: [ParseWeather] OpenWeather API --> HTTP code %d\n", httpGet);
    }
    http.end();
}

void Parselatlon(const String &url)
{
    if(WiFi.status() != WL_CONNECTED) {
        return;
    }

    DynamicJsonDocument doc(6144); //Allocate memory dynamically

    if(!http.begin(url)) {
        Serial.println("Geocoding skipped: invalid request URL");
        return;
    }
    http.setUserAgent("ESP32-WiFiClock/1.0");

    int httpGet = http.GET();
    if(httpGet == HTTP_CODE_OK) {
        DeserializationError error = deserializeJson(doc, http.getString());
        if(error) {
            Serial.printf("ERROR: [Parselatlon] JSON parse failed: %s\n", error.c_str());
        } else {
            lat = doc[0]["lat"].as<String>();
            lon = doc[0]["lon"].as<String>();

            Serial.printf("lat: %s\n", lat.c_str());
            Serial.printf("lon: %s\n", lon.c_str());
        }
    } else {
        Serial.printf("ERROR: [Parselatlon] OpenWeather API --> HTTP code %d\n", httpGet);
    }
    http.end();
}

void splitTime(char *timeStr, char *init_hour_rol, char *init_min_rol, char *init_sec_rol) {
    char *token = strtok(timeStr, ":");
    if (token != NULL) {
        strcpy(init_hour_rol, token);
        token = strtok(NULL, ":");
        if (token != NULL) {
            strcpy(init_min_rol, token);
            token = strtok(NULL, ":");
            if (token != NULL) {
                strcpy(init_sec_rol, token);
            } else {
                strcpy(init_sec_rol, "00");
            }
        } else {
            strcpy(init_min_rol, "00");
            strcpy(init_sec_rol, "00");
        }
    } else {
        strcpy(init_hour_rol, "00");
        strcpy(init_min_rol, "00");
        strcpy(init_sec_rol, "00");
    }
}

/* Map WMO weather interpretation codes (Open-Meteo) to the icon categories
 * handled by WeatherMod(). */
const char *wmoToWeatherName(int code)
{
    if(code == 0) return "Clear";
    if(code <= 3) return "Clouds";
    if(code == 45 || code == 48) return "Mist";
    if(code >= 51 && code <= 57) return "Drizzle";
    if((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "Rain";
    if((code >= 71 && code <= 77) || code == 85 || code == 86) return "Snow";
    if(code >= 95) return "Thunderstorm";
    return "Clouds";
}

/* Resolve the city name through the key-free Open-Meteo geocoding API. */
void ParseOpenMeteoLatlon(const String &url)
{
    if(WiFi.status() != WL_CONNECTED) {
        return;
    }

    DynamicJsonDocument doc(2048);

    if(!http.begin(url)) {
        Serial.println("Geocoding skipped: invalid request URL");
        return;
    }
    http.setUserAgent("ESP32-WiFiClock/1.0");

    int httpGet = http.GET();
    if(httpGet == HTTP_CODE_OK) {
        DeserializationError error = deserializeJson(doc, http.getString());
        if(error) {
            Serial.printf("ERROR: [ParseOpenMeteoLatlon] JSON parse failed: %s\n", error.c_str());
        } else {
            lat = doc["results"][0]["latitude"].as<String>();
            lon = doc["results"][0]["longitude"].as<String>();
            Serial.printf("lat: %s\n", lat.c_str());
            Serial.printf("lon: %s\n", lon.c_str());
        }
    } else {
        Serial.printf("ERROR: [ParseOpenMeteoLatlon] Open-Meteo geocoding --> HTTP code %d\n", httpGet);
    }
    http.end();
}

/* Fetch current temperature and WMO weather code from Open-Meteo. */
void ParseOpenMeteoWeather(const String &url)
{
    if(WiFi.status() != WL_CONNECTED) {
        return;
    }

    DynamicJsonDocument doc(2048);

    if(!http.begin(url)) {
        Serial.println("Weather skipped: invalid request URL");
        return;
    }
    http.setUserAgent("ESP32-WiFiClock/1.0");

    int httpGet = http.GET();
    if(httpGet == HTTP_CODE_OK) {
        DeserializationError error = deserializeJson(doc, http.getString());
        if(error) {
            Serial.printf("ERROR: [ParseOpenMeteoWeather] JSON parse failed: %s\n", error.c_str());
        } else {
            float tempC = doc["current"]["temperature_2m"] | -999.0f;
            if(tempC > -998.0f) {
                temperature = (int)lroundf(tempC);
            }
            int code = doc["current"]["weather_code"] | -1;
            if(code >= 0) {
                Weather = wmoToWeatherName(code);
            }
            Serial.printf("Weather: %s\n", Weather.c_str());
            Serial.printf("temperature: %d\n", temperature);
        }
    } else {
        Serial.printf("ERROR: [ParseOpenMeteoWeather] Open-Meteo --> HTTP code %d\n", httpGet);
    }
    http.end();
}

void getWeatherOpenMeteo()
{
    lat = "";
    lon = "";
    String geo_url = OPENMETEO_GEO_BASE_URL;
    geo_url += urlEncode(openweather_city);
    geo_url += OPENMETEO_GEO_SUFFIX;

    for(int retry = 0; retry < 3 && (lat.length() == 0 || lon.length() == 0); retry++) {
        ParseOpenMeteoLatlon(geo_url);
        if(lat.length() == 0 || lon.length() == 0) {
            delay(500);
        }
    }

    if(lat.length() == 0 || lon.length() == 0) {
        Serial.println("Weather disabled: could not resolve lat/lon");
        weather_refresh_url = "";
        return;
    }

    weather_refresh_url = OPENMETEO_API_BASE_URL;
    weather_refresh_url += urlEncode(lat);
    weather_refresh_url += OPENMETEO_LON_PARAM;
    weather_refresh_url += urlEncode(lon);
    weather_refresh_url += OPENMETEO_QUERY_SUFFIX;
    ParseOpenMeteoWeather(weather_refresh_url);
}

/* Fetch weather once. Provider choice: OpenWeatherMap when a key is set in
 * secrets.h, otherwise the key-free Open-Meteo service. */
void getWeather()
{
    if(openweather_city.length() == 0) {
        Serial.println("Weather disabled: set WEATHER_CITY in secrets.h");
        weather_provider = WEATHER_PROVIDER_NONE;
        return;
    }

    if(openweather_api_key.length() > 0) {
        weather_provider = WEATHER_PROVIDER_OPEN_WEATHER;

        lat = "";
        lon = "";
        String lat_lon_url = Getlatlon(openweather_api_key, openweather_city);

        for(int retry = 0; retry < 3 && (lat.length() == 0 || lon.length() == 0); retry++) {
            Parselatlon(lat_lon_url);
            if(lat.length() == 0 || lon.length() == 0) {
                delay(500);
            }
        }

        if(lat.length() == 0 || lon.length() == 0) {
            Serial.println("Weather disabled: could not resolve lat/lon");
            weather_refresh_url = "";
            return;
        }

        weather_refresh_url = GetURL(openweather_api_key, lat, lon);
        ParseWeather(weather_refresh_url);
    } else {
        weather_provider = WEATHER_PROVIDER_OPEN_METEO;
        Serial.println("No WEATHER_API_KEY set; using Open-Meteo (key-free)");
        getWeatherOpenMeteo();
    }
}

/* Periodic weather refresh, dispatched to the active provider. */
void refreshWeather()
{
    switch(weather_provider) {
    case WEATHER_PROVIDER_OPEN_WEATHER:
        ParseWeather(weather_refresh_url);
        break;
    case WEATHER_PROVIDER_OPEN_METEO:
        ParseOpenMeteoWeather(weather_refresh_url);
        break;
    default:
        break;
    }
}

/**
 * WiFi Clock User Interface Functions
 *
 * This set of functions is used to manage the user interface (UI) and interaction in a WiFi-enabled clock application
*/
void onTimerClockUpdate(lv_timer_t *timer)
{
    lv_obj_t *ui_LabelTime = (lv_obj_t *) timer->user_data;

    lv_label_set_text_fmt(ui_LabelTime, "%s", time_str);
    if(ui_LabelTimeAmPm != NULL) {
        /* Only touch the widgets when their state actually changes: every
         * unnecessary invalidation repaints the whole screen in LVGL
         * full-refresh mode and makes the RGB panel glitch. */
        if(!time_format_12h) {
            if(!lv_obj_has_flag(ui_LabelTimeAmPm, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(ui_LabelTimeAmPm, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            static char last_am_pm[3] = "";
            const char *am_pm = (timeinfo.tm_hour < 12) ? "AM" : "PM";
            if(lv_obj_has_flag(ui_LabelTimeAmPm, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_clear_flag(ui_LabelTimeAmPm, LV_OBJ_FLAG_HIDDEN);
            }
            if(strcmp(last_am_pm, am_pm) != 0) {
                lv_label_set_text(ui_LabelTimeAmPm, am_pm);
                strlcpy(last_am_pm, am_pm, sizeof(last_am_pm));
            }
        }
    }
    if(!strcmp(alarm_time, time_str_24)) {
        alarm_flag = true;
        Serial.println("alarm_flag: true");
    }

}

void onCalendarUpdate(lv_timer_t *timer)
{
    lv_obj_t *ui_Labelcalendar = (lv_obj_t *) timer->user_data;
    static char last_date[DATE_DISPLAY_LEN] = "";
    if(strcmp(last_date, date_string) != 0) {
        lv_label_set_text(ui_Labelcalendar, date_string);
        strlcpy(last_date, date_string, sizeof(last_date));
    }
}

void onWeekUpdate(lv_timer_t *timer)
{
    lv_obj_t *ui_LabelWeek = (lv_obj_t *) timer->user_data;
    static char last_week[WEEK_INFO_LEN] = "";
    if(strcmp(last_week, weekday_str) != 0) {
        lv_label_set_text(ui_LabelWeek, weekday_str);
        strlcpy(last_week, weekday_str, sizeof(last_week));
    }
}

void WeatherMod(const String &weather)
{
    WeatherImg_t weather_img[] = {
        {ui_ImageCloudy,       "Clouds"},
        {ui_ImageRain,         "Rain"},
        {ui_ImageSnow,         "Snow"},
        {ui_ImageSunny,        "Clear"},
        {ui_ImageThunderstor,  "Thunderstorm"},
        {ui_ImageDrizzle,      "Drizzle"},
        {ui_ImageAtmosphere,   "Mist"},
        {ui_ImageAtmosphere,   "Smoke"},
        {ui_ImageAtmosphere,   "Haze"},
        {ui_ImageAtmosphere,   "Dust"},
        {ui_ImageAtmosphere,   "Fog"},
        {ui_ImageAtmosphere,   "Sand"},
    };

    lvgl_port_lock(-1);

    for (int i = 0; i < sizeof(weather_img) / sizeof(WeatherImg_t); i++) {
        if (weather_img[i].weather == weather) {
            _ui_flag_modify(weather_img[i].img, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
        } else {
            _ui_flag_modify(weather_img[i].img, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
        }
    }

    lvgl_port_unlock();
}

void onWeatherUpdate(lv_timer_t *timer)
{
    if(!wifi_connected_flag){
        return;
    }

    lv_obj_t *ui_Labelweather = (lv_obj_t *) timer->user_data;

    /* Explain the placeholder instead of leaving a silent '--': without a
     * city configured the weather feature cannot fetch anything. */
    if(weather_provider == WEATHER_PROVIDER_NONE) {
        static bool no_city_shown = false;
        if(!no_city_shown) {
            lv_label_set_text(ui_Labelweather, "No city set");
            no_city_shown = true;
        }
        return;
    }

    /* Only publish non-empty weather data: overwriting the label with an
     * empty string while the first fetch is still pending would blank the
     * weather line on the clock face. */
    if(Weather.length() > 0 && prev_weather != Weather) {
        lv_label_set_text(ui_Labelweather, Weather.c_str());
        WeatherMod(Weather);
        prev_weather = Weather;
    }
}

void onTemperatureUpdate(lv_timer_t *timer)
{
    if(!wifi_connected_flag) {
        return;
    }

    lv_obj_t *ui_Labeltemperature = (lv_obj_t *) timer->user_data;
    static int last_temperature = 999;
    if(temperature != 999 && temperature != last_temperature) {
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%d℃", temperature);
        lv_label_set_text(ui_Labeltemperature, temperature_buffer);
        last_temperature = temperature;
    }
}

/**
 * Set up the UI timers for the Squareline-generated UI project.
 *
 * The timers are set to trigger the corresponding callback functions every second.
 */
void initializeUITimer()
{
    lv_timer_t *LabelTime_Wifitimer = lv_timer_create(onTimerClockUpdate, 1000, (void *) ui_LabelTime);
    onTimerClockUpdate(LabelTime_Wifitimer);
    lv_timer_t *Labelcalendar_timer = lv_timer_create(onCalendarUpdate, 1000, (void *) ui_Labelcalendar);
    onCalendarUpdate(Labelcalendar_timer);
    lv_timer_t *Labelweek_timer = lv_timer_create(onWeekUpdate, 1000, (void *) ui_Labelweek);
    onWeekUpdate(Labelweek_timer);
    lv_timer_t *Labelweather_timer = lv_timer_create(onWeatherUpdate, 1000, (void *) ui_Labelweather);
    onWeatherUpdate(Labelweather_timer);
    lv_timer_t *Labeltemperature_timer = lv_timer_create(onTemperatureUpdate, 1000, (void *) ui_Labeltemperature);
    onTemperatureUpdate(Labeltemperature_timer);
}

/**
 * Handles various UI events such as Wi-Fi list item clicks, keyboard input, alarm confirmation, factory actions, etc.
 *
 * The specific event is determined by the event code in the LVGL event structure. The function performs actions
 * based on the triggered event, such as updating flags, UI elements, and changing screens.
 */
/* Function to handle the click event of an item in the WiFi list */
void handleWifiListItemClick(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);

    if (event_code == LV_EVENT_CLICKED) {
        lvgl_port_lock(-1);
        /* Copy the SSID into a private buffer: lv_list_get_btn_text() points
         * into the LVGL list, which is deleted when the list is rebuilt. */
        const char *ssid = lv_list_get_btn_text(wifi_list, target);
        if (ssid != NULL) {
            strlcpy(pending_wifi_ssid, ssid, sizeof(pending_wifi_ssid));
            selected_wifi_name = pending_wifi_ssid;
            Serial.printf("%s\n", selected_wifi_name);
        }
        _ui_screen_change(&ui_ScreenPassword, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_ScreenPassword_screen_init);
        lvgl_port_unlock();
    }
}

/* Function to handle the click event of a button on the keyboard for entering WiFi password.
 * LVGL's keyboard sends LV_EVENT_READY (to the keyboard and its attached textarea)
 * when the Enter / new-line key is pressed; listening for that event is reliable,
 * unlike matching a hard-coded button index. */
void handleKeyboardEvent(lv_event_t *e)
{
    if(lv_event_get_code(e) != LV_EVENT_READY) {
        return;
    }

    if(password_valid_flag) {
        return;    /* A connection attempt is already in progress */
    }
    if(selected_wifi_name == NULL) {
        Serial.println("No Wi-Fi network selected");
        return;
    }
    /* Copy out of the textarea; its buffer is invalidated when cleared */
    strlcpy(st_wifi_password, lv_textarea_get_text(ui_TextPassword), sizeof(st_wifi_password));
    wifi_password = st_wifi_password;
    Serial.printf("Connecting to %s\n", selected_wifi_name);

    WiFi.begin(selected_wifi_name, wifi_password);

    password_valid_flag = true;
    Serial.println("password_valid_flag: true");

    _ui_flag_modify(ui_SpinnerLoadPassword, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    _ui_flag_modify(ui_KeyboardPassword, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
}

/* Function to handle the confirmation event for setting an alarm */
void handleAlarmConfirmation(lv_event_t * e)
{
    lv_roller_get_selected_str(ui_Rollerhour, hours_rol, sizeof(hours_rol));
    lv_roller_get_selected_str(ui_Rollerminute, min_rol, sizeof(min_rol));
    lv_roller_get_selected_str(ui_Rollersecond, sec_rol, sizeof(sec_rol));

    snprintf(alarm_time, sizeof(alarm_time), "%s:%s:%s", hours_rol, min_rol, sec_rol);
    Serial.printf("alarm_time: %s\n", alarm_time);
}

/* Function to handle the action after a factory reset is triggered */
void handleFactoryAction(lv_event_t * e)
{
    lvgl_port_lock(-1);
    _ui_flag_modify(ui_ImageWifi, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
    lvgl_port_unlock();

    http.end();
    WiFi.disconnect();

    /* Forget everything: saved credentials, pending connects and reconnects */
    wifi_connect_pending = false;
    wifi_connected_flag = false;
    wifi_reconnect_disabled = true;
    wifi_reconnect_due_ms = 0;
    st_wifi_name[0] = '\0';
    st_wifi_password[0] = '\0';
    weather_refresh_url = "";
    weather_provider = WEATHER_PROVIDER_NONE;

    writeIntToNVS("NVS_WifiFg", 0);
    writeStrToNVS("NVS_WifiNa", "");
    writeStrToNVS("NVS_WifiPw", "");

    Serial.println("Wi-Fi disconnected, saved credentials cleared");
}

/* Function to handle the button event for setting the alarm time */
void handleButtonAlarmEvent(lv_event_t * e)
{
    splitTime(time_str, init_hours_rol, init_min_rol, init_sec_rol);

    init_hour_roll_set = atoi(&init_hours_rol[0]);
    init_min_roll_set = atoi(&init_min_rol[0]);
    init_sec_roll_set = atoi(&init_sec_rol[0]);

    lv_roller_set_selected(ui_Rollerhour, init_hour_roll_set, LV_ANIM_OFF);
    lv_roller_set_selected(ui_Rollerminute, init_min_roll_set, LV_ANIM_OFF);
    lv_roller_set_selected(ui_Rollersecond, init_sec_roll_set, LV_ANIM_OFF);
}

/* Function to display the list of available WiFi networks */
void displayWifiList()
{
    Serial.println("Wifi list show:");

    lvgl_port_lock(-1);

    _ui_flag_modify(ui_SpinnerLoadWifi, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);

    if(wifi_list != NULL) {
        lv_obj_del(wifi_list);
        wifi_list = NULL;
    }
    if(wifi_list_msg != NULL) {
        lv_obj_add_flag(wifi_list_msg, LV_OBJ_FLAG_HIDDEN);
    }
    /* A rebuilt list invalidates the previously selected entry */
    selected_wifi_name = NULL;

    wifi_list = lv_list_create(ui_ScreenWifiList);
    lv_obj_set_size(wifi_list, 720, 300);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(0x1B3B5F), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(wifi_list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(wifi_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(wifi_list, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(wifi_list, &ui_font_FontPuHui20bp4, LV_PART_MAIN | LV_STATE_DEFAULT);
    for (int i = 0; i < num_wifi; i++) {
        lv_obj_t *wifi_list_Item = lv_list_add_btn(wifi_list, NULL, WiFi.SSID(i).c_str());
        lv_obj_set_style_min_height(wifi_list_Item, 56, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(wifi_list_Item, &ui_font_FontPuHui20bp4, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(wifi_list_Item, handleWifiListItemClick, LV_EVENT_ALL, NULL);
    }
    lv_obj_align(wifi_list, LV_ALIGN_BOTTOM_MID, 0, -60);

    lvgl_port_unlock();
}

/* Function to handle the validation of the entered WiFi password */
void handlePasswordValidation()
{
    connected_count++;

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("password correct: Wifi connected success");

        writeStrToNVS("NVS_WifiNa", selected_wifi_name);
        writeStrToNVS("NVS_WifiPw", wifi_password);

        wifi_connected_flag = true;
        Serial.println("wifi_connected_flag: true");
        writeIntToNVS("NVS_WifiFg", 1);

        /* Remember the working credentials so a later drop can reconnect */
        strlcpy(st_wifi_name, selected_wifi_name, sizeof(st_wifi_name));
        strlcpy(st_wifi_password, wifi_password, sizeof(st_wifi_password));

        password_valid_flag = false;
        Serial.println("password_valid_flag: false");

        connected_count = 0;

        lvgl_port_lock(-1);
        _ui_flag_modify(ui_SpinnerLoadPassword, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
        _ui_flag_modify(ui_KeyboardPassword, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
        _ui_screen_change(&ui_ScreenClock, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_ScreenClock_screen_init);
        lv_textarea_set_text(ui_TextPassword, "");
        lvgl_port_unlock();

        if(openweather_city.length() > 0) {
            getWeather();
        }
    } else if(connected_count >= 6) {
        Serial.println("password wrong: Wifi connected failed");

        wifi_connected_flag = false;
        writeIntToNVS("NVS_WifiFg", 0);

        http.end();
        WiFi.disconnect();

        password_valid_flag = false;

        connected_count = 0;

        lvgl_port_lock(-1);

        _ui_flag_modify(ui_SpinnerLoadPassword, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
        _ui_flag_modify(ui_KeyboardPassword, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
        lv_textarea_set_text(ui_TextPassword, "");

        /* Tell the user the attempt failed instead of silently re-showing
         * the keyboard. */
        static const char *ok_btn[] = {"OK", ""};
        lv_obj_t *fail_mbox = lv_msgbox_create(NULL, "Wi-Fi",
                                               "Connection failed.\nCheck the password and try again.", ok_btn, false);
        lv_obj_center(fail_mbox);

        lvgl_port_unlock();
    }
}

/* Function to handle the activation of the alarm */
void handleAlarmActivation()
{
    if(!alarm_flag) {
        return;
    }

    lvgl_port_lock(-1);

    /* NULL button list: only the built-in close (X) button is shown */
    lv_obj_t *Alarm_mbox = lv_msgbox_create(NULL, "Alarm activated", "Tap the close button to stop the alarm", NULL, true);
    lv_obj_center(Alarm_mbox);

    alarm_flag = false;
    Serial.println("alarm_flag: false");

    lvgl_port_unlock();
}

/* Hide the scanning spinner on the Wi-Fi list screen */
void hideWifiSpinner()
{
    lvgl_port_lock(-1);
    _ui_flag_modify(ui_SpinnerLoadWifi, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
    lvgl_port_unlock();
}

/* Show a status line (e.g. when no networks were found) instead of the list */
void showWifiListMessage(const char *text)
{
    lvgl_port_lock(-1);
    if(wifi_list_msg == NULL) {
        wifi_list_msg = lv_label_create(ui_ScreenWifiList);
        lv_obj_align(wifi_list_msg, LV_ALIGN_TOP_MID, 0, 120);
        lv_obj_set_style_text_color(wifi_list_msg, lv_color_hex(0xDBE2EF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(wifi_list_msg, &ui_font_FontNumber28bp4, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_label_set_text(wifi_list_msg, text);
    lv_obj_clear_flag(wifi_list_msg, LV_OBJ_FLAG_HIDDEN);
    if(wifi_list != NULL) {
        lv_obj_del(wifi_list);
        wifi_list = NULL;
    }
    _ui_flag_modify(ui_SpinnerLoadWifi, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
    lvgl_port_unlock();
}

/* Function to scan and display the list of available WiFi networks */
void scanAndDisplayWiFiList()
{
    static bool scan_pending = false;
    static uint32_t scan_started_ms = 0;

    if(!scan_pending && !wifi_list_switch) {
        return;
    }

    /* Stop following a scan once the user leaves the Wi-Fi list screen */
    lvgl_port_lock(-1);
    const bool on_wifi_list_screen = (lv_scr_act() == ui_ScreenWifiList);
    lvgl_port_unlock();
    if(!on_wifi_list_screen) {
        scan_pending = false;
        wifi_list_switch = false;
        return;
    }

    if(!scan_pending) {
        wifi_list_switch = false;
        WiFi.scanDelete();
        if(WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED) {
            Serial.println("Wi-Fi scan could not be started");
            hideWifiSpinner();
            return;
        }
        scan_pending = true;
        scan_started_ms = millis();
        return;
    }

    const int16_t scan_status = WiFi.scanComplete();
    if(scan_status == WIFI_SCAN_RUNNING) {
        if(millis() - scan_started_ms > 20000) {
            WiFi.scanDelete();
            scan_pending = false;
            Serial.println("Wi-Fi scan timed out");
            showWifiListMessage("No Wi-Fi networks found");
        }
        return;
    }
    if(scan_status == WIFI_SCAN_FAILED) {
        scan_pending = false;
        hideWifiSpinner();
        return;
    }

    scan_pending = false;
    num_wifi = scan_status;
    Serial.println("Scan done");
    if(num_wifi == 0) {
        Serial.println("no networks found");
        showWifiListMessage("No Wi-Fi networks found");
    } else {
        displayWifiList();
    }
    WiFi.scanDelete();
}

/* Function to handle the WiFi connection process */
void handleWifiConnection()
{
    if(!password_valid_flag) {
        return;
    }

    handlePasswordValidation();
}

/* Function to update weather information and current time */
void updateWeatherAndTime()
{
    if(wifi_connected_flag) {
        if(weather_provider != WEATHER_PROVIDER_NONE && WiFi.status() == WL_CONNECTED) {
            get_weather_count++;
            if(weather_refresh_url.length() > 0) {
                /* Regular refresh every ~60 s (loop runs at ~500 ms) */
                if(get_weather_count >= 120) {
                    refreshWeather();
                    get_weather_count = 0;
                }
            } else {
                /* Setup (geocoding or first fetch) failed earlier, e.g. DNS
                 * was not ready right after connect: retry every ~5 min. */
                if(get_weather_count >= 600) {
                    Serial.println("Retrying weather setup");
                    getWeather();
                    get_weather_count = 0;
                }
            }
        }

        if(WiFi.status() != WL_CONNECTED) {
            wifi_connected_flag = false;
            Serial.println("Wi-Fi connection lost");
            writeIntToNVS("NVS_WifiFg", 0);
            http.end();
            WiFi.disconnect();
            if(!wifi_reconnect_disabled && st_wifi_name[0] != '\0') {
                wifi_reconnect_due_ms = millis() + WIFI_RECONNECT_INTERVAL_MS;
            }
        }

        lvgl_port_lock(-1);
        if(lv_obj_has_flag(ui_ImageWifi, LV_OBJ_FLAG_HIDDEN)) {
            _ui_flag_modify(ui_ImageWifi, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
        }
        lvgl_port_unlock();
    } else {
        lvgl_port_lock(-1);
        if(!lv_obj_has_flag(ui_ImageWifi, LV_OBJ_FLAG_HIDDEN)) {
            _ui_flag_modify(ui_ImageWifi, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
        }
        lvgl_port_unlock();
    }

    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(time_str_24, sizeof(time_str_24), "%H:%M:%S", &timeinfo);
    if(time_format_12h) {
        strftime(time_str, sizeof(time_str), "%I:%M:%S", &timeinfo);
    } else {
        strcpy(time_str, time_str_24);
    }
    static const char * const month_abbreviations[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
    snprintf(date_string, sizeof(date_string), "%02d-%s-%04d",
             timeinfo.tm_mday, month_abbreviations[timeinfo.tm_mon], timeinfo.tm_year + 1900);
    strftime(weekday_str, sizeof(weekday_str), "%A", &timeinfo);
}

/* Position the big time label and the AM/PM suffix inside the clock card.
 * In 12-hour mode the time shifts to the left half of the card so the 96 px
 * AM/PM text fits at the right edge. Must be called with the LVGL lock held. */
void applyClockCardLayout()
{
    if(ui_LabelTime == NULL) {
        return;
    }
    if(time_format_12h && ui_LabelTimeAmPm != NULL) {
        lv_obj_align(ui_LabelTime, LV_ALIGN_LEFT_MID, 48, 0);
        lv_obj_align(ui_LabelTimeAmPm, LV_ALIGN_RIGHT_MID, -32, 0);
    } else {
        lv_obj_align(ui_LabelTime, LV_ALIGN_CENTER, 0, 0);
    }
}

/* Function to handle the button event for toggling 12/24-hour time format */
extern "C" void handleTimeFormatToggle(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    time_format_12h = !time_format_12h;
    writeIntToNVS("NVS_TimeFmt", time_format_12h ? 1 : 0);

    lvgl_port_lock(-1);
    if(ui_LabelTimeFormat != NULL) {
        lv_label_set_text(ui_LabelTimeFormat, time_format_12h ? "12H" : "24H");
    }
    applyClockCardLayout();
    lvgl_port_unlock();
}
/* Function to handle the button event for setting WiFi */
void handleButtonSetWifiEvent(lv_event_t * e)
{
    wifi_list_switch = true;
}

/* Function to handle the button event for resetting actions */
void handleButtonResetAction(lv_event_t * e)
{
    wifi_list_switch = false;
}

/* Function to handle the button event for returning to WiFi settings */
void handleButtonReturnWifiAction(lv_event_t * e)
{
    wifi_list_switch = true;
}

void setup()
{
    Serial.begin(115200);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Partition layout changed: wipe NVS and try again */
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        Serial.printf("NVS init failed: %d\n", err);
    }

    time_format_12h = (readIntFromNVS("NVS_TimeFmt") != 0);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    /* Set the timezone and start SNTP; system time is kept in sync
     * automatically (and re-synchronized hourly) once Wi-Fi is up. */
    configTzTime(TIMEZONE_OFFSET, "pool.ntp.org", "time.nist.gov");


    Serial.println("Initializing board");
    Board *board = new Board();
    board->init();

#if LVGL_PORT_AVOID_TEARING_MODE
    // When avoid tearing function is enabled, the frame buffer number should be set in the board driver
    board->getLCD()->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#endif
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd = board->getLCD();
    auto lcd_bus = lcd->getBus();
    /**
     * When using "ESP32-S3 + RGB LCD" to operate WiFi or flash, the screen may drift. We need to enable the
     * `RGB LCD Bounce Buffer + XIP on PSRAM` feature to avoid this issue.
     * See FAQ section in README.md for more information.
     */
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 20);
    }
#endif
    if(!board->begin()) {
        Serial.println("ERROR: board init failed, restarting");
        delay(3000);
        ESP.restart();
    }

    Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());

    Serial.println("Creating UI");
    /* Lock the mutex due to the LVGL APIs are not thread-safe */
    lvgl_port_lock(-1);

    /* Initialize LVGL UI generated by Squareline */
    ui_init();
    initializeUITimer();

    /* Release the mutex */
    lvgl_port_unlock();

    initializeNVSRead();
}

void loop()
{
    serviceWiFiConnection();
    scanAndDisplayWiFiList();

    handleWifiConnection();

    handleAlarmActivation();

    updateWeatherAndTime();

    delay(LOOP_DELAY_MS);
}
