# esp32-i2s-webradio [![Build Status](https://github.com/probonopd/esp32-i2s-webradio/actions/workflows/compile.yml/badge.svg)](https://github.com/probonopd/esp32-i2s-webradio/actions/workflows/compile.yml)

A webradio player using based on esp32 and i2s DAC

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

## Serial debugging

```
sudo screen /dev/ttyU0 115200
```

## TODO

- [ ] Add support for suitable optional display(s)
- [ ] Make stations configurable via web interface rather than hardcoded
- [ ] Make infrared codes configurable via web interface rather than hardcoded
- [ ] Add Ethernet via optional [LAN8720](https://sautter.com/blog/ethernet-on-esp32-using-lan8720/) in addition to WLAN
- [ ] Possibly add Bluetooth support
- [ ] Possibly add crossfade support for seamless station ID insertions
- [ ] Possibly add equalizer support
- [ ] Design carrier PCB with sockets for all the needed modules (easier) or design all chips onto PCB (harder) for assembly at Aisler or JLCPCB (any help appreciated; possibly base on https://github.com/philippe44/SqueezeAMP)
- [ ] Design 3D printable case

Also see

* https://github.com/sle118/squeezelite-esp32
* ESP32 Audio Kit with https://github.com/schreibfaul1/es8388 and Radio project ([https://radio-bastler.de/forum/showthread.php?tid=17786](more))
