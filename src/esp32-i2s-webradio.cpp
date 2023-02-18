// Based on https://github.com/schreibfaul1/ESP32-audioI2S/blob/master/examples/Simple_WiFi_Radio/Simple_WiFi_Radio_IR.ino

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>

#include "Audio.h" // https://github.com/schreibfaul1/ESP32-audioI2S

#include <IRremoteESP8266.h> // https://github.com/crankyoldgit/IRremoteESP8266
#include <IRutils.h>

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

#include <ESPmDNS.h>

#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/rtc.h"
#endif

#include <esp_sleep.h>

#include <esp_attr.h>

#include <WebServer.h>
#include <HTTPUpdateServer.h>

#include <HTTPClient.h>
#include <ArduinoJson.h>

WiFiServer telnetServer(23);
WiFiClient serverClient;

WiFiManager wifiManager;

// HTTP Server
WebServer server(80);

// OTA update via web interface upload
HTTPUpdateServer httpUpdater;

// I2S pins for DAC
#if PLATFORMIO_BOARD == esp32cam
#define I2S_DOUT      2
#define I2S_BCLK      14
#define I2S_LRC       15
#else
#define I2S_DOUT      25
#define I2S_BCLK      27
#define I2S_LRC       26
#endif

// IR pin
#ifdef ESP32C3
const uint16_t kRecvPin = 10; // 14 on a ESP32-C3 causes a boot loop
#else
const uint16_t kRecvPin = 14;
#endif

IRrecv irrecv(kRecvPin);
decode_results results;

Preferences preferences;
Audio audio;

String stations[128];
String titles[128];

//some global variables

uint8_t max_volume   = 21;
uint8_t min_volume   = 2;
uint8_t max_stations = 0;   //will be set later
uint8_t cur_station  = 0;   //current station(nr), will be set later
uint8_t cur_volume   = 0;   //will be set from stored preferences
int8_t  cur_btn      =-1;   //current button (, -1 means idle)

String lines[128];
int line_count = 0;

String last_ir_command;

String device_name = F("WebRadio");

bool playing_a_station = false; // True if we are playing a station, false if we are playing a URL, needed for caching station URLs

bool use_deep_sleep = false;
unsigned long sleep_timer_begin_time = 0; // Time at which the sleep should begin in milliseconds from the start of the program; automatically set by deepSleep()
unsigned int deep_sleep_millis = 1 * 60 * 1000 ; // After this many minutes, the ESP32 will go to deep sleep; can be set by the user via the web interface

enum action{VOLUME_UP=0, VOLUME_DOWN=1, STATION_UP=2, STATION_DOWN=3};
enum staus {RELEASED=0, PRESSED=1};

inline void println(const String line) {
  if (serverClient && serverClient.connected())     // send data to telnet client if connected
    serverClient.println(line);
    Serial.println(line);
}

inline void print(const String line) {
  if (serverClient && serverClient.connected())     // send data to telnet client if connected
    serverClient.print(line);
    Serial.print(line);
}

String getFirstEpisode(String id) {

  println("Getting episodes for: " + id);
  String url = "https://pod.link/%id%.json?offset=0&limit=1";
  url.replace("%id%", id);

  HTTPClient http;
  http.begin(url);
  println(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    println(F("Error making HTTP request"));
    // return "";
  }

    // https://arduinojson.org/v6/assistant/

    StaticJsonDocument<64> filter;
    filter["episodes"][0]["enclosure"]["url"] = true;

    StaticJsonDocument<512> doc;

    DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return "";
    }

    const char* episodes_0_enclosure_url = doc["episodes"][0]["enclosure"]["url"];
    println(String("Episode URL: ") + episodes_0_enclosure_url);

    return String(episodes_0_enclosure_url);
  
}

String getOnePodcast(String query) {

  println("Searching for podcast: " + query);
  String url = "https://pod.link/search?query=";
  // Make a new string with the query URL encoded
    String quoted_query = "";
    for (int i = 0; i < query.length(); i++) {
        if (query[i] == ' ') {
            quoted_query += '+';
        } else if (isalnum(query[i])) {
            quoted_query += query[i];
        } else {
            quoted_query += '%';
            if (query[i] < 16) {
                quoted_query += '0';
            }
            quoted_query += String(query[i], HEX);
        }
    }
  url += quoted_query;

  HTTPClient http;
  http.begin(url);
  println(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    println(F("Error making HTTP request"));
    // return "";
  }

    // https://arduinojson.org/v6/assistant/

    StaticJsonDocument<32> filter;
    filter[0]["id"] = true;

    StaticJsonDocument<2048> doc;

    DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return "";
    }

    for (JsonObject item : doc.as<JsonArray>()) {
        long id = item["id"]; 
        println(String("Podcast ID: ") + id);
        return String(id);
    }

    return "";
  
}

void handleRoot() {
  println(F("/ requested"));
  // Simple html page with buttons to control the radio
  String html = "<!DOCTYPE html><html><head><title>" + device_name + "</title></head><body><center><h1>" + device_name + "</h1>\n";
  html += "<script>\n";
  html += "function send(url) { var xhttp = new XMLHttpRequest(); xhttp.open('GET', url, true); xhttp.send(); }\n";
  html += "function play(url) { var xhttp = new XMLHttpRequest(); xhttp.open('POST', '/play_url', true); xhttp.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded'); xhttp.send('url=' + encodeURIComponent(url)); }\n";
  html += "function play_station_id(station_id) { var xhttp = new XMLHttpRequest(); xhttp.open('POST', '/play_station_id', true); xhttp.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded'); xhttp.send('station_id=' + encodeURIComponent(station_id)); }\n";
  html += "</script>\n";
  html += "<p><input type='text' id='urlInput' size='50'>\n";
  html += "<button onclick='play(document.getElementById(\"urlInput\").value)'>Play URL</button></p>\n";
  html += "<button onclick='send(\"/volume_up\")'>Volume up</button> ";
  html += "<button onclick='send(\"/volume_down\")'>Volume down</button>&nbsp;&nbsp;&nbsp;\n";
  html += "<button onclick='send(\"/station_up\")'>Station up</button> ";
  html += "<button onclick='send(\"/station_down\")'>Station down</button>&nbsp;&nbsp;&nbsp;\n";
  html += "<button onclick='send(\"/play\")'>Play</button> ";
  html += "<button onclick='send(\"/pause\")'>Pause</button> ";
  html += "<button onclick='send(\"/stop\")'>Stop</button>&nbsp;&nbsp;&nbsp;\n";
  html += "<button onclick='send(\"/off\")'>Off</button>\n";
  html += "<button onclick='send(\"/reboot\")'>Reboot</button>\n";
  html += "<p>\n";
    for (int i = 0; i < max_stations; i++) {
        html += "<button onclick='play_station_id(\"" + String(i) + "\")' url=\"" + stations[i] + "\">";
        html += titles[i].substring(3);
        html += "</button>\n";
    }
  html += "</p>";
  html += "<p><a href='/config'>Configuration</a> | <a href='/update'>Update (reboot first!)</a></p>";
#if defined(GIT_IDENT)
    html += "<p>" + String(GIT_IDENT) + "</p>";
#endif

  html += "</center></body></html>";
  server.send(200, "text/html", html);
}

void updateSleepTime() {
  // Call this function whenever the sleep time should start over again
  println("Prolonging the sleep time");
  sleep_timer_begin_time = millis() + (deep_sleep_millis);
}

void deepSleep(int minutes) {
  deep_sleep_millis = minutes * 60 * 1000;
  println("Entering deep sleep in " + String(minutes) + " minutes");
  updateSleepTime();
  use_deep_sleep = true;
}

void parseConfigurationData() {

    // Retrieve the stored multi-line string from NVS
    String storedData = preferences.getString("data", "");
    println("storedData:");
    println(storedData);

    for (int i = 0; i < storedData.length(); i++) {
        if (storedData[i] == '\n') {
            line_count++;
        } else {
            lines[line_count] += storedData[i];
        }
    }
    print("line_count: ");
    println(String(line_count));
    
    int stationCount = 0;
    int b; // bass
    int m; // midtones
    int t; // treble

    for (int i = 0; i <= line_count; i++) {
        if (lines[i].startsWith("station ")) {
            print("Station: ");
            String station = lines[i].substring(8);
            station.trim();
            println(station);
            stations[stationCount] = station;
            stationCount++;
        }
        else if (lines[i].startsWith("title ")) {
            print("Title: ");
            String title = lines[i].substring(6);
            title.trim();
            println(title);
            titles[stationCount] = title;
        }
        else if (lines[i].startsWith("sleep ")) {
            print("Sleep: ");
            String minutes = lines[i].substring(6);
            minutes.trim();
            println(minutes);
            int m = minutes.toInt();
            if (m > 0) {
              deepSleep(m);
              use_deep_sleep = true;
            }
        }
        else if (lines[i].startsWith("name ")) {
                        print("Name: ");
            String name = lines[i].substring(5);
            name.trim();
            println(name);
            device_name = name;
        }
        else if (lines[i].startsWith("bass ")) {    
            print("Bass: ");
            String bass = lines[i].substring(5);
            bass.trim();
            println(bass);
            b = bass.toInt();
        }
        else if (lines[i].startsWith("midtones ")) {
            print("Midtones: ");
            String midtones = lines[i].substring(9);
            midtones.trim();
            println(midtones);
            m = midtones.toInt();
        }
        else if (lines[i].startsWith("treble ")) {
            print("Treble: ");
            String treble = lines[i].substring(7);
            treble.trim();
            println(treble);
            t = treble.toInt();
        }

        // Set equalizer
        // int8_t gainLowPass, int8_t gainBandPass, int8_t gainHighPass
        // values can be between -40 ... +6 (dB)
        if(b >= -40 && b <= 6 && t >= -40 && t <= 6) {
            println("Setting equalizer: Bass " + String(b) + ", treble " + String(t));
            audio.setTone(b, m, t);
        }
                        
    }
    max_stations = stationCount;
    print("max_stations: ");
    println(String(max_stations));
}

void handleConfig() {
  println("/config requested");
  String html = "<html><body>";
  html += "<form action='/update_config' method='post'>";
  html += "<textarea rows='40' cols='80' name='multiline'>";
  html += preferences.getString("data", "");
  html += "</textarea><br>";
  html += "<input type='submit' value='Update'>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

void updateConfig() {
  println(F("/update_config requested"));
  String data = server.arg("multiline");

  // Store the multi-line string in NVS
  preferences.putString("data", data);

  static const char successResponse[] PROGMEM = 
  "<META http-equiv=\"refresh\" content=\"1;URL=/\">Configuration updated! Rebooting...";
  server.send(200, "text/html", successResponse);
  esp_restart();
}

void write_volume(uint8_t vol){
    String svol = String(vol);
    if(svol.length()<2) svol = "0"+svol;
    println(svol);
}

void write_stationName(String sName){
    println(sName);
}

void write_streamTitle(String sTitle){
    println(sTitle);
}

void volume_up(){
    if(cur_volume < max_volume){
        cur_volume++;
        write_volume(cur_volume);
        audio.setVolume(cur_volume);
        preferences.putShort("volume", cur_volume);} // store the current volume in nvs
        println(String(cur_volume));
        server.send(200, "text/html", "Volume: "+String(cur_volume)+"");
}

void volume_down(){
    if(cur_volume > min_volume+1){
        cur_volume-- ;
        write_volume(cur_volume);
        audio.setVolume(cur_volume);
        preferences.putShort("volume", cur_volume);} // store the current volume in nvs
        println(String(cur_volume));
        server.send(200, "text/html", "Volume: "+String(cur_volume)+"");
}

void stop(){
    audio.stopSong();
    playing_a_station = false;
    server.send(200, "text/html", "Stopped");
}

void pauseResume(){
    audio.pauseResume();
    server.send(200, "text/html", "Paused");
}

void play(){
    if (! audio.isRunning()) {
        audio.pauseResume();
    }
    server.send(200, "text/html", "Playing");
}

void play_station(){
    preferences.putShort("station", cur_station); // Store the current station in nvs
    preferences.putString("url", "");
    println("Playing station id: "+String(cur_station));
    // Get the first 2 characters of the station name
    String station_name_language = titles[cur_station].substring(0, 2);
    // Get the station name but without the first 3 characters
    String station_name = titles[cur_station].substring(3);
    playing_a_station = false;
    audio.connecttospeech(station_name.c_str(), station_name_language.c_str());
    // Wait for the speech to finish
    while(audio.isRunning()){
        loop();
    }
    println("Speech finished");
    playing_a_station = true;

    String url = stations[cur_station];
    // If URL does not start with http, then search for a podcast
    if (!url.startsWith("http")) {
        String onePodcastID = getOnePodcast(url);
        println(onePodcastID);
        url = getFirstEpisode(onePodcastID);
        println(url);
    }
    if(url == ""){
        println("No URL found");
        stop();
        server.send(404, "text/html", "No URL found");
        return;
    }
    audio.connecttohost(url.c_str());
    print("Requested to play: ");
    println(stations[cur_station].c_str());
    server.send(200, "text/html", "Station: "+String(cur_station)+"");
}

void play_station_id(){
    String station_id = server.arg("station_id");
    cur_station = station_id.toInt();
    server.send(200, "text/html", "Playing station ID: "+station_id);  
    play_station();
}

void station_down(){
    if(cur_station == 0){
        cur_station = max_stations-1;
    } else {
        cur_station--;
    }
        play_station();
}

void station_up(){
    if(cur_station == max_stations-1){
        cur_station = 0;
    } else {
        cur_station++;
    }
        play_station();
}

void play_url(){
    if (server.method() == HTTP_POST) {
        println(F("/play_url requested"));
        String url = server.arg("url");

        // If URL does not start with http, then search for a podcast
        if (!url.startsWith("http")) {
            String onePodcastID = getOnePodcast(url);
            println(onePodcastID);
            url = getFirstEpisode(onePodcastID);
            println(url);
        }

        preferences.putString("url", url);
        playing_a_station = false;
        audio.connecttohost(url.c_str());
        print("Requested to play: ");
        println(url);
        server.send(200, "text/html", "Playing URL: "+url);      
    }
}

String lookupIrCode(String irCode) {
    println("Looking up IR code: "+irCode);

    // Retrieve the stored multi-line string from NVS
    String storedData = preferences.getString("data", "");

    for (int i = 0; i <= line_count; i++) {
        if (lines[i].startsWith(irCode)) {
            String command = lines[i].substring(irCode.length()+1);
            command.trim(); // Strip any amount of whitespace at the end
            print("Found IR command: ");
            println(command);

            return(command);
        }
    }
    return("");
}

void loopTelnet() {
  if (telnetServer.hasClient() && (!serverClient || !serverClient.connected())) {
    if (serverClient)
      serverClient.stop();
    serverClient = telnetServer.available();
    serverClient.flush();  // clear input buffer, else you get strange characters
  }
}

void off() {

    // According to the Fluke 17B+, this draws 14 mA at 5V (on the ESP32-PICO-KIT V4)
    // Power (W) = Current (A) * Voltage (V) = 0.014 A * 5 V = 0.07 W
    // Energy (kWh) = Power (W) * Time (hours) / 1000 = 0.07 W * 8760 hours/year / 1000 = 0.62 kWh/year
    // Cost = Energy (kWh) * Cost per kWh = 0.62 kWh * $0.50/kWh = $0.31/year
    // Assuming 80% efficiency of the power supply, the cost is $0.39/year
    //
    // So while this is not the most efficient way to "sleep" the ESP32, it is not terribly bad either.
    // If we can find a way to use deep sleep, the module would draw 5 mA instead of 14 mA
    // (~ one third of the power consumption)
    
    // Go to standby; TODO: Use deep sleep instead; how?
    println("Going to standby mode");

    // Stop the audio
    audio.stopSong();
    audio.~Audio();

    // Stop advertising the web server
    MDNS.end();
    Serial.println("MDNS disabled");

    // Switch off the WLAN
    esp_wifi_stop();
    WiFi.mode(WIFI_OFF);
    Serial.println("WLAN disabled");

    // NOTE: If IR is not needed, or if some clumsyness for waking it up via IR is acceptable,
    // then we should use deep sleep instead; see the code as it was before the commit
    // that added this text; especially at the beginning of setup()

    // Clock down the CPU to the minimal frequency allowed on the ESP32
    Serial.println("Setting CPU frequency to 10 MHz");
    delay(100);

    setCpuFrequencyMhz(10);

    delay(100);

    // Wait for an IR signal
    while (true) {
        if (irrecv.decode(&results)) {

            // Lookup the IR code
            char buffer[16];
            sprintf(buffer, "0x%08X", results.value);
            // Convert buffer to String
            String hexstring = String(buffer);
            String command = lookupIrCode(hexstring);
            if (command == "OFF") {
                break;
            }
            irrecv.resume(); // Receive the next value
        }
        delay(100);
    }

    // Reboot
    ESP.restart();
}

void handleIrCommand(String command) {

    updateSleepTime();

    if (command.compareTo("REPEAT") == 0) {
            // Repeat the last command
            print("Last command: ");
            println(last_ir_command);
            if (last_ir_command.compareTo("VOLUME_UP") == 0 || last_ir_command.compareTo("VOLUME_DOWN") == 0) {
                print("Repeating");
                handleIrCommand(last_ir_command);
            }
            }
            else if (command.compareTo("VOLUME_UP") == 0) {
                println("Volume up");
                volume_up();
            }
            else if (command.compareTo("OFF") == 0) {
                println("Off");
                off();
            }
            else if (command.compareTo("VOLUME_DOWN") == 0) {
                println("Volume down");
                volume_down();
            }
            else if (command.compareTo("STATION_UP") == 0) {
                println("Station up");
                station_up();
            }
            else if (command.compareTo("STATION_DOWN") == 0) {
                println("Station down");
                station_down();
            } else if (command.compareTo("SLEEP_TIMER") == 0) {
                println("Sleep timer");
                deepSleep(1);
            } else if (command.compareTo("1") == 0) {
                if (max_stations >= 1) {
                    cur_station = 0;
                    play_station();
                }
            } else if (command.compareTo("2") == 0) {
                if (max_stations >= 2) {
                    cur_station = 1;
                    play_station();
                }
            } else if (command.compareTo("3") == 0) {
                if (max_stations >= 3) {
                    cur_station = 2;
                    play_station();
                }
            } else if (command.compareTo("4") == 0) {
                if (max_stations >= 4) {
                    cur_station = 3;
                    play_station();
                }
            } else if (command.compareTo("5") == 0) {
                if (max_stations >= 5) {
                    cur_station = 4;
                    play_station();
                }
            } else if (command.compareTo("6") == 0) {
                if (max_stations >= 6) {
                    cur_station = 5;
                    play_station();
                }
            } else if (command.compareTo("7") == 0) {
                if (max_stations >= 7) {
                    cur_station = 6;
                    play_station();
                }
            } else if (command.compareTo("8") == 0) {
                if (max_stations >= 8) {
                    cur_station = 7;
                    play_station();
                }
            } else if (command.compareTo("9") == 0) {
                if (max_stations >= 9) {
                    cur_station = 8;
                    play_station();
                }
            }
            if (command.compareTo("REPEAT") != 0) {
                last_ir_command = command;
            }
            if (command.compareTo("PLAY") == 0) {
                play();
            }
            if (command.compareTo("PAUSE") == 0) {
                pauseResume();
            }
            if (command.compareTo("STOP") == 0) {
                audio.stopSong();
            }

}

void handleIrCode(String irCode) {
    String command = lookupIrCode(irCode);
    if (command.length() > 0) {
        handleIrCommand(command);
    }
}

//**************************************************************************************************
//                                           S E T U P                                             *
//**************************************************************************************************
void setup() {

    Serial.begin(115200);
 
    preferences.begin("WebRadio", false);  // instance of preferences for defaults (station, volume ...)
    if(preferences.getShort("volume", 1000) == 1000){ // if that: preferences was never been initialized
        preferences.putShort("volume", 10);
        preferences.putShort("station", 0);
    }
    else{ // get the stored values
        cur_station = preferences.getShort("station");
        cur_volume = preferences.getShort("volume");
    }

    wifiManager.autoConnect();
    
    parseConfigurationData();
    print(F("Number of stations: ")); println(String(max_stations));
    print(F("Current station: ")); println(String(cur_station));
    print(F("Current volume: ")); println(String(cur_volume));
    
    while (WiFi.status() != WL_CONNECTED) {delay(1500); print(".");}
    log_i("Connected to %s", WiFi.SSID().c_str());

    if (!MDNS.begin(device_name.c_str())) {
        println(F("Error setting up MDNS responder!"));
        while(1) {
            delay(1000);
        }
    }
    println(F("mDNS responder started"));

    // Add services to MDNS-SD
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("telnet", "tcp", 23);

    // Start the telnet server
    telnetServer.begin();
    telnetServer.setNoDelay(true);

    // Start the HTTP server
    // with callback functions to handle requests
    server.on("/", handleRoot);
    server.on("/off", off);
    server.on("/reboot", esp_restart);
    server.on("/volume_up", volume_up);
    server.on("/volume_down", volume_down);
    server.on("/station_up", station_up);
    server.on("/station_down", station_down);
    server.on("/play_station_id", play_station_id);
    server.on("/play_url", play_url);
    server.on("/play", play);
    server.on("/pause", pauseResume);
    server.on("/stop", stop);
    server.on("/config", handleConfig);
    server.on("/update_config", updateConfig);

    httpUpdater.setup(&server);
    println("HTTPUpdateServer ready! Open /update in your browser");

    server.begin();
    print(F("HTTP server started, listening on IP ")), println(WiFi.localIP().toString());

    print(F("IR pin ")); println(String(kRecvPin));
    irrecv.enableIRIn();  // Start the receiver

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(cur_volume); // 0...21

    int freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    int freeExternal = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    if (freeExternal > 0) {
        println(F("PSRAM detected"));
    } else {
        println(F("PSRAM not detected"));
        // Increase the buffer size of the built-in RAM buffer; this seem to help against stuttering
        println(F("Increasing buffer size"));
        audio.setBufsize(15000, 0);
    }

    if (cur_station >= max_stations) {
        cur_station = 0;
    }
    
    write_volume(cur_volume);

    String url = preferences.getString("url", "");

    // Check if url is empty, in which case we are playing the last played station
    
    if (url.length() == 0) {
        url = stations[cur_station];
        play_station();
        println(F("Playing last played station: ")); print (stations[cur_station]);
    } else {
        print(F("Playing last played url: ")); println(url);
        playing_a_station = false;
        audio.connecttohost(url.c_str());
    }

}

//**************************************************************************************************
//                                            L O O P                                              *
//**************************************************************************************************
void loop()
{
    audio.loop();

    // listen for web requests
    server.handleClient();

    // Handle IR input
    if (irrecv.decode(&results)) {
        // serialPrintUint64(results.value, HEX);
        char buffer[16];
        sprintf(buffer, "0x%08X", results.value);
        // Convert buffer to String
        String hexstring = String(buffer);
        print(F("IR Code: ")); println(hexstring);
        irrecv.resume(); // Receive the next value
        handleIrCode(hexstring);
    }

    // See if time to sleep has arrived
    if( use_deep_sleep == true && millis() >= sleep_timer_begin_time) {
        off();
    }

    loopTelnet();

}
//**************************************************************************************************
//                                           E V E N T S                                           *
//**************************************************************************************************
void audio_info(const char *info){
    print(F("audio_info: ")); println(info);
}
void audio_showstation(const char *info){
    write_stationName(String(info));
}
void audio_showstreamtitle(const char *info){
    String sinfo=String(info);
    sinfo.replace("|", "\n");
    write_streamTitle(sinfo);
}

void audio_id3data(const char *info){  //id3 metadata
    print(F("id3data     ")); println(info);
}
void audio_eof_stream(const char* info){ // The webstream comes to an end
    print(F("end of stream:      ")); println(info);
}
void audio_bitrate(const char *info){
    print(F("bitrate     ")); println(info);
}
void audio_lasthost(const char *info){  //stream URL played
    print(F("lasthost    ")); println(info);
    if(playing_a_station == true) {
        // Cache the fully resolved URL so that we can play it again faster
        print(F("Caching station URL for station id: ")); println(String(cur_station));
        print(F("Before caching station URL: ")); println(stations[cur_station]);
        stations[cur_station] = String(info);
        // TODO: Cache this to a location that survives a reboot once we have a way to refresh it periodically
        print(F("After caching station URL: ")); println(stations[cur_station]);
    }
}
void audio_codec(const char *info){
    print(F("codec       ")); println(info);
}
void audio_commercial(const char *info){  //duration in sec
    print(F("commercial  ")); println(info);
}
void audio_icyurl(const char *info){  //homepage
    print(F("icyurl      ")); println(info);
}
void audio_lastmodified(const char *info){  //UTC time of last modification of stream
    print(F("lastmodif   ")); println(info);
}
void audio_newstation(const char *info){
    print(F("newstation  ")); println(info);
}
void audio_eof_speech(const char *info){
    print(F("eof_speech  ")); println(info);
}
void audio_file_mp3(const char *info){
    print(F("file_mp3    ")); println(info);
}
void audio_file_aac(const char *info){
    print(F("file_aac    ")); println(info);
}
void audio_file_aac_adts(const char *info){
    print(F("file_aac_adts")); println(info);
}
void audio_file_opus(const char *info){
    print(F("file_opus   ")); println(info);
}
void audio_file_vorbis(const char *info){
    print(F("file_vorbis ")); println(info);
}
void audio_file_flac(const char *info){
    print(F("file_flac   ")); println(info);
}
void audio_file_wav(const char *info){
    print(F("file_wav    ")); println(info);
}
void audio_file_wma(const char *info){
    print(F("file_wma    ")); println(info);
}
void audio_file_raw(const char *info){
    print(F("file_raw    ")); println(info);
}
void audio_file_unknown(const char *info){
    print(F("file_unknown")); println(info);
}
void audio_streamtype(const char *info){
    print(F("streamtype  ")); println(info);
}
void audio_streamurl(const char *info){
    print(F("streamurl   ")); println(info);
}
void audio_stationcount(const char *info){
    print(F("stationcount")); println(info);
}
void audio_stationname(const char *info){
    print(F("stationname ")); println(info);
}
void audio_stationlist(const char *info){
    print(F("stationlist ")); println(info);
}
void audio_stationlistend(const char *info){
    print(F("stationlistend")); println(info);
}
void audio_filesize(const char *info){
    print(F("filesize    ")); println(info);
}
void audio_filecount(const char *info){
    print(F("filecount   ")); println(info);
}
void audio_filelist(const char *info){
    print(F("filelist    ")); println(info);
}
void audio_filelistend(const char *info){
    print(F("filelistend ")); println(info);
}
void audio_playlisttitle(const char *info){
    print(F("playlisttitle")); println(info);
}
void audio_playlisturl(const char *info){
    print(F("playlisturl ")); println(info);
}
void audio_playlistshuffled(const char *info){
    print(F("playlistshuffled")); println(info);
}
void audio_playlistrepeat(const char *info){
    print(F("playlistrepeat")); println(info);
}
void audio_playlistcount(const char *info){
    print(F("playlistcount")); println(info);
}
void audio_playlist(const char *info){
    print(F("playlist    ")); println(info);
}
void audio_playlistend(const char *info){
    print(F("playlistend ")); println(info);
}