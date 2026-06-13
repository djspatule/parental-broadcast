#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include "driver/i2s.h"
#include <time.h>
#include <math.h>

// Forward-declared here so arduino-cli prototype injection finds it before use
struct WavInfo {
  uint16_t audioFormat, numChannels, bitsPerSample;
  uint32_t sampleRate, dataStart, dataSize;
};

// =========================
// CONFIGURATION
// =========================

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* MDNS_NAME = "interphone-enfants";

#define I2S_PORT I2S_NUM_0
#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DOUT 22

float VOLUME = 0.45;

// =========================
// GLOBALS
// =========================

WebServer server(80);
bool isPlaying = false;

#define LOG_FILE    "/playlog.txt"
#define LABELS_FILE "/labels.txt"

// Default labels — seeded into LABELS_FILE on first boot.
// Format: filename_stem=Display label[|css_class]
// css classes: (none)=blue, secondary=grey, warning=red
const char* DEFAULT_LABELS =
  "moins_fort=Moins fort\n"
  "retour_lit=Retour au lit\n"
  "stop_courir=Arrêtez de courir\n"
  "separez_vous=Séparez-vous\n"
  "tout_va_bien=Tout va bien|secondary\n"
  "papa_arrive=Parents arrivent mécontents|secondary\n"
  "dernier_avertissement=Dernier avertissement|warning\n";

// =========================
// LABELS (labels.txt)
// =========================

struct LabelEntry {
  String stem;   // filename without .wav
  String label;  // display text
  String cls;    // css class
};

static LabelEntry g_labels[32];
static int        g_labelCount = 0;

void loadLabels() {
  g_labelCount = 0;
  File f = LittleFS.open(LABELS_FILE, "r");
  if (!f) return;
  while (f.available() && g_labelCount < 32) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int eq = line.indexOf('=');
    if (eq < 1) continue;
    String stem = line.substring(0, eq);
    String rest = line.substring(eq + 1);
    String lbl, cls;
    int pipe = rest.indexOf('|');
    if (pipe >= 0) { lbl = rest.substring(0, pipe); cls = rest.substring(pipe + 1); }
    else           { lbl = rest; cls = ""; }
    g_labels[g_labelCount++] = {stem, lbl, cls};
  }
  f.close();
}

void saveLabels() {
  File f = LittleFS.open(LABELS_FILE, "w");
  if (!f) return;
  for (int i = 0; i < g_labelCount; i++) {
    f.print(g_labels[i].stem + "=" + g_labels[i].label);
    if (g_labels[i].cls.length()) f.print("|" + g_labels[i].cls);
    f.print("\n");
  }
  f.close();
}

// Returns index of stem in g_labels, or -1
int findLabel(const String& stem) {
  for (int i = 0; i < g_labelCount; i++)
    if (g_labels[i].stem == stem) return i;
  return -1;
}

String getLabelFor(const String& stem) {
  int i = findLabel(stem);
  return i >= 0 ? g_labels[i].label : stem;
}

void addLabel(const String& stem, const String& label) {
  int i = findLabel(stem);
  if (i >= 0) { g_labels[i].label = label; }
  else if (g_labelCount < 32) { g_labels[g_labelCount++] = {stem, label, ""}; }
  saveLabels();
}

void removeLabel(const String& stem) {
  int i = findLabel(stem);
  if (i < 0) return;
  for (int j = i; j < g_labelCount - 1; j++) g_labels[j] = g_labels[j + 1];
  g_labelCount--;
  saveLabels();
}

// =========================
// LOG (playlog.txt)
// =========================
// Line format: "YYYY-MM-DD total stem1:n1,stem2:n2\n"

String getToday() {
  struct tm t;
  if (!getLocalTime(&t)) return "unknown";
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
  return String(buf);
}

void incrementLog(const String& stem) {
  String today = getToday();
  if (today == "unknown") return;

  String content = "";
  File f = LittleFS.open(LOG_FILE, "r");
  if (f) { content = f.readString(); f.close(); }

  bool found = false;
  String output = "";
  int start = 0;

  while (start < (int)content.length()) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();
    start = nl + 1;
    if (line.length() == 0) continue;

    // Parse: date total detail
    int s1 = line.indexOf(' ');
    if (s1 < 0) { output += line + "\n"; continue; }
    String date = line.substring(0, s1);
    int s2 = line.indexOf(' ', s1 + 1);
    int total = 0;
    String detail = "";
    if (s2 < 0) { total = line.substring(s1 + 1).toInt(); }
    else        { total = line.substring(s1 + 1, s2).toInt(); detail = line.substring(s2 + 1); }

    if (date == today) {
      found = true;
      total++;
      // Update or add this stem in detail
      bool stemFound = false;
      String newDetail = "";
      int ds = 0;
      while (ds < (int)detail.length()) {
        int dc = detail.indexOf(',', ds);
        if (dc < 0) dc = detail.length();
        String pair = detail.substring(ds, dc);
        ds = dc + 1;
        if (pair.length() == 0) continue;
        int col = pair.indexOf(':');
        if (col < 0) { newDetail += (newDetail.length() ? "," : "") + pair; continue; }
        String pStem = pair.substring(0, col);
        int pCount   = pair.substring(col + 1).toInt();
        if (pStem == stem) { pCount++; stemFound = true; }
        newDetail += (newDetail.length() ? "," : "") + pStem + ":" + String(pCount);
      }
      if (!stemFound) newDetail += (newDetail.length() ? "," : "") + stem + ":1";
      output += date + " " + String(total) + " " + newDetail + "\n";
    } else {
      output += line + "\n";
    }
  }

  if (!found) output += today + " 1 " + stem + ":1\n";

  File fw = LittleFS.open(LOG_FILE, "w");
  if (fw) { fw.print(output); fw.close(); }
}

// =========================
// I2S SETUP
// =========================

void setupI2S(int sampleRate) {
  i2s_driver_uninstall(I2S_PORT);
  i2s_config_t cfg = {
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
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num  = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };
  if (i2s_driver_install(I2S_PORT, &cfg, 0, NULL) != ESP_OK) return;
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);
}

// =========================
// CHIME (synthesised — no WAV file needed)
// =========================

void playChime() {
  const int SR = 16000;
  // Two tones: 880 Hz then 660 Hz, 180 ms each, with fade envelope
  float freqs[2] = {880.0f, 660.0f};
  const int TONE_SAMPLES = SR * 180 / 1000;   // 2880 samples per tone
  const int FADE         = SR * 30  / 1000;   // 30 ms fade in/out
  const int GAP          = SR * 40  / 1000;   // 40 ms silence between tones

  int16_t buf[128]; // stereo: 64 frames × 2 ch

  for (int t = 0; t < 2; t++) {
    float freq = freqs[t];
    int s = 0;
    while (s < TONE_SAMPLES) {
      int batch = min(64, TONE_SAMPLES - s);
      for (int i = 0; i < batch; i++) {
        int pos = s + i;
        float env = 1.0f;
        if (pos < FADE)                        env = (float)pos / FADE;
        else if (pos > TONE_SAMPLES - FADE)    env = (float)(TONE_SAMPLES - pos) / FADE;
        float v = sinf(2.0f * M_PI * freq * pos / SR) * env * VOLUME * 0.55f;
        int16_t sample = (int16_t)(v * 32767.0f);
        buf[i * 2]     = sample;
        buf[i * 2 + 1] = sample;
      }
      size_t written;
      i2s_write(I2S_PORT, buf, batch * 4, &written, portMAX_DELAY);
      s += batch;
    }
    // Gap
    memset(buf, 0, sizeof(buf));
    for (int g = 0; g < GAP; g += 64) {
      int batch = min(64, GAP - g);
      size_t written;
      i2s_write(I2S_PORT, buf, batch * 4, &written, portMAX_DELAY);
    }
  }
}

// =========================
// WAV PLAYER
// =========================

static bool readBytes(File& f, void* b, size_t n) { return f.read((uint8_t*)b, n) == n; }

static uint32_t readU32(File& f) {
  uint8_t b[4]; if (!readBytes(f, b, 4)) return 0;
  return b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
}
static uint16_t readU16(File& f) {
  uint8_t b[2]; if (!readBytes(f, b, 2)) return 0;
  return b[0] | (b[1]<<8);
}

bool parseWav(File& f, WavInfo& info) {
  char riff[4], wave[4];
  f.seek(0);
  if (!readBytes(f, riff, 4)) return false;
  readU32(f); // riff size
  if (!readBytes(f, wave, 4)) return false;
  if (strncmp(riff, "RIFF", 4) || strncmp(wave, "WAVE", 4)) return false;

  bool gotFmt = false, gotData = false;
  while (f.available()) {
    char id[4]; if (!readBytes(f, id, 4)) break;
    uint32_t sz = readU32(f);
    uint32_t pos = f.position();
    if (!strncmp(id, "fmt ", 4)) {
      info.audioFormat  = readU16(f);
      info.numChannels  = readU16(f);
      info.sampleRate   = readU32(f);
      readU32(f); readU16(f); // byteRate, blockAlign
      info.bitsPerSample = readU16(f);
      gotFmt = true;
    } else if (!strncmp(id, "data", 4)) {
      info.dataStart = pos;
      info.dataSize  = sz;
      gotData = true;
    }
    f.seek(pos + sz);
    if (gotFmt && gotData) break;
  }
  return gotFmt && gotData;
}

int16_t applyVolume(int16_t s) {
  int32_t v = (int32_t)(s * VOLUME);
  if (v >  32767) v =  32767;
  if (v < -32768) v = -32768;
  return (int16_t)v;
}

bool playWav(const char* path) {
  if (isPlaying) return false;
  isPlaying = true;

  File f = LittleFS.open(path, "r");
  if (!f) { Serial.printf("File not found: %s\n", path); isPlaying = false; return false; }

  WavInfo wav;
  if (!parseWav(f, wav) || wav.audioFormat != 1 || wav.bitsPerSample != 16) {
    Serial.println("Unsupported WAV format.");
    f.close(); isPlaying = false; return false;
  }

  Serial.printf("Playing: %s (%lu Hz, %u ch)\n", path, wav.sampleRate, wav.numChannels);
  setupI2S(wav.sampleRate);
  f.seek(wav.dataStart);

  uint8_t inBuf[512];
  int16_t outBuf[512];
  uint32_t remaining = wav.dataSize;

  while (remaining > 0) {
    size_t toRead = remaining > 512 ? 512 : remaining;
    size_t got    = f.read(inBuf, toRead);
    if (got == 0) break;
    remaining -= got;

    size_t written;
    if (wav.numChannels == 1) {
      size_t samples = got / 2, out = 0;
      for (size_t i = 0; i < samples; i++) {
        int16_t s = applyVolume(((int16_t*)inBuf)[i]);
        outBuf[out++] = s; outBuf[out++] = s;
        if (out >= 512) { i2s_write(I2S_PORT, outBuf, out*2, &written, portMAX_DELAY); out = 0; }
      }
      if (out) i2s_write(I2S_PORT, outBuf, out*2, &written, portMAX_DELAY);
    } else {
      int16_t* s16 = (int16_t*)inBuf;
      for (size_t i = 0; i < got/2; i++) s16[i] = applyVolume(s16[i]);
      i2s_write(I2S_PORT, inBuf, got, &written, portMAX_DELAY);
    }
    yield();
  }

  i2s_zero_dma_buffer(I2S_PORT);
  f.close();
  isPlaying = false;
  return true;
}

// =========================
// SECURITY
// =========================

bool isSafeStem(const String& stem) {
  if (stem.length() == 0 || stem.length() > 40) return false;
  for (int i = 0; i < (int)stem.length(); i++) {
    char c = stem[i];
    if (!isalnum(c) && c != '_' && c != '-') return false;
  }
  return true;
}

// =========================
// HTML — MAIN PAGE (dynamic)
// =========================

String htmlPage() {
  String html = F("<!doctype html><html lang='fr'><head>"
    "<meta charset='utf-8'><title>Interphone enfants</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    ":root{font-family:system-ui,-apple-system,sans-serif}"
    "body{margin:0;background:#111;color:#f5f5f5}"
    "main{max-width:520px;margin:0 auto;padding:20px}"
    "h1{font-size:1.8rem;margin-bottom:.3rem}"
    "p{color:#bbb;margin-top:0;margin-bottom:1.5rem}"
    "button{width:100%;border:0;border-radius:14px;padding:18px 16px;margin:8px 0;"
           "font-size:1.2rem;font-weight:650;background:#2f80ed;color:#fff;"
           "box-shadow:0 4px 12px rgba(0,0,0,.25);cursor:pointer}"
    "button:active{transform:scale(.98);background:#1c65c9}"
    ".secondary{background:#444}"
    ".warning{background:#b84040}"
    ".volume-row{display:flex;align-items:center;gap:12px;margin:20px 0 8px}"
    ".volume-row label{white-space:nowrap;color:#bbb;font-size:.95rem;min-width:90px}"
    "input[type=range]{flex:1;accent-color:#2f80ed}"
    "#vol-val{color:#f5f5f5;min-width:36px;text-align:right;font-size:.95rem}"
    "#status{margin-top:16px;padding:12px;background:#222;border-radius:10px;color:#ddd;min-height:1.2rem}"
    ".footer-links{display:flex;gap:20px;justify-content:center;margin-top:20px}"
    ".footer-links a{color:#888;font-size:.9rem;text-decoration:none}"
    ".footer-links a:hover{color:#bbb}"
    "</style></head><body><main>"
    "<h1>Chambre enfants</h1>"
    "<p>Appuie sur un bouton pour diffuser un message.</p>");

  for (int i = 0; i < g_labelCount; i++) {
    String path = "/" + g_labels[i].stem + ".wav";
    if (!LittleFS.exists(path)) continue;
    String cls = g_labels[i].cls.length() ? " class='" + g_labels[i].cls + "'" : "";
    html += "<button" + cls + " onclick=\"play('" + g_labels[i].stem + "')\">"
          + g_labels[i].label + "</button>\n";
  }

  html += F("<div class='volume-row'>"
    "<label for='vol'>Volume&nbsp;:</label>"
    "<input type='range' id='vol' min='0' max='3' step='0.05' value='0.45'"
    " oninput='updateVolDisplay(this.value)' onchange='setVolume(this.value)'>"
    "<span id='vol-val'>0.45</span></div>"
    "<div id='status'>Prêt.</div>"
    "<div class='footer-links'>"
    "<a href='/log'>📋 Journal</a>"
    "<a href='/manage'>⚙️ Gérer les messages</a>"
    "</div>"
    "</main><script>"
    "async function play(msg){"
      "const s=document.getElementById('status');"
      "s.textContent='Envoi : '+msg+'...';"
      "try{"
        "const r=await fetch('/play?msg='+encodeURIComponent(msg));"
        "s.textContent=r.ok?'Message envoyé.':'Erreur : '+(await r.text());"
      "}catch(e){s.textContent='Impossible de joindre l\\'ESP32.';}"
    "}"
    "function updateVolDisplay(v){document.getElementById('vol-val').textContent=parseFloat(v).toFixed(2);}"
    "async function setVolume(v){updateVolDisplay(v);await fetch('/volume?value='+v);}"
    "fetch('/status').then(r=>r.json()).then(d=>{"
      "if(d.volume!==undefined){document.getElementById('vol').value=d.volume;updateVolDisplay(d.volume);}"
    "}).catch(()=>{});"
    "</script></body></html>");

  return html;
}

// =========================
// HTML — LOG PAGE
// =========================

String htmlLog() {
  String html = F("<!doctype html><html lang='fr'><head>"
    "<meta charset='utf-8'><title>Journal</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#111;color:#f5f5f5;margin:0;padding:20px}"
    "h1{font-size:1.5rem}a{color:#2f80ed}"
    "table{border-collapse:collapse;width:100%;max-width:600px;margin-top:16px}"
    "th,td{padding:10px 14px;text-align:left;border-bottom:1px solid #333;vertical-align:top}"
    "th{color:#bbb;font-weight:500}"
    ".total{font-weight:bold;white-space:nowrap}"
    ".detail{color:#bbb;font-size:.9rem;line-height:1.6}"
    "</style></head><body>"
    "<h1>Journal des messages</h1>"
    "<p><a href='/'>← Retour</a></p>");

  File f = LittleFS.open(LOG_FILE, "r");
  if (!f || f.size() == 0) {
    html += "<p style='color:#bbb'>Aucun message envoyé pour l'instant.</p>";
    if (f) f.close();
    html += "</body></html>";
    return html;
  }

  // Collect lines
  String lines[64]; int count = 0;
  while (f.available() && count < 64) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length()) lines[count++] = line;
  }
  f.close();

  html += "<table><tr><th>Date</th><th>Total</th><th>Détail</th></tr>";
  for (int i = count - 1; i >= 0; i--) {
    int s1 = lines[i].indexOf(' ');
    if (s1 < 0) continue;
    int s2 = lines[i].indexOf(' ', s1 + 1);
    String date   = lines[i].substring(0, s1);
    String total  = s2 < 0 ? lines[i].substring(s1 + 1) : lines[i].substring(s1 + 1, s2);
    String detail = s2 < 0 ? "" : lines[i].substring(s2 + 1);

    // Build friendly detail string
    String friendlyDetail = "";
    int ds = 0;
    while (ds < (int)detail.length()) {
      int dc = detail.indexOf(',', ds);
      if (dc < 0) dc = detail.length();
      String pair = detail.substring(ds, dc); ds = dc + 1;
      int col = pair.indexOf(':');
      if (col < 0) continue;
      String stem = pair.substring(0, col);
      String cnt  = pair.substring(col + 1);
      if (friendlyDetail.length()) friendlyDetail += "<br>";
      friendlyDetail += getLabelFor(stem) + " &times;" + cnt;
    }

    html += "<tr><td>" + date + "</td><td class='total'>" + total
          + "</td><td class='detail'>" + (friendlyDetail.length() ? friendlyDetail : "-")
          + "</td></tr>";
  }
  html += "</table></body></html>";
  return html;
}

// =========================
// HTML — MANAGE PAGE
// =========================

String htmlManage() {
  size_t used  = LittleFS.usedBytes();
  size_t total = LittleFS.totalBytes();
  size_t free  = total - used;

  String html = F("<!doctype html><html lang='fr'><head>"
    "<meta charset='utf-8'><title>Gérer les messages</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#111;color:#f5f5f5;margin:0;padding:20px}"
    "h1,h2{font-size:1.3rem}a{color:#2f80ed}"
    "table{border-collapse:collapse;width:100%;max-width:520px;margin:12px 0}"
    "th,td{padding:9px 12px;text-align:left;border-bottom:1px solid #333}"
    "th{color:#bbb;font-weight:500}"
    ".del{color:#e05;font-size:.85rem;text-decoration:none}"
    ".del:hover{text-decoration:underline}"
    "form{max-width:520px;margin-top:20px}"
    "label{display:block;color:#bbb;font-size:.9rem;margin:12px 0 4px}"
    "input[type=text],input[type=file]{width:100%;box-sizing:border-box;"
      "background:#222;border:1px solid #444;border-radius:8px;"
      "padding:10px;color:#f5f5f5;font-size:1rem}"
    "button[type=submit]{margin-top:14px;padding:12px 24px;background:#2f80ed;"
      "color:#fff;border:0;border-radius:10px;font-size:1rem;cursor:pointer}"
    ".space{color:#888;font-size:.85rem;margin-bottom:4px}"
    ".warn{color:#e88}"
    "</style></head><body>"
    "<h1>⚙️ Gérer les messages</h1>"
    "<p><a href='/'>← Retour</a></p>");

  // Space indicator
  html += "<p class='space'>Espace utilisé : " + String(used / 1024) + " Ko / "
        + String(total / 1024) + " Ko — libre : " + String(free / 1024) + " Ko</p>";
  if (free < 50000)
    html += "<p class='warn'>⚠️ Moins de 50 Ko libres — supprimez des messages avant d'en ajouter.</p>";

  // Current messages table
  html += "<h2>Messages actuels</h2>"
          "<table><tr><th>Bouton</th><th>Fichier</th><th></th></tr>";
  for (int i = 0; i < g_labelCount; i++) {
    String path = "/" + g_labels[i].stem + ".wav";
    if (!LittleFS.exists(path)) continue;
    File wf = LittleFS.open(path, "r");
    String size = wf ? String(wf.size() / 1024) + " Ko" : "?";
    if (wf) wf.close();
    html += "<tr><td>" + g_labels[i].label + "</td><td>" + g_labels[i].stem
          + ".wav (" + size + ")</td>"
          + "<td><a class='del' href='/delete?file=" + g_labels[i].stem
          + "' onclick=\"return confirm('Supprimer " + g_labels[i].label + " ?')\">Supprimer</a></td></tr>";
  }
  html += "</table>";

  // Upload form
  html += F("<h2>Ajouter un message</h2>"
    "<form action='/upload' method='POST' enctype='multipart/form-data'>"
    "<label>Nom du bouton</label>"
    "<input type='text' name='label' placeholder='Ex : Dîner prêt' required maxlength='40'>"
    "<label>Fichier WAV <span style='color:#888'>(mono, 16 bits, 16 000 Hz)</span></label>"
    "<input type='file' name='file' accept='.wav' required>"
    "<button type='submit'>Envoyer</button>"
    "</form>"
    "</body></html>");

  return html;
}

// =========================
// HTTP HANDLERS
// =========================

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", htmlPage());
}

void handlePlay() {
  if (!server.hasArg("msg")) {
    server.send(400, "text/plain", "Missing: msg"); return;
  }
  String stem = server.arg("msg");
  if (!isSafeStem(stem)) {
    server.send(400, "text/plain", "Invalid message name"); return;
  }
  String path = "/" + stem + ".wav";
  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "Audio file missing: " + path); return;
  }
  server.send(200, "text/plain", "Playing: " + stem);
  delay(20);
  incrementLog(stem);
  setupI2S(16000);
  playChime();
  playWav(path.c_str());
}

void handleStatus() {
  server.send(200, "application/json",
    "{\"wifi\":true,\"ip\":\"" + WiFi.localIP().toString() + "\","
    "\"playing\":" + String(isPlaying ? "true" : "false") + ","
    "\"volume\":" + String(VOLUME, 2) + "}");
}

void handleVolume() {
  if (!server.hasArg("value")) { server.send(400, "text/plain", "Missing: value"); return; }
  float v = server.arg("value").toFloat();
  if (v < 0.0f || v > 3.0f) { server.send(400, "text/plain", "value must be 0.0-3.0"); return; }
  VOLUME = v;
  Serial.printf("Volume: %.2f\n", VOLUME);
  server.send(200, "text/plain", String(VOLUME, 2));
}

void handleLog() {
  server.send(200, "text/html; charset=utf-8", htmlLog());
}

void handleManage() {
  server.send(200, "text/html; charset=utf-8", htmlManage());
}

void handleDelete() {
  if (!server.hasArg("file")) { server.send(400, "text/plain", "Missing: file"); return; }
  String stem = server.arg("file");
  if (!isSafeStem(stem)) { server.send(400, "text/plain", "Invalid name"); return; }
  String path = "/" + stem + ".wav";
  if (LittleFS.exists(path)) LittleFS.remove(path);
  removeLabel(stem);
  server.sendHeader("Location", "/manage");
  server.send(303, "text/plain", "Deleted");
}

// Upload globals
static File    g_uploadFile;
static String  g_uploadStem = "";
static bool    g_uploadOk   = false;

String sanitizeStem(String name) {
  // Strip extension
  int dot = name.lastIndexOf('.');
  if (dot > 0) name = name.substring(0, dot);
  name.toLowerCase();
  String out = "";
  for (int i = 0; i < (int)name.length() && out.length() < 32; i++) {
    char c = name[i];
    if (isalnum(c))           out += c;
    else if (c==' '||c=='-') out += '_';
  }
  if (out.length() == 0) out = "msg_" + String(millis());
  return out;
}

void handleUploadChunk() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    g_uploadStem = sanitizeStem(upload.filename);
    g_uploadOk   = false;

    size_t free = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (free < 40000) {
      Serial.println("Upload refused: not enough space");
      return;
    }
    String path = "/" + g_uploadStem + ".wav";
    g_uploadFile = LittleFS.open(path, "w");
    g_uploadOk   = (bool)g_uploadFile;
    Serial.printf("Upload start: %s\n", path.c_str());

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (g_uploadOk && g_uploadFile)
      g_uploadFile.write(upload.buf, upload.currentSize);

  } else if (upload.status == UPLOAD_FILE_END) {
    if (g_uploadFile) g_uploadFile.close();
    Serial.printf("Upload done: %u bytes\n", upload.totalSize);
  }
}

void handleUploadDone() {
  if (!g_uploadOk || g_uploadStem.length() == 0) {
    server.send(500, "text/html; charset=utf-8",
      "<p>Échec de l'envoi (espace insuffisant ou erreur). <a href='/manage'>Retour</a></p>");
    return;
  }
  String label = server.arg("label");
  if (label.length() == 0) label = g_uploadStem;
  addLabel(g_uploadStem, label);
  loadLabels(); // refresh in-memory labels
  server.sendHeader("Location", "/manage");
  server.send(303, "text/plain", "OK");
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
  Serial.println("\nStarting children's intercom...");

  if (!LittleFS.begin(true)) { Serial.println("LittleFS error."); return; }

  // Seed labels file if missing
  if (!LittleFS.exists(LABELS_FILE)) {
    File f = LittleFS.open(LABELS_FILE, "w");
    if (f) { f.print(DEFAULT_LABELS); f.close(); }
  }
  loadLabels();

  // Print files
  Serial.println("Files in LittleFS:");
  File root = LittleFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile())
    Serial.printf("  %s (%lu bytes)\n", f.name(), f.size());

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());

  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
  Serial.print("Syncing time");
  struct tm ti;
  for (int i = 0; i < 10 && !getLocalTime(&ti); i++) { delay(500); Serial.print("."); }
  if (getLocalTime(&ti)) {
    char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
    Serial.printf("\nTime: %s\n", buf);
  } else {
    Serial.println("\nTime sync failed.");
  }

  if (MDNS.begin(MDNS_NAME)) {
    Serial.printf("mDNS: http://%s.local\n", MDNS_NAME);
    MDNS.addService("http", "tcp", 80);
  }

  setupI2S(16000);

  server.on("/",        HTTP_GET,  handleRoot);
  server.on("/play",    HTTP_GET,  handlePlay);
  server.on("/status",  HTTP_GET,  handleStatus);
  server.on("/volume",  HTTP_GET,  handleVolume);
  server.on("/log",     HTTP_GET,  handleLog);
  server.on("/manage",  HTTP_GET,  handleManage);
  server.on("/delete",  HTTP_GET,  handleDelete);
  server.on("/upload",  HTTP_POST, handleUploadDone, handleUploadChunk);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  server.handleClient();
}
