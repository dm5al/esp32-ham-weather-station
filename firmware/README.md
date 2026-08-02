# Pre-built firmware

Ready-to-flash binaries for both supported boards. Nothing but `esptool` is
needed — no toolchain, no ESP-IDF.

| Folder | Board | Panel |
|---|---|---|
| [`waveshare/`](waveshare/) | Waveshare ESP32-S3-Touch-LCD-7 | 7″, 800×480 |
| [`sunton/`](sunton/) | Sunton ESP32-8048S050C | 5″, 800×480 |

**The two are not interchangeable.** They share no GPIO assignments and use
different panel timings. Flashing the wrong one gives a board that boots
normally over serial and shows nothing at all on the display.

## Install esptool

```bash
pip install esptool
```

## Flash

Replace `PORT` with your serial port — `COM5` on Windows, `/dev/ttyUSB0` on
Linux, `/dev/cu.usbserial-*` on macOS.

### Waveshare 7″

```bash
esptool.py --chip esp32s3 -p PORT --baud 460800 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 waveshare/bootloader.bin \
  0x8000 waveshare/partition-table.bin \
  0x10000 waveshare/ham_weather_station.bin
```

### Sunton 5″

```bash
esptool.py --chip esp32s3 -p PORT --baud 460800 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size detect \
  0x0 sunton/bootloader.bin \
  0x8000 sunton/partition-table.bin \
  0x10000 sunton/ham_weather_station.bin
```

Updating an already-flashed board only needs the application:

```bash
esptool.py --chip esp32s3 -p PORT --baud 460800 \
  write_flash 0x10000 <board>/ham_weather_station.bin
```

## First run

The commissioning assistant appears automatically:

1. **Language and units** — six languages, EU/UK/USA formats
2. **Wi-Fi** — pick a network and enter its password
3. **Station** — callsign, QTH and six-character Maidenhead locator

Get the locator right; every bearing and distance on the device is computed from
it. If you mistype it, Settings → Edit will fix it, or over the console:

```
station set DM5AL UNNAU JO30WP
```

Your callsign is stored on the device and **never transmitted**. Only the
two-character grid field (`JO`) is ever sent, and only to ask which regions have
been heard from.

## If it does not connect

Some boards need help entering download mode: hold **BOOT**, tap **RST**,
release **BOOT**, then run the command again. If `esptool` reports *"no serial
data received"* repeatedly, try a different USB cable before anything else —
cheap charge-only cables are the single most common cause, and a marginal one
will also fail intermittently mid-session rather than cleanly.

## Verifying the flash

Open a serial monitor at 115200. A healthy boot ends with:

```
I (1127) board: board ready: 800x480 RGB565, GT911 touch, LVGL running
I (1853) selftest: ==== ALL PASS ====
```

If the display is blank but that appears, the firmware is running and you have
almost certainly flashed the wrong board's image.

## Adjusting the picture

Panel timings are stored in NVS and tunable over the console without
reflashing — see [`../docs/HARDWARE.md`](../docs/HARDWARE.md).

```
lcd                    show timings and refresh rate
lcd set pclk 21        pixel clock, MHz
lcd set bblines 20     bounce buffer height
lcd reset              back to compiled defaults
```
