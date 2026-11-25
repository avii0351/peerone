#include <WiFi.h>
#include <Preferences.h>
#include <WebSocketsClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

#define WIFI_SSID "YOUR_WIFI_OR_HOTSPOT_SSID"
#define WIFI_PASS "YOUR_WIFI_OR_HOTSPOT_PASSWORD"

#define WS_HOST "api.fastpeer.ai"
#define WS_PORT 443
#define WS_PATH "/ws/botone"
#define USER_EMAIL "your-email@fastpeer.ai"
#define DEFAULT_DEVICE_TOKEN "YOUR_PERMANENT_DEVICE_TOKEN"

#define OLED_SDA 1
#define OLED_SCL 2
#define OLED_ADDR 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define TOUCH_PIN 3

#define I2S_MIC_SCK 42
#define I2S_MIC_WS 41
#define I2S_MIC_SD 40

#define I2S_SPK_BCLK 47
#define I2S_SPK_LRC 48
#define I2S_SPK_DOUT 21

#define SAMPLE_RATE 16000
#define AUDIO_CHUNK_BYTES 1024

enum BotState {
  STATE_BOOT,
  STATE_OFFLINE,
  STATE_IDLE,
  STATE_LISTENING,
  STATE_THINKING,
  STATE_SPEAKING
};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebSocketsClient webSocket;
Preferences prefs;

BotState currentState = STATE_BOOT;
String deviceToken = "";
bool isConnected = false;
bool isRecording = false;
unsigned long lastAudioRx = 0;

unsigned long lastAnimFrame = 0;
unsigned long lastBlink = 0;
unsigned long lastLookChange = 0;
bool eyesClosed = false;
int eyeOffsetX = 0;
int animStep = 0;
int audioLevel = 0;

int16_t micBuffer[AUDIO_CHUNK_BYTES / 2];

void drawStatus(const char* title, const char* subtitle) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 16);
  display.println(title);
  display.setTextSize(1);
  display.setCursor(10, 44);
  display.println(subtitle);
  display.display();
}

void drawIdleEyes(bool closed, int offsetX) {
  display.clearDisplay();
  int lx = 28 + offsetX;
  int rx = 74 + offsetX;
  if (closed) {
    display.fillRect(lx, 30, 26, 4, SSD1306_WHITE);
    display.fillRect(rx, 30, 26, 4, SSD1306_WHITE);
  } else {
    display.fillRoundRect(lx, 18, 26, 28, 7, SSD1306_WHITE);
    display.fillRoundRect(rx, 18, 26, 28, 7, SSD1306_WHITE);
  }
  display.display();
}

void drawListeningEyes(int step, int level) {
  display.clearDisplay();
  int pulse = (step % 2 == 0) ? 2 : 0;
  display.fillRoundRect(26, 10 - pulse, 30, 32 + pulse * 2, 9, SSD1306_WHITE);
  display.fillRoundRect(72, 10 - pulse, 30, 32 + pulse * 2, 9, SSD1306_WHITE);
  display.fillCircle(41, 26, 5, SSD1306_BLACK);
  display.fillCircle(87, 26, 5, SSD1306_BLACK);
  int barW = constrain(map(level, 0, 3000, 12, 84), 12, 84);
  display.fillRoundRect((SCREEN_WIDTH - barW) / 2, 54, barW, 5, 2, SSD1306_WHITE);
  display.display();
}

void drawThinkingEyes(int step) {
  display.clearDisplay();
  int offsets[4] = {-10, 0, 10, 0};
  int ox = offsets[step % 4];
  display.fillRoundRect(28 + ox, 14, 26, 20, 6, SSD1306_WHITE);
  display.fillRoundRect(74 + ox, 14, 26, 20, 6, SSD1306_WHITE);
  for (int i = 0; i < 3; i++) {
    int r = (step % 3 == i) ? 4 : 2;
    display.fillCircle(48 + i * 16, 52, r, SSD1306_WHITE);
  }
  display.display();
}

void drawSpeakingEyes(int step) {
  display.clearDisplay();
  int bounce = (step % 2 == 0) ? 0 : 2;
  display.fillRoundRect(28, 14 + bounce, 26, 22, 8, SSD1306_WHITE);
  display.fillRoundRect(74, 14 + bounce, 26, 22, 8, SSD1306_WHITE);
  display.fillRect(28, 28 + bounce, 26, 10, SSD1306_BLACK);
  display.fillRect(74, 28 + bounce, 26, 10, SSD1306_BLACK);
  int mouthH[4] = {4, 10, 14, 8};
  int mh = mouthH[step % 4];
  display.fillRoundRect(48, 52 - mh / 2, 32, mh, 3, SSD1306_WHITE);
  display.display();
}

void updateEyeAnimation() {
  unsigned long now = millis();

  if (currentState == STATE_IDLE) {
    if (!eyesClosed && now - lastBlink > 3200) {
      eyesClosed = true;
      lastBlink = now;
      drawIdleEyes(true, eyeOffsetX);
    } else if (eyesClosed && now - lastBlink > 140) {
      eyesClosed = false;
      lastBlink = now;
      drawIdleEyes(false, eyeOffsetX);
    } else if (!eyesClosed && now - lastLookChange > 2200) {
      int dirs[3] = {-8, 0, 8};
      eyeOffsetX = dirs[random(0, 3)];
      lastLookChange = now;
      drawIdleEyes(false, eyeOffsetX);
    }
  } else if (currentState == STATE_LISTENING) {
    if (now - lastAnimFrame > 90) {
      lastAnimFrame = now;
      animStep++;
      drawListeningEyes(animStep, audioLevel);
    }
  } else if (currentState == STATE_THINKING) {
    if (now - lastAnimFrame > 220) {
      lastAnimFrame = now;
      animStep++;
      drawThinkingEyes(animStep);
    }
  } else if (currentState == STATE_SPEAKING) {
    if (now - lastAnimFrame > 110) {
      lastAnimFrame = now;
      animStep++;
      drawSpeakingEyes(animStep);
    }
  }
}

void initI2SMic() {
  i2s_config_t micConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t micPins = {
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  };

  i2s_driver_install(I2S_NUM_0, &micConfig, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &micPins);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void initI2SSpeaker() {
  i2s_config_t spkConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t spkPins = {
    .bck_io_num = I2S_SPK_BCLK,
    .ws_io_num = I2S_SPK_LRC,
    .data_out_num = I2S_SPK_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_1, &spkConfig, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &spkPins);
  i2s_zero_dma_buffer(I2S_NUM_1);
}

String loadOrInitToken() {
  prefs.begin("botone", false);
  String saved = prefs.getString("token", "");
  if (saved.length() == 0) {
    saved = String(DEFAULT_DEVICE_TOKEN);
    prefs.putString("token", saved);
  }
  prefs.end();
  return saved;
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      isConnected = false;
      currentState = STATE_OFFLINE;
      drawStatus("Offline", "Reconnecting...");
      break;

    case WStype_CONNECTED:
      isConnected = true;
      currentState = STATE_IDLE;
      drawIdleEyes(false, 0);
      break;

    case WStype_TEXT:
      break;

    case WStype_BIN:
      currentState = STATE_SPEAKING;
      lastAudioRx = millis();
      size_t bytesWritten = 0;
      i2s_write(I2S_NUM_1, payload, length, &bytesWritten, portMAX_DELAY);
      break;

    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT);
  randomSeed(esp_random());

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  drawStatus("BotOne", "Booting...");

  initI2SMic();
  initI2SSpeaker();

  deviceToken = loadOrInitToken();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  drawStatus("Wi-Fi", "Connecting...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
  }
  WiFi.setSleep(false);

  drawStatus("Fastpeer", "Linking...");

  String fullPath = String(WS_PATH) + "?email=" + String(USER_EMAIL);
  String authHeader = "Authorization: Bearer " + deviceToken;

  webSocket.setExtraHeaders(authHeader.c_str());
  webSocket.beginSSL(WS_HOST, WS_PORT, fullPath.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
  webSocket.enableHeartbeat(15000, 3000, 2);
}

void loop() {
  webSocket.loop();

  bool touched = digitalRead(TOUCH_PIN) == HIGH;

  if (isConnected && touched && !isRecording) {
    isRecording = true;
    currentState = STATE_LISTENING;
    i2s_zero_dma_buffer(I2S_NUM_1);
    String startMsg = "{\"event\":\"start\",\"email\":\"" + String(USER_EMAIL) + "\",\"format\":\"pcm_16k_16bit_mono\"}";
    webSocket.sendTXT(startMsg);
  }

  if (isRecording && touched && isConnected) {
    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0, micBuffer, AUDIO_CHUNK_BYTES, &bytesRead, portMAX_DELAY);
    if (bytesRead > 0) {
      long sum = 0;
      int samples = bytesRead / 2;
      for (int i = 0; i < samples; i += 8) {
        sum += abs(micBuffer[i]);
      }
      audioLevel = sum / (samples / 8);
      webSocket.sendBIN((uint8_t*)micBuffer, bytesRead);
    }
  }

  if (isRecording && !touched) {
    isRecording = false;
    if (isConnected) {
      webSocket.sendTXT("{\"event\":\"stop\"}");
      currentState = STATE_THINKING;
    }
  }

  if (currentState == STATE_SPEAKING && (millis() - lastAudioRx > 600)) {
    currentState = STATE_IDLE;
    i2s_zero_dma_buffer(I2S_NUM_1);
    drawIdleEyes(false, 0);
  }

  updateEyeAnimation();
}