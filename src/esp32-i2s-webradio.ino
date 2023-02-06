// Based on https://github.com/schreibfaul1/ESP32-audioI2S/blob/master/examples/Simple_WiFi_Radio/Simple_WiFi_Radio_IR.ino

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>

#include "Audio.h" // https://github.com/schreibfaul1/ESP32-audioI2S
#include "IR.h"    // https://github.com/schreibfaul1/ESP32-IR-Remote-Control"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>    // https://github.com/me-no-dev/ESPAsyncWebServer
#include <ESPAsyncWiFiManager.h>  // https://github.com/alanswx/ESPAsyncWiFiManager

AsyncWiFiManager wifiManager(&server, &dns);

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

String stations[] ={
        "0n-80s.radionetz.de:8000/0n-70s.mp3",
        "mediaserv30.live-streams.nl:8000/stream",
        "www.surfmusic.de/m3u/100-5-das-hitradio,4529.m3u",
        "stream.1a-webradio.de/deutsch/mp3-128/vtuner-1a",
        "mp3.ffh.de/radioffh/hqlivestream.aac", //  128k aac
        "www.antenne.de/webradio/antenne.m3u",
        "listen.rusongs.ru/ru-mp3-128",
        "edge.audio.3qsdn.com/senderkw-mp3",
        "macslons-irish-pub-radio.com/media.asx",
};

//some global variables

uint8_t max_volume   = 21;
uint8_t max_stations = 0;   //will be set later
uint8_t cur_station  = 0;   //current station(nr), will be set later
uint8_t cur_volume   = 0;   //will be set from stored preferences
int8_t  cur_btn      =-1;   //current button (, -1 means idle)

enum action{VOLUME_UP=0, VOLUME_DOWN=1, STATION_UP=2, STATION_DOWN=3};
enum staus {RELEASED=0, PRESSED=1};



void write_stationNr(uint8_t nr){
    String snr = String(nr);
    if(snr.length()<2) snr = "0"+snr;
}
void write_volume(uint8_t vol){
    String svol = String(vol);
    if(svol.length()<2) svol = "0"+svol;
}
void write_stationName(String sName){
}
void write_streamTitle(String sTitle){
}
void volume_up(){
    if(cur_volume < max_volume){
        cur_volume++;
        write_volume(cur_volume);
        audio.setVolume(cur_volume);
        pref.putShort("volume", cur_volume);} // store the current volume in nvs
}
void volume_down(){
    if(cur_volume>0){
        cur_volume-- ;
        write_volume(cur_volume);
        audio.setVolume(cur_volume);
        pref.putShort("volume", cur_volume);} // store the current volume in nvs
}
void station_up(){
    if(cur_station < max_stations-1){
        cur_station++;
        write_stationNr(cur_station);
        audio.connecttohost(stations[cur_station].c_str());
        pref.putShort("station", cur_station);} // store the current station in nvs
}
void station_down(){
    if(cur_station > 0){
        cur_station--;
        write_stationNr(cur_station);
        audio.connecttohost(stations[cur_station].c_str());
        pref.putShort("station", cur_station);} // store the current station in nvs
}


//**************************************************************************************************
//                                           S E T U P                                             *
//**************************************************************************************************
void setup() {
    max_stations= sizeof(stations)/sizeof(stations[0]); log_i("max stations %i", max_stations);
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

    wifiManager.autoConnect("AutoConnectAP");
        
    while (WiFi.status() != WL_CONNECTED) {delay(1500); Serial.print(".");}
    log_i("Connected to %s", WiFi.SSID().c_str());

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
