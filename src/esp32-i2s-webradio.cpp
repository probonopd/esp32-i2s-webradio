// Based on https://github.com/schreibfaul1/ESP32-audioI2S/blob/master/examples/Simple_WiFi_Radio/Simple_WiFi_Radio_IR.ino

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>

#include "Audio.h" // https://github.com/schreibfaul1/ESP32-audioI2S

#include <IRremoteESP8266.h> // https://github.com/crankyoldgit/IRremoteESP8266
#include <IRutils.h>

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

#include <ESPmDNS.h>

#include "esp32/rom/rtc.h"

#include <esp_sleep.h>

WiFiManager wifiManager;

// HTTP Server
#include <WebServer.h>
WebServer server(80);

// I2S pins for DAC
#define I2S_DOUT      25
#define I2S_BCLK      27
#define I2S_LRC       26

// IR pin
#define kRecvPin        37

Preferences preferences;
Audio audio;

IRrecv irrecv(kRecvPin);
decode_results results;

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

bool use_deep_sleep = false;
unsigned long sleep_timer_begin_time = 0; // Time at which the sleep should begin in milliseconds from the start of the program; automatically set by deepSleep()
unsigned int deep_sleep_millis = 1 * 60 * 1000 ; // After this many minutes, the ESP32 will go to deep sleep; can be set by the user via the web interface

enum action{VOLUME_UP=0, VOLUME_DOWN=1, STATION_UP=2, STATION_DOWN=3};
enum staus {RELEASED=0, PRESSED=1};

void handleRoot() {
  Serial.println("/ requested");
  // Simple html page with buttons to control the radio
  String MAIN_page = "<!DOCTYPE html><html><head><title>WebRadio</title></head><body><center><h1>WebRadio</h1>";
  MAIN_page += "<script>";
  MAIN_page += "function send(url) { var xhttp = new XMLHttpRequest(); xhttp.open('GET', url, true); xhttp.send(); }";
  MAIN_page += "function play(url) { var xhttp = new XMLHttpRequest(); xhttp.open('POST', '/play', true); xhttp.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded'); xhttp.send('url=' + encodeURIComponent(url)); }";
  MAIN_page += "</script>";
  MAIN_page += "<p><input type='text' id='urlInput'>";
  MAIN_page += "<button onclick='play(document.getElementById(\"urlInput\").value)'>Play URL</button></p>";
  MAIN_page += "<button onclick='send(\"/volume_up\")'>Volume up</button>";
  MAIN_page += "<button onclick='send(\"/volume_down\")'>Volume down</button>&nbsp;&nbsp;&nbsp;";
  MAIN_page += "<button onclick='send(\"/station_up\")'>Station up</button>";
  MAIN_page += "<button onclick='send(\"/station_down\")'>Station down</button>";
  MAIN_page += "</center></body></html>";
  server.send(200, "text/html", MAIN_page);
}

void updateSleepTime() {
  // Call this function whenever the sleep time should start over again
  Serial.println("Prolonging the sleep time");
  sleep_timer_begin_time = millis() + (deep_sleep_millis);
}

void deepSleep(int minutes) {
  deep_sleep_millis = minutes * 60 * 1000;
  Serial.println("Entering deep sleep in " + String(minutes) + " minutes");
  updateSleepTime();
  use_deep_sleep = true;
}

void parseConfigurationData() {

    use_deep_sleep = false;

    // Retrieve the stored multi-line string from NVS
    String storedData = preferences.getString("data", "");
    Serial.println("storedData:");
    Serial.println(storedData);

    for (int i = 0; i < storedData.length(); i++) {
        if (storedData[i] == '\n') {
            line_count++;
        } else {
            lines[line_count] += storedData[i];
        }
    }
    Serial.print("line_count: ");
    Serial.println(line_count);
    
    int stationCount = 0;
    for (int i = 0; i <= line_count; i++) {
        if (lines[i].startsWith("station ")) {
            Serial.print("Station: ");
            String station = lines[i].substring(8);
            station.trim();
            Serial.println(station);
            stations[stationCount] = station;
            stationCount++;
        }
        else if (lines[i].startsWith("title ")) {
            Serial.print("Title: ");
            String title = lines[i].substring(6);
            title.trim();
            Serial.println(title);
            titles[stationCount] = title;
        }
        else if (lines[i].startsWith("sleep ")) {
            Serial.print("Sleep: ");
            String minutes = lines[i].substring(6);
            minutes.trim();
            Serial.println(minutes);
            int m = minutes.toInt();
            if (m > 0) {
              deepSleep(m);
              use_deep_sleep = true;
            }
        }
    }
    max_stations = stationCount;
    Serial.print("max_stations: ");
    Serial.println(max_stations);
}

void handleConfig() {
  Serial.println("/config requested");
  String html = "<html><body>";

     html += "<ol>"; 
    for (int i = 0; i < max_stations; i++) {
        html += "<li>";
        html += titles[i];
        html += stations[i];
        html += "</li>\n";
    }
    html += "</ol>";

  html += "<form action='/update' method='post'>";
  html += "<textarea rows='40' cols='80' name='multiline'>";
  html += preferences.getString("data", "");
  html += "</textarea><br>";
  html += "<input type='submit' value='Update'>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

void handleUpdate() {
  Serial.println("/update requested");
  String data = server.arg("multiline");

  // Store the multi-line string in NVS
  preferences.putString("data", data);

  parseConfigurationData();

  // Redirect back to the referrer page

    server.sendHeader("Location", String("/config"), true);
    server.send(302, "text/plain", "");

}

void write_stationNr(uint8_t nr){
    String snr = String(nr);
    if(snr.length()<2) snr = "0"+snr;
    Serial.println(snr);
}

void write_volume(uint8_t vol){
    String svol = String(vol);
    if(svol.length()<2) svol = "0"+svol;
    Serial.println(svol);
}

void write_stationName(String sName){
    Serial.println(sName);
}

void write_streamTitle(String sTitle){
    Serial.println(sTitle);
}

void volume_up(){
    if(cur_volume < max_volume){
        cur_volume++;
        write_volume(cur_volume);
        audio.setVolume(cur_volume);
        preferences.putShort("volume", cur_volume);} // store the current volume in nvs
        Serial.println(cur_volume);
        server.send(200, "text/html", "Volume: "+String(cur_volume)+"");
}

void volume_down(){
    if(cur_volume > min_volume+1){
        cur_volume-- ;
        write_volume(cur_volume);
        audio.setVolume(cur_volume);
        preferences.putShort("volume", cur_volume);} // store the current volume in nvs
        Serial.println(cur_volume);
        server.send(200, "text/html", "Volume: "+String(cur_volume)+"");
}

void play_station(){
    preferences.putShort("station", cur_station); // store the current station in nvs
    write_stationNr(cur_station);
    // Get the first 2 characters of the station name
    String station_name_language = titles[cur_station].substring(0, 2);
    // Get the station name but without the first 3 characters
    String station_name = titles[cur_station].substring(3);
    audio.connecttospeech(station_name.c_str(), station_name_language.c_str());
    // Wait for the speech to finish
    while(audio.isRunning()){
        loop();
    }
    Serial.println("Speech finished");

    audio.connecttohost(stations[cur_station].c_str());
    Serial.print("Requested to play: ");
    Serial.println(stations[cur_station].c_str());
    server.send(200, "text/html", "Station: "+String(cur_station)+"");
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
        String url = server.arg("url");
        preferences.putString("url", url);
        audio.connecttohost(url.c_str());
        Serial.print("Requested to play: ");
        Serial.println(url);
        server.send(200, "text/html", "Playing URL: "+url);
    }
}

void handleIrCommand(String command) {

    updateSleepTime();

    if (command.compareTo("REPEAT") == 0) {
            // Repeat the last command
            Serial.print("Last command: ");
            Serial.println(last_ir_command);
            if (last_ir_command.compareTo("VOLUME_UP") == 0 || last_ir_command.compareTo("VOLUME_DOWN") == 0) {
                Serial.print("Repeating");
                handleIrCommand(last_ir_command);
            }
            }
            else if (command.compareTo("VOLUME_UP") == 0) {
                Serial.println("Volume up");
                volume_up();
            }
            else if (command.compareTo("VOLUME_DOWN") == 0) {
                Serial.println("Volume down");
                volume_down();
            }
            else if (command.compareTo("STATION_UP") == 0) {
                Serial.println("Station up");
                station_up();
            }
            else if (command.compareTo("STATION_DOWN") == 0) {
                Serial.println("Station down");
                station_down();
            } else if (command.compareTo("SLEEP_TIMER") == 0) {
                Serial.println("Sleep timer");
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
}

void handleIrCode(String irCode) {
    Serial.println("Looking up IR code: "+irCode);

    // Retrieve the stored multi-line string from NVS
    String storedData = preferences.getString("data", "");

    for (int i = 0; i <= line_count; i++) {
        if (lines[i].startsWith(irCode)) {
            String command = lines[i].substring(irCode.length()+1);
            command.trim(); // Strip any amount of whitespace at the end
            Serial.print("Found IR command: ");
            Serial.println(command);

            handleIrCommand(command);
        }
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
    Serial.println("Number of stations: "+String(max_stations));
    Serial.println("Current station: "+String(cur_station));
    Serial.println("Current volume: "+String(cur_volume));

    
    while (WiFi.status() != WL_CONNECTED) {delay(1500); Serial.print(".");}
    log_i("Connected to %s", WiFi.SSID().c_str());

    // Start the mDNS responder for radio.local
    if (!MDNS.begin("radio")) {
        Serial.println("Error setting up MDNS responder!");
        while(1) {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");

    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);

    // Start the HTTP server
    // with callback functions to handle requests
    server.on("/", handleRoot);
    server.on("/volume_up", volume_up);
    server.on("/volume_down", volume_down);
    server.on("/station_up", station_up);
    server.on("/station_down", station_down);
    server.on("/play", play_url);
    server.on("/config", handleConfig);
    server.on("/update", handleUpdate);
    server.begin();
    Serial.printf("HTTP server started, listening on IP %s", WiFi.localIP().toString().c_str());
    Serial.println();

    irrecv.enableIRIn();  // Start the receiver
    Serial.print("IR pin ");
    Serial.println(kRecvPin);

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(cur_volume); // 0...21

    if (cur_station >= max_stations) {
        cur_station = 0;
    }
    audio.connecttohost(stations[cur_station].c_str());
    write_volume(cur_volume);
    write_stationNr(cur_station);
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
        Serial.println("IR Code: "+hexstring);
        irrecv.resume(); // Receive the next value
        handleIrCode(hexstring);
    }

    // See if time to sleep has arrived
    if( use_deep_sleep == true && millis() >= sleep_timer_begin_time) {
#ifdef ESP32C3
        // https://github.com/espressif/arduino-esp32/issues/7005
        // Tell it to wake up from deep sleep when infrared command is received
        esp_deep_sleep_enable_gpio_wakeup(1ULL << kRecvPin,ESP_GPIO_WAKEUP_GPIO_HIGH);
        // Now enter deep sleep
        esp_deep_sleep(0);
#else
        // Tell it to wake up from deep sleep when infrared command is received
        esp_sleep_enable_ext0_wakeup((gpio_num_t)kRecvPin, 0);
        // Now enter deep sleep
        esp_deep_sleep_start();
#endif
    }

}
//**************************************************************************************************
//                                           E V E N T S                                           *
//**************************************************************************************************
void audio_info(const char *info){
    Serial.print("audio_info: "); Serial.println(info);
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
    Serial.print("id3data     ");Serial.println(info);
}
void audio_eof_stream(const char* info){ // The webstream comes to an end
    Serial.print("end of stream:      ");Serial.println(info);
}
void audio_bitrate(const char *info){
    Serial.print("bitrate     ");Serial.println(info);
}
void audio_lasthost(const char *info){  //stream URL played
    Serial.print("lasthost    ");Serial.println(info);
}
void audio_codec(const char *info){
    Serial.print("codec       ");Serial.println(info);
}
void audio_commercial(const char *info){  //duration in sec
    Serial.print("commercial  ");Serial.println(info);
}
void audio_icyurl(const char *info){  //homepage
    Serial.print("icyurl      ");Serial.println(info);
}
void audio_lastmodified(const char *info){  //UTC time of last modification of stream
    Serial.print("lastmodif   ");Serial.println(info);
}
void audio_newstation(const char *info){
    Serial.print("newstation  ");Serial.println(info);
}
void audio_eof_speech(const char *info){
    Serial.print("eof_speech  ");Serial.println(info);
}
void audio_file_mp3(const char *info){
    Serial.print("file_mp3    ");Serial.println(info);
}
void audio_file_aac(const char *info){
    Serial.print("file_aac    ");Serial.println(info);
}
void audio_file_aac_adts(const char *info){
    Serial.print("file_aac_adts");Serial.println(info);
}
void audio_file_opus(const char *info){
    Serial.print("file_opus   ");Serial.println(info);
}
void audio_file_vorbis(const char *info){
    Serial.print("file_vorbis ");Serial.println(info);
}
void audio_file_flac(const char *info){
    Serial.print("file_flac   ");Serial.println(info);
}
void audio_file_wav(const char *info){
    Serial.print("file_wav    ");Serial.println(info);
}
void audio_file_wma(const char *info){
    Serial.print("file_wma    ");Serial.println(info);
}
void audio_file_raw(const char *info){
    Serial.print("file_raw    ");Serial.println(info);
}
void audio_file_unknown(const char *info){
    Serial.print("file_unknown");Serial.println(info);
}
void audio_streamtype(const char *info){
    Serial.print("streamtype  ");Serial.println(info);
}
void audio_streamurl(const char *info){
    Serial.print("streamurl   ");Serial.println(info);
}
void audio_stationcount(const char *info){
    Serial.print("stationcount");Serial.println(info);
}
void audio_stationname(const char *info){
    Serial.print("stationname ");Serial.println(info);
}
void audio_stationlist(const char *info){
    Serial.print("stationlist ");Serial.println(info);
}
void audio_stationlistend(const char *info){
    Serial.print("stationlistend");Serial.println(info);
}
void audio_filesize(const char *info){
    Serial.print("filesize    ");Serial.println(info);
}
void audio_filecount(const char *info){
    Serial.print("filecount   ");Serial.println(info);
}
void audio_filelist(const char *info){
    Serial.print("filelist    ");Serial.println(info);
}
void audio_filelistend(const char *info){
    Serial.print("filelistend ");Serial.println(info);
}
void audio_playlisttitle(const char *info){
    Serial.print("playlisttitle");Serial.println(info);
}
void audio_playlisturl(const char *info){
    Serial.print("playlisturl ");Serial.println(info);
}
void audio_playlistshuffled(const char *info){
    Serial.print("playlistshuffled");Serial.println(info);
}
void audio_playlistrepeat(const char *info){
    Serial.print("playlistrepeat");Serial.println(info);
}
void audio_playlistcount(const char *info){
    Serial.print("playlistcount");Serial.println(info);
}
void audio_playlist(const char *info){
    Serial.print("playlist    ");Serial.println(info);
}
void audio_playlistend(const char *info){
    Serial.print("playlistend ");Serial.println(info);
}