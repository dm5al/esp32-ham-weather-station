Waveshare ESP32-S3-Touch-LCD-7, 7 inch, CH422G expander

Built from BOARD=WAVESHARE_7

Flash with:
  esptool.py --chip esp32s3 -p <PORT> --baud 460800 \
    write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
    0x0 bootloader.bin 0x8000 partition-table.bin \
    0x10000 ham_weather_station.bin
