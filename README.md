# parental-broadcast

A one-button Wi-Fi intercom for children's bedrooms, built on an **ESP32** + **MAX98357A** I2S amplifier. Open a web page on your phone, tap a button, and a pre-recorded voice message plays through the speaker in the kids' room — no app, no cloud, no subscription.

```
Phone (same Wi-Fi)
      |
      | HTTP  →  http://interphone-enfants.local
      |
   ESP32
      |
      | I2S (BCLK / LRC / DIN)
      |
  MAX98357A  →  Speaker (4 Ω or 8 Ω, 1–3 W)
```

---

## Hardware

### Required

| Part | Notes |
|---|---|
| ESP32 DevKit (or equivalent) | Any classic ESP32 with 4 MB flash |
| MAX98357A I2S amplifier module | Adafruit breakout or clone |
| Speaker, 4 Ω or 8 Ω, 1–3 W | Small bookshelf or project speaker |
| Dupont wires | |
| USB cable + stable USB power supply | Dedicated supply preferred over a laptop port |

### Recommended but not required for V1

- Small enclosure
- Status LED (see [Improvements](#improvements))
- Physical mute button (see [Improvements](#improvements))

---

## Wiring

| MAX98357A pin | ESP32 pin |
|---|---|
| VIN | 5 V |
| GND | GND |
| BCLK | GPIO 26 |
| LRC | GPIO 25 |
| DIN | GPIO 22 |
| SD | 3.3 V or VIN (must not float — LOW = shutdown) |

| Speaker terminal | MAX98357A terminal |
|---|---|
| + | OUT+ |
| − | OUT− |

> **Do not connect headphones** to the MAX98357A output — it is a speaker amplifier, not a headphone driver.

---

## Software setup

### Prerequisites

- [VS Code](https://code.visualstudio.com/) + [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- [ffmpeg](https://ffmpeg.org/) (for audio conversion)

### 1. Clone the repo

```bash
git clone https://github.com/YOUR_USERNAME/parental-broadcast.git
cd parental-broadcast
```

### 2. Set your Wi-Fi credentials

Edit `src/main.cpp`, lines near the top:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
```

> Never commit real credentials. The `.gitignore` does not protect `main.cpp` — keep a local-only copy if needed.

### 3. Prepare audio files

Record your own voice for each message (your voice works better with children than a robot). Suggested messages:

| Filename | Suggested phrase |
|---|---|
| `moins_fort.wav` | "Les enfants, moins fort s'il vous plaît." |
| `retour_lit.wav` | "Retournez au lit maintenant." |
| `stop_courir.wav` | "Arrêtez de courir, vous allez réveiller tout le monde." |
| `separez_vous.wav` | "Séparez-vous tout de suite." |
| `tout_va_bien.wav` | "Tout va bien ?" |
| `papa_arrive.wav` | "Je vous entends, calmez-vous." |
| `dernier_avertissement.wav` | "Dernier avertissement, sinon je viens." |

Convert to the required format (mono, 16-bit PCM, 16 kHz) using the provided script:

```bash
# Convert all *.m4a files in the current directory, output to data/
./scripts/convert_audio.sh

# Or convert a single file
./scripts/convert_audio.sh recording.m4a

# Manual ffmpeg command
ffmpeg -i input.m4a -ac 1 -ar 16000 -sample_fmt s16 data/moins_fort.wav
```

**Required WAV spec:**

| Parameter | Value |
|---|---|
| Container | WAV |
| Encoding | PCM (uncompressed) |
| Channels | Mono (1) |
| Bit depth | 16-bit |
| Sample rate | 16 000 Hz (or 22 050 Hz) |

Approximate sizes: 1 s ≈ 32 KB · 5 s ≈ 160 KB · 10 messages × 3 s ≈ 960 KB — fits easily in 4 MB flash with a LittleFS partition.

### 4. Flash the firmware

Plug in the ESP32, then from VS Code PlatformIO terminal (or a terminal in the project root):

```bash
# Compile and upload the firmware
pio run --target upload

# Upload the audio files to LittleFS
pio run --target uploadfs

# Open the serial monitor
pio device monitor
```

Expected serial output on boot:

```
Starting children's intercom...
Files in LittleFS:
  - /moins_fort.wav (48234 bytes)
  - /retour_lit.wav (51890 bytes)
  ...
Connected. IP: 192.168.1.42
mDNS active: http://interphone-enfants.local
HTTP server started.
```

---

## Usage

From your phone (connected to the same Wi-Fi):

```
http://interphone-enfants.local
```

If mDNS does not resolve (some Android versions), use the IP shown in the serial monitor:

```
http://192.168.1.42
```

You can also trigger playback directly:

```
http://192.168.1.42/play?msg=moins_fort
```

**Tip:** on iPhone or Android, add the page to the home screen — it will appear as a pseudo-app icon.

---

## Project structure

```
parental-broadcast/
├── platformio.ini          # PlatformIO build config
├── src/
│   └── main.cpp            # All ESP32 firmware
├── data/                   # WAV files go here (not committed)
│   └── .gitkeep
└── scripts/
    └── convert_audio.sh    # ffmpeg batch converter
```

---

## Configuration reference

All tuneable constants are at the top of `src/main.cpp`:

| Constant | Default | Description |
|---|---|---|
| `WIFI_SSID` | `"YOUR_WIFI_SSID"` | Your Wi-Fi network name |
| `WIFI_PASS` | `"YOUR_WIFI_PASSWORD"` | Your Wi-Fi password |
| `MDNS_NAME` | `"interphone-enfants"` | mDNS hostname (`<name>.local`) |
| `I2S_BCLK` | `26` | ESP32 GPIO for I2S bit clock |
| `I2S_LRC` | `25` | ESP32 GPIO for I2S word select |
| `I2S_DOUT` | `22` | ESP32 GPIO for I2S data |
| `VOLUME` | `0.45` | Software volume, `0.0`–`1.0` |

---

## Fixing the IP address

mDNS is convenient, but a reserved IP in your router is more reliable for direct access and troubleshooting. Look for "DHCP reservation" or "static lease" in your router admin panel and assign a fixed IP to the ESP32's MAC address (printed on the serial monitor at boot, or via `WiFi.macAddress()`).

---

## Improvements

These are out of scope for V1 but easy to add later:

### Status LED

```cpp
#define LED_PIN 2
digitalWrite(LED_PIN, isPlaying ? HIGH : LOW);
```

Suggested behaviour: solid = connected, blinking = playing, fast blink = Wi-Fi error.

### Physical mute button

Wire a momentary button between GPIO 32 and GND, configure `INPUT_PULLUP`, and set a `muted` flag that `playWav()` checks.

### Admin endpoints

- `GET /status` — already implemented, returns JSON
- `GET /reboot` — calls `ESP.restart()`
- `GET /volume?value=0.3` — updates `VOLUME` at runtime

### Attention chime

Play a short `ding.wav` before every message so children know to listen.

### Optional token authentication

If you ever expose the ESP32 beyond your local network (not recommended), add a token to the URL:

```
/play?token=YOUR_SECRET&msg=moins_fort
```

In `handlePlay()`:

```cpp
const char* API_TOKEN = "change-me";

if (!server.hasArg("token") || server.arg("token") != API_TOKEN) {
  server.send(403, "text/plain", "Forbidden");
  return;
}
```

And include the token in the JavaScript `fetch()` call.

---

## Troubleshooting

| Symptom | Check |
|---|---|
| Page does not open via `.local` | Try the raw IP — mDNS is a best-effort protocol on some Android versions |
| No sound | VIN→5 V, GND→GND, BCLK→26, LRC→25, DIN→22, SD→HIGH, speaker connected |
| Sound too fast or too slow | WAV is not 16 kHz — re-convert with `ffmpeg -ar 16000` |
| Sound is distorted / clipping | Lower `VOLUME` (e.g. `0.25`) |
| Message is cut off | A second button press while playing is silently ignored (by design) |
| File not found at runtime | Re-run `pio run --target uploadfs` and check the serial monitor file list |

---

## Security notes

- Do not expose this device to the internet (no port forwarding).
- The whitelist in `safeMessageToPath()` ensures arbitrary file paths cannot be requested via the URL.
- Keep the device on your home or IoT Wi-Fi network only.
- Keep the volume at a reasonable level.

---

## License

MIT — do whatever you want with it.
