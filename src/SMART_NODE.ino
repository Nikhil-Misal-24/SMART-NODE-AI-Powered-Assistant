/*
   SMART-NODE
   AI Powered Voice Assistant

   Hardware:
   - ESP32
   - INMP441 MEMS Microphone
   - MAX98357A I2S Amplifier
   - OLED Display
   - Status LED

   Working:
   Mic -> ESP32 -> Gemini AI -> Gemini TTS -> Speaker
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include "mbedtls/base64.h"


// ======================================================
// WIFI AND GEMINI SETTINGS
// ======================================================

const char* WIFI_SSID = "EC";
const char* WIFI_PASSWORD = "ECE12345";

const char* GEMINI_API_KEY = "YOUR_GEMINI_API_KEY";


// ======================================================
// PIN CONNECTIONS
// ======================================================

// OLED
#define OLED_SDA 4
#define OLED_SCL 15

// Status LED
#define STATUS_LED 18

// INMP441 Microphone
#define MIC_BCLK 26
#define MIC_WS   25
#define MIC_SD   32

// MAX98357A Amplifier
#define SPK_BCLK 14
#define SPK_LRC  27
#define SPK_DIN  33


// ======================================================
// OLED SETTINGS
// ======================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);


// ======================================================
// AUDIO SETTINGS
// ======================================================

#define SAMPLE_RATE 16000
#define RECORD_SECONDS 2

#define AUDIO_SAMPLES (SAMPLE_RATE * RECORD_SECONDS)
#define AUDIO_BYTES   (AUDIO_SAMPLES * 2)

int16_t *recordedAudio = NULL;


// ======================================================
// FUNCTION DECLARATIONS
// ======================================================

void showMessage(String line1, String line2 = "");
void connectWiFi();

void setupMicrophone();
void setupSpeaker();
void stopI2S();

bool recordAudio();

String sendAudioToGemini();
bool speakWithGemini(String text);

String extractGeminiText(String response);

String base64Encode(uint8_t *data, size_t length);

void playPCM(uint8_t *audioData, size_t audioLength);


// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);


  // OLED initialization
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        0x3C
      )) {

    Serial.println("OLED initialization failed");

  }

  showMessage(
    "SMART-NODE",
    "Starting..."
  );

  delay(1500);


  // Connect WiFi
  connectWiFi();


  // Start microphone
  setupMicrophone();


  showMessage(
    "SMART-NODE",
    "Ready"
  );

  Serial.println();
  Serial.println("SMART-NODE is ready.");
  Serial.println("Speak after the LED turns ON.");
}


// ======================================================
// MAIN LOOP
// ======================================================

void loop() {

  showMessage(
    "Press Serial",
    "Send R to record"
  );


  // For testing from Serial Monitor
  if (Serial.available()) {

    char command = Serial.read();

    if (command == 'r' || command == 'R') {

      digitalWrite(STATUS_LED, HIGH);

      showMessage(
        "Listening...",
        "Speak now"
      );

      Serial.println("Recording started...");


      bool success = recordAudio();


      digitalWrite(STATUS_LED, LOW);


      if (!success) {

        showMessage(
          "Recording",
          "Failed"
        );

        delay(2000);

        return;
      }


      Serial.println("Recording completed.");


      showMessage(
        "Thinking...",
        "Please wait"
      );


      // Send recorded audio to Gemini
      String answer = sendAudioToGemini();


      if (answer.length() == 0) {

        showMessage(
          "No response",
          "Try again"
        );

        delay(2000);

        return;
      }


      Serial.println();
      Serial.println("Gemini Response:");
      Serial.println(answer);


      showMessage(
        "Answer Ready",
        "Speaking..."
      );


      // Speak answer
      bool spoken = speakWithGemini(answer);


      if (!spoken) {

        showMessage(
          "Speaker Error",
          "Try again"
        );

      }

      delay(1500);

      // Return to microphone mode
      setupMicrophone();

      showMessage(
        "SMART-NODE",
        "Ready"
      );
    }
  }
}


// ======================================================
// OLED MESSAGE FUNCTION
// ======================================================

void showMessage(
  String line1,
  String line2
) {

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);

  display.setCursor(0, 10);
  display.println(line1);

  display.setCursor(0, 35);
  display.println(line2);

  display.display();
}


// ======================================================
// WIFI CONNECTION
// ======================================================

void connectWiFi() {

  showMessage(
    "Connecting WiFi",
    "Please wait..."
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  Serial.print("Connecting to WiFi");

  int attempts = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    attempts < 30
  ) {

    delay(500);

    Serial.print(".");

    attempts++;
  }


  if (
    WiFi.status() == WL_CONNECTED
  ) {

    Serial.println();

    Serial.println(
      "WiFi Connected"
    );

    Serial.println(
      WiFi.localIP()
    );

    showMessage(
      "WiFi Connected",
      WiFi.localIP().toString()
    );

    delay(1500);

  }
  else {

    Serial.println();
    Serial.println(
      "WiFi connection failed"
    );

    showMessage(
      "WiFi Failed",
      "Check settings"
    );

    delay(3000);
  }
}


// ======================================================
// STOP I2S
// ======================================================

void stopI2S() {

  i2s_driver_uninstall(
    I2S_NUM_0
  );

  delay(100);
}


// ======================================================
// MICROPHONE SETUP
// ======================================================

void setupMicrophone() {

  stopI2S();


  i2s_config_t i2s_config = {

    .mode =
      (i2s_mode_t)(
        I2S_MODE_MASTER |
        I2S_MODE_RX
      ),

    .sample_rate =
      SAMPLE_RATE,

    .bits_per_sample =
      I2S_BITS_PER_SAMPLE_32BIT,

    .channel_format =
      I2S_CHANNEL_FMT_ONLY_LEFT,

    .communication_format =
      I2S_COMM_FORMAT_I2S,

    .intr_alloc_flags =
      ESP_INTR_FLAG_LEVEL1,

    .dma_buf_count =
      8,

    .dma_buf_len =
      256,

    .use_apll =
      false,

    .tx_desc_auto_clear =
      false,

    .fixed_mclk =
      0
  };


  i2s_pin_config_t pin_config = {

    .bck_io_num =
      MIC_BCLK,

    .ws_io_num =
      MIC_WS,

    .data_out_num =
      I2S_PIN_NO_CHANGE,

    .data_in_num =
      MIC_SD
  };


  i2s_driver_install(
    I2S_NUM_0,
    &i2s_config,
    0,
    NULL
  );


  i2s_set_pin(
    I2S_NUM_0,
    &pin_config
  );


  i2s_zero_dma_buffer(
    I2S_NUM_0
  );


  Serial.println(
    "Microphone ready"
  );
}


// ======================================================
// RECORD AUDIO
// ======================================================

bool recordAudio() {

  if (
    recordedAudio != NULL
  ) {

    free(recordedAudio);

    recordedAudio = NULL;
  }


  recordedAudio =
    (int16_t *)malloc(
      AUDIO_BYTES
    );


  if (
    recordedAudio == NULL
  ) {

    Serial.println(
      "Not enough memory"
    );

    return false;
  }


  size_t bytesRead = 0;

  int32_t rawSample = 0;


  for (
    int i = 0;
    i < AUDIO_SAMPLES;
    i++
  ) {

    i2s_read(
      I2S_NUM_0,
      &rawSample,
      sizeof(rawSample),
      &bytesRead,
      portMAX_DELAY
    );


    // Convert 32-bit microphone data
    // to 16-bit PCM audio

    int32_t sample =
      rawSample >> 14;


    // Limit values

    if (sample > 32767)
      sample = 32767;

    if (sample < -32768)
      sample = -32768;


    recordedAudio[i] =
      (int16_t)sample;
  }


  Serial.println(
    "Audio recorded"
  );


  return true;
}


// ======================================================
// BASE64 ENCODE FUNCTION
// ======================================================

String base64Encode(
  uint8_t *data,
  size_t length
) {

  size_t outputLength =
    0;


  size_t requiredLength =
    4 * (
      (length + 2) / 3
    ) + 1;


  unsigned char *output =
    (unsigned char *)malloc(
      requiredLength
    );


  if (
    output == NULL
  ) {

    return "";
  }


  int result =
    mbedtls_base64_encode(
      output,
      requiredLength,
      &outputLength,
      data,
      length
    );


  if (
    result != 0
  ) {

    free(output);

    return "";
  }


  output[outputLength] = '\0';


  String encoded =
    String(
      (char *)output
    );


  free(output);


  return encoded;
}


// ======================================================
// CREATE WAV HEADER
// ======================================================

void createWavHeader(
  uint8_t *header,
  uint32_t dataSize
) {

  uint32_t fileSize =
    dataSize + 36;

  uint32_t sampleRate =
    SAMPLE_RATE;

  uint16_t channels =
    1;

  uint16_t bitsPerSample =
    16;

  uint32_t byteRate =
    sampleRate *
    channels *
    bitsPerSample / 8;

  uint16_t blockAlign =
    channels *
    bitsPerSample / 8;


  memcpy(
    header,
    "RIFF",
    4
  );

  header[4] =
    fileSize & 0xFF;

  header[5] =
    (fileSize >> 8) & 0xFF;

  header[6] =
    (fileSize >> 16) & 0xFF;

  header[7] =
    (fileSize >> 24) & 0xFF;


  memcpy(
    header + 8,
    "WAVEfmt ",
    8
  );


  header[16] = 16;
  header[17] = 0;
  header[18] = 0;
  header[19] = 0;


  header[20] = 1;
  header[21] = 0;


  header[22] =
    channels & 0xFF;

  header[23] =
    (channels >> 8) & 0xFF;


  header[24] =
    sampleRate & 0xFF;

  header[25] =
    (sampleRate >> 8) & 0xFF;

  header[26] =
    (sampleRate >> 16) & 0xFF;

  header[27] =
    (sampleRate >> 24) & 0xFF;


  header[28] =
    byteRate & 0xFF;

  header[29] =
    (byteRate >> 8) & 0xFF;

  header[30] =
    (byteRate >> 16) & 0xFF;

  header[31] =
    (byteRate >> 24) & 0xFF;


  header[32] =
    blockAlign & 0xFF;

  header[33] =
    (blockAlign >> 8) & 0xFF;


  header[34] =
    bitsPerSample & 0xFF;

  header[35] =
    (bitsPerSample >> 8) & 0xFF;


  memcpy(
    header + 36,
    "data",
    4
  );


  header[40] =
    dataSize & 0xFF;

  header[41] =
    (dataSize >> 8) & 0xFF;

  header[42] =
    (dataSize >> 16) & 0xFF;

  header[43] =
    (dataSize >> 24) & 0xFF;
}


// ======================================================
// SEND AUDIO TO GEMINI
// ======================================================

String sendAudioToGemini() {

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    Serial.println(
      "WiFi not connected"
    );

    return "";
  }


  // Create WAV file in memory

  size_t wavSize =
    AUDIO_BYTES + 44;


  uint8_t *wavData =
    (uint8_t *)malloc(
      wavSize
    );


  if (
    wavData == NULL
  ) {

    Serial.println(
      "WAV memory error"
    );

    return "";
  }


  createWavHeader(
    wavData,
    AUDIO_BYTES
  );


  memcpy(
    wavData + 44,
    recordedAudio,
    AUDIO_BYTES
  );


  // Encode WAV as Base64

  String audioBase64 =
    base64Encode(
      wavData,
      wavSize
    );


  free(wavData);


  if (
    audioBase64.length() == 0
  ) {

    Serial.println(
      "Base64 encoding failed"
    );

    return "";
  }


  Serial.println(
    "Sending audio to Gemini..."
  );


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient http;


  String url =
    "https://generativelanguage.googleapis.com/"
    "v1beta/models/gemini-2.5-flash:generateContent?key=";

  url += GEMINI_API_KEY;


  http.begin(
    client,
    url
  );


  http.addHeader(
    "Content-Type",
    "application/json"
  );


  // Gemini request

  String requestBody =
    "{"
      "\"contents\":[{"
        "\"parts\":["
          "{"
            "\"inlineData\":{"
              "\"mimeType\":\"audio/wav\","
              "\"data\":\""
              + audioBase64 +
              "\""
            "}"
          "},"
          "{"
            "\"text\":"
            "\"Listen to the user's voice. Understand the question and answer naturally. "
            "Give a short and clear answer suitable for a voice assistant.\""
          "}"
        "]"
      "}]"
    "}";


  int httpCode =
    http.POST(
      requestBody
    );


  String response =
    "";


  if (
    httpCode > 0
  ) {

    Serial.print(
      "Gemini HTTP Code: "
    );

    Serial.println(
      httpCode
    );


    response =
      http.getString();


    Serial.println(
      response
    );

  }
  else {

    Serial.print(
      "HTTP Error: "
    );

    Serial.println(
      http.errorToString(
        httpCode
      )
    );
  }


  http.end();


  String answer =
    extractGeminiText(
      response
    );


  return answer;
}


// ======================================================
// EXTRACT GEMINI TEXT
// ======================================================

String extractGeminiText(
  String response
) {

  if (
    response.length() == 0
  ) {

    return "";
  }


  DynamicJsonDocument doc(
    16384
  );


  DeserializationError error =
    deserializeJson(
      doc,
      response
    );


  if (
    error
  ) {

    Serial.println(
      "JSON parsing failed"
    );

    return "";
  }


  String answer =
    doc[
      "candidates"
    ][0][
      "content"
    ][
      "parts"
    ][0][
      "text"
    ].as<String>();


  return answer;
}


// ======================================================
// SPEAKER SETUP
// ======================================================

void setupSpeaker() {

  stopI2S();


  i2s_config_t i2s_config = {

    .mode =
      (i2s_mode_t)(
        I2S_MODE_MASTER |
        I2S_MODE_TX
      ),

    .sample_rate =
      24000,

    .bits_per_sample =
      I2S_BITS_PER_SAMPLE_16BIT,

    .channel_format =
      I2S_CHANNEL_FMT_RIGHT_LEFT,

    .communication_format =
      I2S_COMM_FORMAT_I2S,

    .intr_alloc_flags =
      ESP_INTR_FLAG_LEVEL1,

    .dma_buf_count =
      8,

    .dma_buf_len =
      512,

    .use_apll =
      false,

    .tx_desc_auto_clear =
      true,

    .fixed_mclk =
      0
  };


  i2s_pin_config_t pin_config = {

    .bck_io_num =
      SPK_BCLK,

    .ws_io_num =
      SPK_LRC,

    .data_out_num =
      SPK_DIN,

    .data_in_num =
      I2S_PIN_NO_CHANGE
  };


  i2s_driver_install(
    I2S_NUM_0,
    &i2s_config,
    0,
    NULL
  );


  i2s_set_pin(
    I2S_NUM_0,
    &pin_config
  );


  i2s_zero_dma_buffer(
    I2S_NUM_0
  );


  Serial.println(
    "Speaker ready"
  );
}


// ======================================================
// GEMINI TEXT TO SPEECH
// ======================================================

bool speakWithGemini(
  String text
) {

  if (
    text.length() == 0
  ) {

    return false;
  }


  Serial.println(
    "Generating speech..."
  );


  setupSpeaker();


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient http;


  String url =
    "https://generativelanguage.googleapis.com/"
    "v1beta/models/gemini-2.5-flash-preview-tts:generateContent?key=";

  url += GEMINI_API_KEY;


  http.begin(
    client,
    url
  );


  http.addHeader(
    "Content-Type",
    "application/json"
  );


  // Escape quotation marks if present

  text.replace(
    "\"",
    "'"
  );


  String requestBody =
    "{"
      "\"contents\":[{"
        "\"parts\":[{"
          "\"text\":\""
          "Say naturally and clearly: "
          + text +
          "\""
        "}]"
      "}],"
      "\"generationConfig\":{"
        "\"responseModalities\":[\"AUDIO\"],"
        "\"speechConfig\":{"
          "\"voiceConfig\":{"
            "\"prebuiltVoiceConfig\":{"
              "\"voiceName\":\"Kore\""
            "}"
          "}"
        "}"
      "}"
    "}";


  int httpCode =
    http.POST(
      requestBody
    );


  if (
    httpCode <= 0
  ) {

    Serial.println(
      "TTS request failed"
    );

    http.end();

    return false;
  }


  String response =
    http.getString();


  http.end();


  DynamicJsonDocument doc(
    16384
  );


  DeserializationError error =
    deserializeJson(
      doc,
      response
    );


  if (
    error
  ) {

    Serial.println(
      "TTS JSON parsing failed"
    );

    return false;
  }


  String audioBase64 =
    doc[
      "candidates"
    ][0][
      "content"
    ][
      "parts"
    ][0][
      "inlineData"
    ][
      "data"
    ].as<String>();


  if (
    audioBase64.length() == 0
  ) {

    Serial.println(
      "No audio data received"
    );

    return false;
  }


  size_t decodedLength =
    (audioBase64.length() * 3) / 4 + 10;


  uint8_t *decodedAudio =
    (uint8_t *)malloc(
      decodedLength
    );


  if (
    decodedAudio == NULL
  ) {

    Serial.println(
      "Audio memory allocation failed"
    );

    return false;
  }


  size_t outputLength =
    0;


  int result =
    mbedtls_base64_decode(
      decodedAudio,
      decodedLength,
      &outputLength,
      (const unsigned char *)
        audioBase64.c_str(),
      audioBase64.length()
    );


  if (
    result != 0
  ) {

    Serial.println(
      "Audio Base64 decode failed"
    );

    free(decodedAudio);

    return false;
  }


  Serial.println(
    "Playing response..."
  );


  playPCM(
    decodedAudio,
    outputLength
  );


  free(
    decodedAudio
  );


  return true;
}


// ======================================================
// PLAY PCM AUDIO
// ======================================================

void playPCM(
  uint8_t *audioData,
  size_t audioLength
) {

  size_t bytesWritten =
    0;


  size_t position =
    0;


  while (
    position < audioLength
  ) {

    size_t chunkSize =
      1024;


    if (
      position + chunkSize >
      audioLength
    ) {

      chunkSize =
        audioLength -
        position;
    }


    i2s_write(
      I2S_NUM_0,
      audioData + position,
      chunkSize,
      &bytesWritten,
      portMAX_DELAY
    );


    position +=
      chunkSize;
  }


  delay(200);
}
