# Hardware

Two boards are supported. Both are 800×480 RGB565 panels driven by the
ESP32-S3's LCD_CAM peripheral with GT911 capacitive touch and 8 MB octal PSRAM,
so everything above the BSP is identical — but almost nothing below it is.

## Waveshare ESP32-S3-Touch-LCD-7

7 inch. The reference board this project was developed on.

- **Board:** <https://www.waveshare.com/esp32-s3-touch-lcd-7.htm>
- **Enclosure:** <https://www.thingiverse.com/thing:7273339>

| | |
|---|---|
| Flash | 8 MB |
| PSRAM | 8 MB octal @ 80 MHz |
| HSYNC / VSYNC / DE / PCLK | 46 / 3 / 5 / 7 |
| Touch I²C SDA / SCL | 8 / 9 |
| Touch reset | CH422G expander, EXIO1 |
| Touch INT | GPIO 4 |
| Backlight | CH422G expander, EXIO2 — on/off only |
| Pixel clock | 21 MHz, ~47 Hz refresh |

Two things about this board cost real time to discover:

**The CH422G silkscreen numbering.** EXIO*n* on the board is chip IO*n*, so
EXIO1 is bit 1, not bit 0. Assuming otherwise moves every control by one.

**The GT911 address latch.** The controller samples its INT pin on the rising
edge of reset to choose its I²C address: low → 0x5D, high → 0x14. The level has
to be held for **at least 50 ms after reset rises**, not just during it.
Releasing INT early leaves the chip answering on I²C but never reporting a
touch — it looks like a working device that ignores you.

## Sunton ESP32-8048S050C

5 inch.

- **Board and pinout:** <https://www.openhasp.com/0.7.0/hardware/sunton/esp32-8048s0xx/>
  — covers the whole 8048S0xx family; the S050 is the 5 inch member
- **Enclosure:** <https://cults3d.com/en/3d-model/various/desk-stand-for-esp32-8048s043-display>
  — drawn for the **8048S043**, the 4.3 inch board. The PCB outlines differ, so
  check the fit or rescale before printing

| | |
|---|---|
| Flash | 16 MB |
| PSRAM | 8 MB octal @ 80 MHz |
| HSYNC / VSYNC / DE / PCLK | 39 / 41 / 40 / 42 |
| Touch I²C SDA / SCL | 19 / 20 |
| Touch reset | GPIO 38 |
| Touch INT | **not connected** |
| Backlight | GPIO 2 on LEDC — dims |
| Pixel clock | 24 MHz on PLL240M, ~53 Hz refresh |

**No I/O expander.** The backlight is a real GPIO on an LEDC channel, so unlike
the Waveshare this panel can actually dim.

**INT is unpopulated.** There is an empty R17 footprint to GPIO 18 for anyone
who wants interrupts. Without it the address cannot be selected at reset, so the
controller comes up on its default and the driver polls. Slightly more CPU, no
functional difference.

**The pixel clock was tuned by observation, not taken from the reference.** The
vendor board support uses 18 MHz, which with these porches gives only

```
18e6 / ((7+40+800+40) × (7+10+480+10)) = 40 Hz
```

and 40 Hz is visible as flicker. The clock source matters separately: at
`LCD_CLK_SRC_DEFAULT` the black areas showed low-level noise that disappeared
entirely on `PLL240M` with nothing else changed. So the source fixed the noise
and the rate fixed the flicker — two independent problems that looked like one.

## Tuning the panel without rebuilding

Timings live in NVS and can be changed over the serial console. Every `lcd set`
saves and reboots.

```
lcd                        show current timings and resulting refresh rate
lcd set pclk 21            pixel clock in MHz
lcd set hbp 40             horizontal back porch — moves the picture right
lcd set bblines 20         bounce buffer height in lines
lcd reset                  forget the override, return to compiled defaults
```

Symptoms and the lever that addresses them:

| Symptom | Try |
|---|---|
| Picture trembles or jumps | `bblines 20`, then a lower `pclk` |
| Visible flicker, "frames" | higher `pclk` |
| Noise or shimmer in dark areas | clock source (compile-time) |
| Picture offset sideways | `hbp` |

## The bounce buffer, and why it is 16 lines

The LCD DMA reads the framebuffer straight out of PSRAM at around 42 MB/s,
which is close to what that bus will give. Bounce buffers stage each frame
through internal SRAM so a momentary stall on the PSRAM bus does not starve the
scan.

Ten lines gives 411 µs between refills and was enough until the propagation
map's canvas grew to 389 KB: LVGL blits that canvas into the PSRAM framebuffer,
and the copy saturates the same bus the DMA is reading from for long enough to
run the buffer dry. The scan then desyncs, which is seen as the picture
trembling and jumping.

Sixteen lines gives 658 µs instead. Measured cost: 19 KB more internal RAM, and
nothing else — the largest free internal block after boot was unchanged, because
this comes out of the DMA reserve rather than the general heap.
