# Label Maker

![Label Maker](docs/preview.png)

Print a PDF or a picture on an Omezizy D450 4 × 6 thermal label printer from Linux. Open a file, check the black and white preview, then print. It talks to the printer over Bluetooth LE through BlueZ, so there is no driver to install and no pairing.

Made for Omarchy (Arch Linux, Hyprland). It follows the Omarchy theme and shows up in the app launcher.

## What it does

- **Shipping labels.** A FedEx, USPS or UPS label PDF on a letter or A4 page is cropped down to the 4 × 6 label and printed with no dithering, so barcodes stay sharp.
- **Pictures and stickers.** Fit whole page shows all of it. Fill covers the label and crops the edges. Photo mode dithers photos.
- **Turning.** Each fit turns the artwork sideways when that uses the label better. Rotate turns it another quarter turn clockwise, and Auto puts it back.
- **Exact preview.** The preview is the 1-bit image that gets sent, at 203 dots per inch (808 × 1218 dots for 4 × 6). A blank preview says there is nothing dark to print.
- Darkness, copies, and other label sizes.

The printer is thermal, so it prints black only.

## Install

Needs Qt 6 (`qt6-declarative`, `qt6-wayland`), `poppler-qt6` and `bluez`; `./bin/install` pulls them in with pacman.

```sh
git clone https://github.com/Dankestrick/LabelMaker.git
cd LabelMaker
./bin/install
```

That builds the app and installs it as the `label-maker` package. Turn the printer on, load labels, open Label Maker from the app launcher and press Print. It finds the printer by its Bluetooth name, `D450`.

## Shortcuts

| Key | Action |
| --- | --- |
| O | Open a file |
| Space | Print the label |
| ? | Show shortcuts |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z | Redo |
| Q | Quit |

## Command line

```sh
label-maker --render label.pdf --size 4x6 --out preview.png --print
label-maker --render photo.png --size 50x30mm --fit page --photo --out preview.png
```

`--fit` is `shipping` (the default), `page`, or `fill`. `--turn` is `auto` (the default), `upright`, `sideways`, `0`, `90`, `180`, or `270`. `--page` starts at 1. `--darkness` is 1–15 (default 10). `--copies` sets how many print. `--tspl job.bin` saves the raw printer job. `--print` sends it to the printer. Add `--json` for a one-line summary.

`label-maker` and `label-maker open file.pdf` open the window.

## How the printer works

The D450 speaks plain TSPL on the Microchip transparent UART BLE service (`49535343-fe7d-4ae5-8fa9-9fafd205e455`). Writes without a response drop bytes, so every second packet waits for one. The label is sent without `PRINT`, the printer gets a second to report `Cmd error`, and only a clean transfer is printed. A damaged one is cleared and sent once more. The scripts in `probe/` are the experiments that worked this out; they take the printer's address from `D450_ADDR`.

## License

MIT
