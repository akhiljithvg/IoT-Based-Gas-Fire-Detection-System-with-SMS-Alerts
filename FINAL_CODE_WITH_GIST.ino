#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <ArduinoJson.h>

#define BUZZER_PIN 23
#define GAS_SENSOR_ANALOG 34
#define GAS_SENSOR_DIGITAL 35
#define FLAME_SENSOR_PIN 32
#define LED 22
#define THRESHOLD 600

const char *ssid = "LLF@GGHSS"; // SSID
const char *password = "llf1234@"; // PASSWORD

String apiKey = "";  // Store API key dynamically
const char *templateID = "101";
const char *mobileNumber = "919550802200"; // MOBILE NUMBER

volatile bool wakeUpFlag = false;
unsigned long lastMessageTime = 0;
const unsigned long MESSAGE_INTERVAL = 20000;
unsigned long lastApiFetchTime = 0;
const unsigned long API_FETCH_INTERVAL = 3600000; // 10 seconds for testing (change to 1 hour in production)

const char *gistUrl = "https://gist.githubusercontent.com/akhiljithvg/f43d9ffd73969d57ef12a9a2377b7739/raw/apikey.json";

TaskHandle_t SensorTaskHandle, BuzzerTaskHandle;
WiFiClient client; // Global client

void IRAM_ATTR wakeUp() {
    wakeUpFlag = true;
}

// Function to fetch API key from Gist
void fetchApiKey() {
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure secureClient;
        secureClient.setInsecure();  // Ignore SSL verification

        HTTPClient http;
        Serial.println("🌐 Sending HTTPS GET request...");

        String url = String(gistUrl) + "?t=" + String(millis());  // Append timestamp to prevent caching
        http.useHTTP10(true); // Forces fresh connection

        http.begin(secureClient, url);
        int httpResponseCode = http.GET();

        Serial.print("📡 HTTP Response Code: ");
        Serial.println(httpResponseCode);

        if (httpResponseCode == 200) {
            String response = http.getString();
            Serial.println("🔹 Response:");
            Serial.println(response);

            // **Parse JSON**
            StaticJsonDocument<200> doc;
            DeserializationError error = deserializeJson(doc, response);

            if (!error) {
                apiKey = doc["api_key"].as<String>();  // ✅ Store in the global variable
                Serial.print("✅ Extracted API Key: ");
                Serial.println(apiKey);
            } else {
                Serial.println("❌ JSON Parsing Failed!");
            }
        } else {
            Serial.println("❌ API Fetch Failed.");
        }

        http.end();
    } else {
        Serial.println("❌ Not connected to WiFi.");
    }
}

void WiFiTask(void *parameter) {
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Connecting...");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_STA);
            WiFi.begin(ssid, password);

            unsigned long wifiStart = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
                delay(500);
                Serial.print(".");
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\n✅ WiFi Connected!");
                Serial.print("IP Address: ");
                Serial.println(WiFi.localIP());
                digitalWrite(LED, HIGH);
                fetchApiKey(); // Fetch API key on WiFi connection
            } else {
                Serial.println("\n❌ WiFi Connection Failed. Retrying...");
                digitalWrite(LED, LOW);
            }
        }

        // Fetch API key every 10 seconds (for testing)
        if (millis() - lastApiFetchTime > API_FETCH_INTERVAL) {
            Serial.println("🔄 Fetching API Key again...");
            fetchApiKey();
            lastApiFetchTime = millis();
        }

        delay(5000);
    }
}

void sendSMS(int gasValue) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        Serial.printf("🔍 Free Heap Before SMS: %d bytes\n", ESP.getFreeHeap());

        String apiUrl = "http://www.circuitdigest.cloud/send_sms?ID=" + String(templateID);
        http.begin(client, apiUrl);
        http.addHeader("Authorization", apiKey);  // ✅ Use updated API Key
        http.addHeader("Content-Type", "application/json");

        String payload = "{\"mobiles\":\"" + String(mobileNumber) + "\",\"var1\":\"GAS Level\",\"var2\":\"DANGER - LEAK DETECTED!\"}";
        int httpResponseCode = http.POST(payload);

        if (httpResponseCode == 200) {
            Serial.println("✅ SMS sent successfully!");
            Serial.println(http.getString());
        } else {
            Serial.print("❌ Failed to send SMS. Error: ");
            Serial.println(httpResponseCode);
        }
        
        http.end();
        client.stop(); // Ensure connection closes properly

        Serial.printf("🔍 Free Heap After SMS: %d bytes\n", ESP.getFreeHeap());
    } else {
        Serial.println("⚠️ WiFi not connected!");
    }
}

void SensorTask(void *parameter) {
    unsigned long gasDetectedStart = 0;
    bool gasContinuouslyDetected = false;
    int smsCount = 0;

    for (;;) {
        int gasValue = analogRead(GAS_SENSOR_ANALOG);
        int flameStatus = digitalRead(FLAME_SENSOR_PIN);
        unsigned long currentTime = millis();

        Serial.printf("[Sensor] Gas Level: %d | Flame Sensor: %d\n", gasValue, flameStatus);

        if (gasValue > THRESHOLD || flameStatus == LOW) {
            if (!gasContinuouslyDetected) {
                gasDetectedStart = currentTime;
                gasContinuouslyDetected = true;
                smsCount = 0;
            }

            if (gasContinuouslyDetected && (currentTime - gasDetectedStart >= 10000)) {
                Serial.println("⚠️ ALERT: Gas leak or Fire detected for 10 seconds!");
                xTaskNotifyGive(BuzzerTaskHandle);

                if (smsCount < 3 && (currentTime - lastMessageTime >= MESSAGE_INTERVAL)) {
                    sendSMS(gasValue);
                    lastMessageTime = currentTime;
                    smsCount++;
                } else if (smsCount >= 3) {
                    Serial.println("[INFO] Maximum 3 SMS sent. Skipping further alerts...");
                } else {
                    Serial.println("[INFO] Skipping SMS to avoid spam...");
                }
            }
        } else {
            if (gasContinuouslyDetected) {
                Serial.println("✅ Gas levels back to normal. Resetting SMS counter...");
            }
            gasContinuouslyDetected = false;
        }
        delay(2000);
    }
}

void BuzzerTask(void *parameter) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        Serial.println("[Buzzer] 🔊 Activating Buzzer!");

        unsigned long startTime = millis();
        while (millis() - startTime < 5000) {
            digitalWrite(BUZZER_PIN, HIGH);
            delay(500);
            digitalWrite(BUZZER_PIN, LOW);
            delay(500);
        }
        Serial.println("[Buzzer] 🔕 Buzzer Deactivated!");
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED, OUTPUT);
    pinMode(GAS_SENSOR_ANALOG, INPUT);
    pinMode(GAS_SENSOR_DIGITAL, INPUT_PULLUP);
    pinMode(FLAME_SENSOR_PIN, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(GAS_SENSOR_DIGITAL), wakeUp, FALLING);
    attachInterrupt(digitalPinToInterrupt(FLAME_SENSOR_PIN), wakeUp, FALLING);

    xTaskCreate(WiFiTask, "WiFiTask", 8192, NULL, 1, NULL);
    xTaskCreate(SensorTask, "SensorTask", 8192, NULL, 2, &SensorTaskHandle);
    xTaskCreate(BuzzerTask, "BuzzerTask", 4096, NULL, 3, &BuzzerTaskHandle);
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
