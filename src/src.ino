#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include "driver/i2s.h"
#include <time.h>

// =========================
// WIFI CONFIGURATION
// =========================

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

const char* MDNS_NAME = "interphone-enfants";

// =========================
// I2S CONFIGURATION
// =========================

#define I2S_PORT I2S_NUM_0

#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DOUT 22

// Software volume: 0.0 to 3.0  (>1.0 clips but is louder)
float VOLUME = 0.45;

// =========================
// WEB SERVER
// =========================

WebServer server(80);

bool isPlaying = false;

// =========================
// DAILY LOG
// =========================

#define LOG_FILE "/playlog.txt"

String getToday() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "unknown";
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

void incrementLog() {
  String today = getToday();
  if (today == "unknown") return;

  // Read existing log
  String content = "";
  File f = LittleFS.open(LOG_FILE, "r");
  if (f) {
    content = f.readString();
    f.close();
  }

  // Parse lines, find today, increment or append
  bool found = false;
  String output = "";
  int start = 0;
  while (start < (int)content.length()) {
    int nl = content.indexOf('\n', start);
    if (nl == -1) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();
    if (line.length() > 0) {
      int sp = line.indexOf(' ');
      if (sp > 0) {
        String date  = line.substring(0, sp);
        int    count = line.substring(sp + 1).toInt();
        if (date == today) {
          output += today + " " + String(count + 1) + "\n";
          found = true;
        } else {
          output += line + "\n";
        }
      }
    }
    start = nl + 1;
  }
  if (!found) output += today + " 1\n";

  File fw = LittleFS.open(LOG_FILE, "w");
  if (fw) { fw.print(output); fw.close(); }
}

// =========================
// HTML PAGE
// =========================

String htmlPage() {
  return R"rawliteral(
<!doctype html>
<html lang="fr">
<head>
  <meta charset="utf-8">
  <title>Interphone enfants</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root {
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }
    body {
      margin: 0;
      background: #111;
      color: #f5f5f5;
    }
    main {
      max-width: 520px;
      margin: 0 auto;
      padding: 20px;
    }
    h1 {
      font-size: 1.8rem;
      margin-bottom: 0.3rem;
    }
    p {
      color: #bbb;
      margin-top: 0;
      margin-bottom: 1.5rem;
    }
    button {
      width: 100%;
      border: 0;
      border-radius: 14px;
      padding: 18px 16px;
      margin: 8px 0;
      font-size: 1.2rem;
      font-weight: 650;
      background: #2f80ed;
      color: white;
      box-shadow: 0 4px 12px rgba(0,0,0,0.25);
      cursor: pointer;
    }
    button:active {
      transform: scale(0.98);
      background: #1c65c9;
    }
    .secondary {
      background: #444;
    }
    .warning {
      background: #b84040;
    }
    .volume-row {
      display: flex;
      align-items: center;
      gap: 12px;
      margin: 20px 0 8px;
    }
    .volume-row label {
      white-space: nowrap;
      color: #bbb;
      font-size: 0.95rem;
      min-width: 90px;
    }
    input[type=range] {
      flex: 1;
      accent-color: #2f80ed;
    }
    #vol-val {
      color: #f5f5f5;
      min-width: 36px;
      text-align: right;
      font-size: 0.95rem;
    }
    #status {
      margin-top: 16px;
      padding: 12px;
      background: #222;
      border-radius: 10px;
      color: #ddd;
      min-height: 1.2rem;
    }
  </style>
</head>
<body>
  <main>
    <h1>Chambre enfants</h1>
    <p>Appuie sur un bouton pour diffuser un message.</p>

    <button onclick="play('moins_fort')">Moins fort</button>
    <button onclick="play('retour_lit')">Retour au lit</button>
    <button onclick="play('stop_courir')">Arrêtez de courir</button>
    <button onclick="play('separez_vous')">Séparez-vous</button>
    <button onclick="play('tout_va_bien')" class="secondary">Tout va bien</button>
    <button onclick="play('papa_arrive')" class="secondary">Parents arrivent mécontents</button>
    <button onclick="play('dernier_avertissement')" class="warning">Dernier avertissement</button>

    <div class="volume-row">
      <label for="vol">Volume :</label>
      <input type="range" id="vol" min="0" max="3" step="0.05" value="0.45"
             oninput="updateVolDisplay(this.value)" onchange="setVolume(this.value)">
      <span id="vol-val">0.45</span>
    </div>

    <div id="status">Prêt.</div>
  </main>

  <script>
    async function play(msg) {
      const status = document.getElementById('status');
      status.textContent = "Envoi : " + msg + "...";
      try {
        const res = await fetch('/play?msg=' + encodeURIComponent(msg));
        const text = await res.text();
        status.textContent = res.ok ? "Message envoyé." : "Erreur : " + text;
      } catch (e) {
        status.textContent = "Impossible de joindre l'ESP32.";
      }
    }

    function updateVolDisplay(v) {
      document.getElementById('vol-val').textContent = parseFloat(v).toFixed(2);
    }

    async function setVolume(v) {
      updateVolDisplay(v);
      await fetch('/volume?value=' + v);
    }

    // Fetch current volume on load so slider matches actual state
    fetch('/status').then(r => r.json()).then(d => {
      if (d.volume !== undefined) {
        document.getElementById('vol').value = d.volume;
        updateVolDisplay(d.volume);
      }
    }).catch(() => {});
  </script>
</body>
</html>
)rawliteral";
}

// =========================
// I2S SETUP
// =========================

void setupI2S(int sampleRate) {
  i2s_driver_uninstall(I2S_PORT);

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = (uint32_t)sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err;

  err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("i2s_driver_install error: %d\n", err);
    return;
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("i2s_set_pin error: %d\n", err);
    return;
  }

  i2s_zero_dma_buffer(I2S_PORT);
}

// =========================
// SIMPLE WAV READER
// =========================

struct WavInfo {
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint16_t bitsPerSample;
  uint32_t dataStart;
  uint32_t dataSize;
};

bool readBytes(File& file, void* buffer, size_t len) {
  return file.read((uint8_t*)buffer, len) == len;
}

uint32_t readU32(File& file) {
  uint8_t b[4];
  if (!readBytes(file, b, 4)) return 0;
  return ((uint32_t)b[0]) |
         ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) |
         ((uint32_t)b[3] << 24);
}

uint16_t readU16(File& file) {
  uint8_t b[2];
  if (!readBytes(file, b, 2)) return 0;
  return ((uint16_t)b[0]) | ((uint16_t)b[1] << 8);
}

bool parseWav(File& file, WavInfo& info) {
  char riff[4];
  char wave[4];

  file.seek(0);

  if (!readBytes(file, riff, 4)) return false;
  uint32_t riffSize = readU32(file);
  (void)riffSize;
  if (!readBytes(file, wave, 4)) return false;

  if (strncmp(riff, "RIFF", 4) != 0 || strncmp(wave, "WAVE", 4) != 0) {
    return false;
  }

  bool foundFmt = false;
  bool foundData = false;

  while (file.available()) {
    char chunkId[4];
    if (!readBytes(file, chunkId, 4)) break;

    uint32_t chunkSize = readU32(file);
    uint32_t chunkDataStart = file.position();

    if (strncmp(chunkId, "fmt ", 4) == 0) {
      info.audioFormat  = readU16(file);
      info.numChannels  = readU16(file);
      info.sampleRate   = readU32(file);

      uint32_t byteRate   = readU32(file);
      uint16_t blockAlign = readU16(file);
      info.bitsPerSample  = readU16(file);

      (void)byteRate;
      (void)blockAlign;

      foundFmt = true;
    } else if (strncmp(chunkId, "data", 4) == 0) {
      info.dataStart = chunkDataStart;
      info.dataSize  = chunkSize;
      foundData = true;
    }

    file.seek(chunkDataStart + chunkSize);

    if (foundFmt && foundData) break;
  }

  return foundFmt && foundData;
}

int16_t applyVolume(int16_t sample) {
  int32_t scaled = (int32_t)(sample * VOLUME);
  if (scaled >  32767) scaled =  32767;
  if (scaled < -32768) scaled = -32768;
  return (int16_t)scaled;
}

bool playWav(const char* path) {
  if (isPlaying) {
    Serial.println("Playback already in progress.");
    return false;
  }

  isPlaying = true;

  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.printf("File not found: %s\n", path);
    isPlaying = false;
    return false;
  }

  WavInfo wav;
  if (!parseWav(file, wav)) {
    Serial.println("Invalid WAV format.");
    file.close();
    isPlaying = false;
    return false;
  }

  Serial.printf("File: %s\n", path);
  Serial.printf("Format: %u  Channels: %u  SampleRate: %lu  Bits: %u\n",
                wav.audioFormat, wav.numChannels, wav.sampleRate, wav.bitsPerSample);

  if (wav.audioFormat != 1 || wav.bitsPerSample != 16) {
    Serial.println("Only 16-bit PCM WAV files are supported.");
    file.close();
    isPlaying = false;
    return false;
  }

  if (wav.numChannels != 1 && wav.numChannels != 2) {
    Serial.println("Only mono or stereo WAV files are supported.");
    file.close();
    isPlaying = false;
    return false;
  }

  setupI2S(wav.sampleRate);
  file.seek(wav.dataStart);

  const size_t INPUT_BUF_SIZE = 512;
  uint8_t inputBuffer[INPUT_BUF_SIZE];

  int16_t stereoBuffer[512];

  uint32_t remaining = wav.dataSize;

  while (remaining > 0) {
    size_t toRead    = remaining > INPUT_BUF_SIZE ? INPUT_BUF_SIZE : remaining;
    size_t bytesRead = file.read(inputBuffer, toRead);

    if (bytesRead == 0) break;

    remaining -= bytesRead;

    if (wav.numChannels == 1) {
      size_t samples  = bytesRead / 2;
      size_t outIndex = 0;

      for (size_t i = 0; i < samples; i++) {
        int16_t sample = ((int16_t*)inputBuffer)[i];
        sample = applyVolume(sample);

        stereoBuffer[outIndex++] = sample;
        stereoBuffer[outIndex++] = sample;

        if (outIndex >= 512) {
          size_t bytesWritten;
          i2s_write(I2S_PORT, stereoBuffer, outIndex * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
          outIndex = 0;
        }
      }

      if (outIndex > 0) {
        size_t bytesWritten;
        i2s_write(I2S_PORT, stereoBuffer, outIndex * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
      }
    } else {
      size_t samples    = bytesRead / 2;
      int16_t* samples16 = (int16_t*)inputBuffer;
      for (size_t i = 0; i < samples; i++) {
        samples16[i] = applyVolume(samples16[i]);
      }
      size_t bytesWritten;
      i2s_write(I2S_PORT, inputBuffer, bytesRead, &bytesWritten, portMAX_DELAY);
    }

    yield();
  }

  i2s_zero_dma_buffer(I2S_PORT);
  file.close();
  isPlaying = false;
  return true;
}

// =========================
// HTTP ROUTES
// =========================

String safeMessageToPath(String msg) {
  msg.trim();
  if (msg == "moins_fort")           return "/moins_fort.wav";
  if (msg == "retour_lit")           return "/retour_lit.wav";
  if (msg == "stop_courir")          return "/stop_courir.wav";
  if (msg == "separez_vous")         return "/separez_vous.wav";
  if (msg == "tout_va_bien")         return "/tout_va_bien.wav";
  if (msg == "papa_arrive")          return "/papa_arrive.wav";
  if (msg == "dernier_avertissement") return "/dernier_avertissement.wav";
  return "";
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", htmlPage());
}

void handlePlay() {
  if (!server.hasArg("msg")) {
    server.send(400, "text/plain", "Missing parameter: msg");
    return;
  }

  String msg  = server.arg("msg");
  String path = safeMessageToPath(msg);

  if (path == "") {
    server.send(404, "text/plain", "Unknown message");
    return;
  }

  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "Audio file missing: " + path);
    return;
  }

  server.send(200, "text/plain", "Playing: " + msg);
  delay(20);
  incrementLog();
  playWav(path.c_str());
}

void handleStatus() {
  String json = "{";
  json += "\"wifi\":true,";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"playing\":" + String(isPlaying ? "true" : "false") + ",";
  json += "\"volume\":" + String(VOLUME, 2);
  json += "}";
  server.send(200, "application/json", json);
}

void handleVolume() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing parameter: value");
    return;
  }
  float v = server.arg("value").toFloat();
  if (v < 0.0f || v > 3.0f) {
    server.send(400, "text/plain", "value must be 0.0-3.0");
    return;
  }
  VOLUME = v;
  Serial.printf("Volume set to %.2f\n", VOLUME);
  server.send(200, "text/plain", "Volume: " + String(VOLUME, 2));
}

void handleLog() {
  String html = "<!doctype html><html lang='fr'><head>"
    "<meta charset='utf-8'><title>Journal</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#111;color:#f5f5f5;margin:0;padding:20px}"
    "h1{font-size:1.5rem}a{color:#2f80ed}"
    "table{border-collapse:collapse;width:100%;max-width:400px;margin-top:16px}"
    "th,td{padding:10px 16px;text-align:left;border-bottom:1px solid #333}"
    "th{color:#bbb;font-weight:500}"
    "td:last-child{text-align:right;font-weight:bold}"
    "</style></head><body>"
    "<h1>Journal des messages</h1>"
    "<p><a href='/'>← Retour</a></p>";

  File f = LittleFS.open(LOG_FILE, "r");
  if (!f || f.size() == 0) {
    html += "<p style='color:#bbb'>Aucun message envoyé pour l'instant.</p>";
  } else {
    // Collect lines into an array (most recent first)
    String lines[64];
    int count = 0;
    while (f.available() && count < 64) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) lines[count++] = line;
    }
    f.close();

    html += "<table><tr><th>Date</th><th>Messages</th></tr>";
    for (int i = count - 1; i >= 0; i--) {
      int sp = lines[i].indexOf(' ');
      String date  = sp > 0 ? lines[i].substring(0, sp) : lines[i];
      String cnt   = sp > 0 ? lines[i].substring(sp + 1) : "?";
      html += "<tr><td>" + date + "</td><td>" + cnt + "</td></tr>";
    }
    html += "</table>";
  }

  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// =========================
// SETUP / LOOP
// =========================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("Starting children's intercom...");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS error.");
    return;
  }

  Serial.println("Files in LittleFS:");
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.printf("  - %s (%lu bytes)\n", file.name(), file.size());
    file = root.openNextFile();
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("Connecting to Wi-Fi: %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.printf("Connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // NTP time sync (France timezone, DST-aware)
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
  Serial.print("Syncing time");
  struct tm timeinfo;
  for (int i = 0; i < 10 && !getLocalTime(&timeinfo); i++) {
    delay(500);
    Serial.print(".");
  }
  if (getLocalTime(&timeinfo)) {
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("\nTime: %s\n", buf);
  } else {
    Serial.println("\nTime sync failed (log dates will show 'unknown').");
  }

  if (MDNS.begin(MDNS_NAME)) {
    Serial.printf("mDNS active: http://%s.local\n", MDNS_NAME);
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("mDNS error.");
  }

  setupI2S(16000);

  server.on("/",       HTTP_GET, handleRoot);
  server.on("/play",   HTTP_GET, handlePlay);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/volume", HTTP_GET, handleVolume);
  server.on("/log",    HTTP_GET, handleLog);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  server.handleClient();
}
