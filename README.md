# esp32-i2s-webradio [![Build Status](https://github.com/probonopd/esp32-i2s-webradio/actions/workflows/compile.yml/badge.svg)](https://github.com/probonopd/esp32-i2s-webradio/actions/workflows/compile.yml)

A webradio player using based on esp32 and i2s DAC

## Features

- [x] Spoken station IDs
- [x] Stations configurable via web interface
- [x] Infrared control with codes configurable via web interface
- [x] Smart sleep timer (time count resets with every infrared key press)
- [x] Deep sleep (WLAN off, will wake up and reconnect with every infrared key press)

## System Requirements

Works with MAX98357A (3 Watt amplifier with DAC), connected three lines (DOUT, BLCK, LRC) to I2S. For stereo are two MAX98357A necessary. AudioI2S works with UDA1334A (Adafruit I2S Stereo Decoder Breakout Board), PCM5102A and CS4344. Other HW may work but not tested.

See https://github.com/schreibfaul1/ESP32-audioI2S for details.

## Downloading

From GitHub Actions under "Summary", "Artifacts"

## Flashing

E.g., on FreeBSD:

```
python3 -m pip install esptool
sudo -E python3 -m esptool erase_flash
sudo -E python3 -m esptool --chip esp32 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size 4MB 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
```

For subsequent flashes, the following is sufficient:

```
sudo -E python3 -m esptool --chip esp32 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size 4MB  0x10000 ~/Downloads/firmware.bin
```

## Configuring

Configuration is done via the web using a simple text format:

```
title de SWR 1
station https://liveradio.swr.de/sw890cl/swr1bw/

title de SWR 2
station https://liveradio.swr.de/sw890cl/swr2/

title de SWR 3
station https://liveradio.swr.de/sw890cl/swr3/

title de SWR 4
station https://liveradio.swr.de/sw890cl/swr4fn/

title de SWR Aktuell
station https://liveradio.swr.de/sw890cl/swraktuell/

title de Deutschlandfunk
station https://st01.sslstream.dlf.de/dlf/01/low/aac/stream.aac?aggregator=web

title en Radio Swiss Classic
station https://stream.srg-ssr.ch/rsc_de/aacp_48.m3u

# IR Codes
0x86C620DF VOLUME_UP
0x86C6A05F VOLUME_DOWN
0x86C640BF STATION_UP
0x86C6C03F STATION_DOWN
0x86C65CA3 SLEEP_TIMER
0x86C658A7 0
0x86C68877 1
0x86C648B7 2
0x86C6C837 3
0x86C628D7 4
0x86C6A857 5
0x86C66897 6
0x86C6E817 7
0x86C618E7 8
0x86C69867 9
0xFFFFFFFF REPEAT

# After how many minutes to sleep
sleep 60
```

## Serial debugging

```
sudo screen /dev/ttyU0 115200
```

## Ideas for future features

Contributions are welcome.

- [ ] Podcast support (remembering which episodes have been played all the way to the end)
- [ ] Alarm clock function (switch on at certain times)
- [ ] Speak certain texts at certain times (optionally repeating until confirmed by the user by pressing a key)
- [ ] Optional display support
- [ ] Optional Ethernet via [LAN8720](https://sautter.com/blog/ethernet-on-esp32-using-lan8720/)
- [ ] Possibly add Bluetooth support
- [ ] Possibly add crossfade support for seamless station ID insertions
- [ ] Possibly add equalizer support (sound library supports this)
- [ ] Design carrier PCB with sockets for all the needed modules (easier) or design all chips onto PCB (harder) for assembly at Aisler or JLCPCB (any help appreciated; possibly base on https://github.com/philippe44/SqueezeAMP)
- [ ] 3D printable case

Also see

* https://github.com/sle118/squeezelite-esp32
* ESP32 Audio Kit with https://github.com/schreibfaul1/es8388 and Radio project ([https://radio-bastler.de/forum/showthread.php?tid=17786](more))
* https://github.com/Edzelf/ESP32-Radio (but this uses a VS1053 hardware mp3 decoder, we want just i2s DACs and do that in software)
