# esp32-i2s-webradio
A webradio player using based on esp32 and i2s DAC

## Downloading

From GitHub Actions under "Summary", "Artifacts"

## Flashing

E.g., on FreeBSD:

```
python3 -m pip install esptool
sudo -E python3 -m esptool --chip esp32 --baud 921600 ... ~/Downloads/firmware.bin
```

## Serial debugging

```
sudo screen /dev/ttyU0 115200
```
