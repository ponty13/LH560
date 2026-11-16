# LH-560 thermal label printer: unofficial CUPS driver for Linux
 
A CUPS raster filter and PPD for the **LH-560** thermal label printer (USB `09c6:0426`), which the vendor supports only through a macOS/Windows driver ("LeftHans2"). The printer speaks **TSPL**; this driver converts CUPS raster to the same TSPL stream the vendor's macOS filter produces.
 
> **Status:** works on the author's LH-560. Tested on `Ubuntu 26.04.1, CUPS 2.4.16`. Reports from other LH-560 / LeftHans2-family printers are welcome (see [Contributing](#contributing)).
 
**Not affiliated with the printer's vendor.** Names are used only to identify the hardware.
 
## Files
 
| File | Purpose |
|---|---|
| `rastertolh560.c` | CUPS filter (CUPS raster in, TSPL out) |
| `LH560.ppd` | PPD: page sizes, 203 dpi, options |
| `install.sh` | Builds and installs the filter and PPD, optionally creates the queue |
 
## Install
 
Build dependency: the CUPS development headers (`libcups2-dev` on Debian/Ubuntu, `cups-devel` on Fedora/RHEL, `libcups` on Arch).
 
```sh
git clone https://github.com/ponty13/LH560 && cd LH560
lpinfo -v                                       # find your printer's USB URI
sudo sh install.sh "usb://LH/-560?serial=0.0"   # use the URI from lpinfo -v; keep the quotes
```
 
This printer reports no manufacturer or model strings over USB, so CUPS shows it under a generic name (here `usb://LH/-560?serial=0.0`). Use the exact URI from your own `lpinfo -v`.
 
Without a URI, `install.sh` installs only the filter and PPD. Create the queue yourself:
 
```sh
sudo lpadmin -p LH560 -E -v "<uri>" -P LH560.ppd
```
 
On CUPS 3 or distros that moved the PPD API into `libppd`, add `-lppd` to the `cc` line in `install.sh`.
 
## Use
 
```sh
# 100x100 mm label; fit-to-page scales a letter-size PDF to the label
lp -d LH560 -o media=w283h283 -o fit-to-page label.pdf
 
# set queue defaults
sudo lpadmin -p LH560 -o media=w283h283 -o Darkness=8
lpoptions -p LH560 -l       # list all options
```
 
| Option | Values | Effect |
|---|---|---|
| `media` | `w283h283` (100x100 mm), `w288h144` (4x2 in), ... | `SIZE` (points converted to mm), bitmap size |
| `Darkness` | `Default`, `0`-`15` | `DENSITY n`; `Default` sends nothing |
| `zePrintRate` | `Default`, `1`-`8` | `SPEED n`; `Default` sends nothing |
| `AdjustHoriaontal` | `-20`..`20` mm | `REFERENCE x,y` (8 dots/mm) |
| `AdjustVertical` | `-20`..`20` mm | `REFERENCE x,y` and `OFFSET n mm` |
| `Rotate` | `0`-`3` | `DIRECTION n,0` (pixels are not rotated; the printer firmware handles it) |
| `AutoDotted` | `0`, `1` | `SETC AUTODOTTED OFF/ON` |
| `Threshold` | `1`-`255` (default `129`) | Black/white cutoff; job option only, not in the PPD |
 
`AdjustHoriaontal` keeps the vendor's original spelling so existing option strings keep working.
 
## How it works
 
For each page the filter writes exactly:
 
```
SIZE <w> mm ,<h> mm
REFERENCE <hoff*8>,<voff*8>
DIRECTION <rotate>,0
GAP 3 mm,0 mm
OFFSET <voff> mm
DENSITY <n>              (omitted if Darkness=Default)
SPEED <n>                (omitted if zePrintRate=Default)
SETC AUTODOTTED ON|OFF
SETC PAUSEKEY ON
SETC WATERMARK OFF
CLS
BITMAP 0,0,<ceil(width/8)>,<height>,1,<raw 1-bpp data, 1 = white>PRINT 1,1
```
 
Lines end in `\n`, and there is no newline between the bitmap data and `PRINT`. Input is 8-bit gray raster at 203 dpi. A pixel is white when its gray value is above 128; there is no dithering. Rows are padded to a byte boundary with white. `PRINT 1,N` uses the copy count from the raster header (normally 1, since the PPD sets `cupsManualCopies`).
 
## How it was made
 
The protocol was worked out by running the vendor's macOS CUPS filter on test rasters and comparing the TSPL it produced. No vendor code or vendor files are included. The result is byte-identical to the vendor filter's output on the test pages used, including a black-to-white gradient that pins down the threshold (gray 128 is black, 129 is white).
 
## Limitations
 
- **Media tracking** (continuous / gap / black-mark) is not sent: `GAP 3 mm,0 mm` is fixed. The vendor filter doesn't send it either, so the printer uses its own stored setting. Calibrate with the printer's hardware button if it misfeeds.
- `FeedOffset` is not supported (the vendor filter ignores it too).
- `Rotate` values `2` and `3` pass `DIRECTION 2,0` / `3,0` through unchanged; what the printer does with them is firmware-dependent and not verified.
- One physical printer tested.
## Troubleshooting
 
- `sudo ./install.sh` says "No such file or directory": the script has Windows (CRLF) line endings. Fix with `sed -i 's/\r$//' install.sh rastertolh560.c LH560.ppd`, or run `sudo sh install.sh`.
- Nothing prints: `lpstat -t` and `sudo tail -40 /var/log/cups/error_log`.
- Check the filter without a printer: `PPD=LH560.ppd ./rastertolh560 1 user title 1 "media=w283h283" test.ras | head -c 300`
## Contributing
 
Issues and pull requests are welcome, especially:
 
- confirmation on other LeftHans2-family printers (include `lsusb` ID and `lpinfo -v` output),
- real continuous / black-mark media support (verified on hardware),
- dithering for photo content.
## License
 
MIT; see `LICENSE`.
