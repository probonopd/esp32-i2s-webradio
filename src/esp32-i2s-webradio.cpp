// Based on https://github.com/schreibfaul1/ESP32-audioI2S/blob/master/examples/Simple_WiFi_Radio/Simple_WiFi_Radio_IR.ino

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>

#include "Audio.h" // https://github.com/schreibfaul1/ESP32-audioI2S
#include "IR.h"    // https://github.com/schreibfaul1/ESP32-IR-Remote-Control"

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

#include <ESPmDNS.h>

#include "esp32/rom/rtc.h"

WiFiManager wifiManager;

// HTTP Server
#include <WebServer.h>
WebServer server(80);

#define SPI_MOSI      23
#define SPI_MISO      19
#define SPI_SCK       18
#define I2S_DOUT      25
#define I2S_BCLK      27
#define I2S_LRC       26
#define IR_PIN        34

Preferences pref;
Audio audio;
IR ir(IR_PIN);  // do not change the objectname, it must be "ir"

String stations[6];

//some global variables

uint8_t max_volume   = 21;
uint8_t max_stations = 0;   //will be set later
uint8_t cur_station  = 0;   //current station(nr), will be set later
uint8_t cur_volume   = 0;   //will be set from stored preferences
int8_t  cur_btn      =-1;   //current button (, -1 means idle)

enum action{VOLUME_UP=0, VOLUME_DOWN=1, STATION_UP=2, STATION_DOWN=3};
enum staus {RELEASED=0, PRESSED=1};

void handleRoot() {
  Serial.println("/ requested");
  // Simple html page with buttons to control the radio
  String MAIN_page = "<!DOCTYPE html><html><head><title>WebRadio</title></head><body><h1>WebRadio</h1>";
  MAIN_page += "<script>";
  MAIN_page += "function send(url) { var xhttp = new XMLHttpRequest(); xhttp.open('GET', url, true); xhttp.send(); }";
  MAIN_page += "</script>";
  MAIN_page += "<button onclick='send(\"/volume_up\")'>Volume up</button>";
  MAIN_page += "<button onclick='send(\"/volume_down\")'>Volume down</button>";
  MAIN_page += "<button onclick='send(\"/station_up\")'>Station up</button>";
  MAIN_page += "<button onclick='send(\"/station_down\")'>Station down</button>";
  MAIN_page += "</body></html>";
  server.send(200, "text/html", MAIN_page);
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
    // Convert sName to const char* and play it
    // audio.connecttospeech(sName.c_str(), "de");
    // This works, but it also stops the radio; we would like to play the station name in the background
}
void write_streamTitle(String sTitle){
    Serial.println(sTitle);
}
void volume_up(){
    if(cur_volume < max_volume){
        cur_volume++;
        write_volume(cur_volume);
        audio.setVolume(cur_volume);
        pref.putShort("volume", cur_volume);} // store the current volume in nvs
        Serial.println(cur_volume);
        server.send(200, "text/html", "Volume: "+String(cur_volume)+"");
}
void volume_down(){
    if(cur_volume>0){
        cur_volume-- ;
        write_volume(cur_volume);
        audio.setVolume(cur_volume);
        pref.putShort("volume", cur_volume);} // store the current volume in nvs
        Serial.println(cur_volume);
        server.send(200, "text/html", "Volume: "+String(cur_volume)+"");
}
void station_up(){
    if(cur_station < max_stations-1){
        cur_station++;
        write_stationNr(cur_station);
        audio.connecttohost(stations[cur_station].c_str());
        pref.putShort("station", cur_station);} // store the current station in nvs
        Serial.println(stations[cur_station].c_str());
        server.send(200, "text/html", "Station: "+String(cur_station)+"");
}
void station_down(){
    if(cur_station > 0){
        cur_station--;
        write_stationNr(cur_station);
        audio.connecttohost(stations[cur_station].c_str());
        pref.putShort("station", cur_station);} // store the current station in nvs
        Serial.println(stations[cur_station].c_str());
        server.send(200, "text/html", "Station: "+String(cur_station)+"");
}


//**************************************************************************************************
//                                           S E T U P                                             *
//**************************************************************************************************
void setup() {

    Serial.begin(115200);
  
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    pref.begin("WebRadio", false);  // instance of preferences for defaults (station, volume ...)
    if(pref.getShort("volume", 1000) == 1000){ // if that: pref was never been initialized
        pref.putShort("volume", 10);
        pref.putShort("station", 0);
    }
    else{ // get the stored values
        cur_station = pref.getShort("station");
        cur_volume = pref.getShort("volume");
    }

    WiFiManagerParameter surl_0("server", "stream_url_0", "https://liveradio.swr.de/sw890cl/swr1bw/", 128);
    WiFiManagerParameter surl_1("server", "stream_url_1", "https://liveradio.swr.de/sw890cl/swr2/", 128);
    WiFiManagerParameter surl_2("server", "stream_url_2", "https://liveradio.swr.de/sw890cl/swr3/", 128);
    WiFiManagerParameter surl_3("server", "stream_url_3", "https://liveradio.swr.de/sw890cl/swr4fn/", 128);
    WiFiManagerParameter surl_4("server", "stream_url_4", "https://liveradio.swr.de/sw890cl/swraktuell/", 128);
    WiFiManagerParameter surl_5("server", "stream_url_5", "https://liveradio.swr.de/sw890cl/dasding/", 128);

    wifiManager.addParameter(&surl_0);
    wifiManager.addParameter(&surl_1);
    wifiManager.addParameter(&surl_2);
    wifiManager.addParameter(&surl_3);
    wifiManager.addParameter(&surl_4);
    wifiManager.addParameter(&surl_5);

    wifiManager.autoConnect();
    
    stations[0] = surl_0.getValue();
    stations[1] = surl_1.getValue();
    stations[2] = surl_2.getValue();
    stations[3] = surl_3.getValue();
    stations[4] = surl_4.getValue();
    stations[5] = surl_5.getValue();

    max_stations = sizeof(stations)/sizeof(stations[0]);
    Serial.println("Number of stations: "+String(max_stations));

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
    server.begin();
    Serial.printf("HTTP server started, listening on IP %s", WiFi.localIP().toString().c_str());
    Serial.println();

    ir.begin();  // Init InfraredDecoder
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(cur_volume); // 0...21
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
    ir.loop();

    // listen for web requests
    server.handleClient();

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

// Events from IR Library
void ir_res(uint32_t res){
    if(res < max_stations){
        cur_station = res;
        write_stationNr(cur_station);
        audio.connecttohost(stations[cur_station].c_str());
        pref.putShort("station", cur_station);} // store the current station in nvs
    else{
        audio.connecttohost(stations[cur_station].c_str());
    }
}
void ir_number(const char* num){

}
void ir_key(const char* key){
    switch(key[0]){
        case 'k':                   break; // OK
        case 'r':   volume_up();    break; // right
        case 'l':   volume_down();  break; // left
        case 'u':   station_up();   break; // up
        case 'd':   station_down(); break; // down
        case '#':                   break; // #
        case '*':                   break; // *
        default:    break;
    }
}