#include <WiFi.h>
#include <WebServer.h>
#include "EmonLib.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP_Mail_Client.h>

// Hardware Configuration
#define VOLTAGE_PIN 35
#define CURRENT_PIN 34
#define BUZZER_PIN 5
#define BUTTON_PIN 4
#define RED_LED 18
#define YELLOW_LED 19
#define GREEN_LED 21
#define vCalibration 90.0
#define currCalibration 6.0



// Network Configuration
const char* wifi_ssid = "SAROVARAM 4G 2";
const char* wifi_password = "200C86EA8370";
const char* ap_ssid = "ESP32_EnergyMeter";
const char* ap_password = "12345678";

// Email Configuration
#define SMTP_server "smtp.gmail.com"
#define SMTP_Port 465
#define sender_email "smartenergymetermeter@gmail.com"
#define sender_password "dnys ujvo fnps wwtj"
#define Recipient_email "akkuachu204@gmail.com"
#define Recipient_name "Athul"

// Global Objects
EnergyMonitor emon;
WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);
SMTPSession smtp;

// Energy Variables
float voltage = 0, current = 0, power = 0, kWh = 0, cost = 0;
String currentSlab = "Normal";
String lastSentSlab = "";
bool powerAlertSent = false;
bool dailyEnergyAlertSent = false;
bool peakAlertSent = false;
unsigned long lastMillis = 0;
int lastDay = -1;

// Web Dashboard HTML
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Energy Meter </title>
    <style>
        body { 
            font-family: 'Poppins', sans-serif;
            text-align: center; 
            background: linear-gradient(135deg, #1e1e2e, #3a3a55);
            color: #ffffff; 
            margin: 0; 
            padding: 20px;
        }
        .container { 
            display: grid; 
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); 
            gap: 20px; 
            padding: 20px; 
            max-width: 1200px;
            margin: auto;
        }
        .box { 
            padding: 20px; 
            border-radius: 15px; 
            box-shadow: 0 6px 12px rgba(0, 0, 0, 0.3);
            background: rgba(255, 255, 255, 0.1);
            backdrop-filter: blur(10px);
            color: white;
            font-size: 1.2em;
            transition: transform 0.3s ease-in-out;
        }
        .box:hover {
            transform: translateY(-5px);
        }
        .normal { background: rgba(0, 200, 83, 0.8); }
        .peak { background: rgba(244, 67, 54, 0.8); }
        .offpeak { background: rgba(255, 152, 0, 0.8); }
    </style>
</head>
<body>
    <h1>Smart Energy Meter </h1>
    <div class="container">
        <div class="box" id="slabBox">📊 Current Slab: <span id="slab"></spa
        ++n></div>
        <div class="box">⚡ Voltage: <span id="voltage"></span> V</div>
        <div class="box">🔌 Current: <span id="current"></span> A</div>
        <div class="box">💡 Power: <span id="power"></span> W</div>
        <div class="box">🔋 Energy: <span id="energy"></span> kWh</div>
        <div class="box">💰 Cost: ₹<span id="cost"></span></div>
        <div class="box">⏰ Time: <span id="time"></span></div>
    </div>
    <footer>
        <p>&copy; 2025 Smart Energy Meter ⚡. All Rights Reserved.</p>
    </footer>
    <script>
        function updateData() {
            fetch("/data")
                .then(response => response.json())
                .then(data => {
                    document.getElementById("voltage").innerText = data.voltage.toFixed(2);
                    document.getElementById("current").innerText = data.current.toFixed(2);
                    document.getElementById("power").innerText = data.power.toFixed(2);
                    document.getElementById("energy").innerText = data.energy.toFixed(3);
                    document.getElementById("cost").innerText = data.cost.toFixed(2);
                    document.getElementById("slab").innerText = data.slab;
                    document.getElementById("time").innerText = data.time;
                    
                    let slabBox = document.getElementById("slabBox");
                    slabBox.className = "box " + (data.slab === "Normal" ? "normal" : data.slab === "Peak" ? "peak" : "offpeak");
                })
                .catch(error => console.log("Error fetching data: ", error));
        }
        setInterval(updateData, 2000);
    </script>
</body>
</html>
)rawliteral";

void triggerBuzzer() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(2000);
    digitalWrite(BUZZER_PIN, LOW);
}

void updateLEDs() {
    digitalWrite(RED_LED, currentSlab == "Peak");
    digitalWrite(YELLOW_LED, currentSlab == "Off-Peak");
    digitalWrite(GREEN_LED, currentSlab == "Normal");
}

void sendEmailAlert(String alertType) {
    ESP_Mail_Session session;
    session.server.host_name = SMTP_server;
    session.server.port = SMTP_Port;
    session.login.email = sender_email;
    session.login.password = sender_password;
    session.login.user_domain = "";

    SMTP_Message message;
    message.sender.name = "ESP32 Smart Meter";
    message.sender.email = sender_email;
    message.subject = "Energy Alert: " + alertType;
    message.addRecipient(Recipient_name, Recipient_email);

    String color;
    if (currentSlab == "Peak") color = "red";
    else if (currentSlab == "Off-Peak") color = "orange";
    else color = "green";

    String htmlMsg = "<div style='color:#000000;padding:20px;'>";
    htmlMsg += "<h1 style='color:" + color + ";'>Energy Alert: " + alertType + "</h1>";
    htmlMsg += "<p>Time: " + timeClient.getFormattedTime() + "</p>";
    htmlMsg += "<p>Voltage: " + String(voltage, 2) + " V</p>";
    htmlMsg += "<p>Current: " + String(current, 2) + " A</p>";
    htmlMsg += "<p>Power: " + String(power, 2) + " W</p>";
    htmlMsg += "<p>Energy: " + String(kWh, 3) + " kWh</p>";
    htmlMsg += "<p>Cost: ₹" + String(cost, 2) + "</p>";
    htmlMsg += "<p>Current Slab: <b style='color:" + color + ";'>" + currentSlab + "</b></p>";
    htmlMsg += "</div>";

    message.html.content = htmlMsg.c_str();
    message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

    if (!smtp.connect(&session)) return;
    if (!MailClient.sendMail(&smtp, &message)) {
        Serial.println("Error sending Email: " + smtp.errorReason());
    }
}

void readSensors() {
    emon.calcVI(20, 2000);
    voltage = emon.Vrms;
    current = emon.Irms;
    power = voltage * current;

    unsigned long currentMillis = millis();
    float elapsedHours = (currentMillis - lastMillis) / 3600000.0;
    kWh += (power * elapsedHours) / 1000.0;
    lastMillis = currentMillis;

    timeClient.update();
    int hour = timeClient.getHours();
    int currentDay = timeClient.getDay();

    // Slab calculation
    if (hour >= 6 && hour < 18) {
        cost = kWh * 5.00;
        currentSlab = "Normal";
    } else if (hour >= 18 && hour < 22) {
        cost = kWh * 7.00;
        currentSlab = "Peak";
    } else {
        cost = kWh * 3.50;
        currentSlab = "Off-Peak";
    }

    // Alerts
    if (power > 1000 && !powerAlertSent) {
        triggerBuzzer();
        sendEmailAlert("High Power Usage (Above 1000W)");
        powerAlertSent = true;
    } else if (power <= 1000) powerAlertSent = false;

    if (currentDay != lastDay) {
        kWh = 0;
        dailyEnergyAlertSent = false;
        lastDay = currentDay;
    }
    if (kWh > 7 && !dailyEnergyAlertSent) {
        triggerBuzzer();
        sendEmailAlert("Daily Energy Limit Exceeded");
        dailyEnergyAlertSent = true;
    }

    if (currentSlab != lastSentSlab) {
        sendEmailAlert("Slab Changed to " + currentSlab);
        lastSentSlab = currentSlab;
        if (currentSlab == "Peak") {
            triggerBuzzer();
            peakAlertSent = true;
        }
    }
    
    updateLEDs();
}

void handleData() {
    readSensors();
    char timeStr[10];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", 
           timeClient.getHours(), timeClient.getMinutes(), timeClient.getSeconds());

    String jsonData = "{";
    jsonData += "\"voltage\":" + String(voltage, 2) + ",";
    jsonData += "\"current\":" + String(current, 2) + ",";
    jsonData += "\"power\":" + String(power, 2) + ",";
    jsonData += "\"energy\":" + String(kWh, 3) + ",";
    jsonData += "\"cost\":" + String(cost, 2) + ",";
    jsonData += "\"slab\":\"" + currentSlab + "\",";
    jsonData += "\"time\":\"" + String(timeStr) + "\"}";

    server.send(200, "application/json", jsonData);
}

void setup() {
    Serial.begin(115200);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(RED_LED, OUTPUT);
    pinMode(YELLOW_LED, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, ap_password);
    WiFi.begin(wifi_ssid, wifi_password);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

    emon.voltage(VOLTAGE_PIN, vCalibration, 1.7);
    emon.current(CURRENT_PIN, currCalibration);

    timeClient.begin();
    while (!timeClient.update()) {
        timeClient.forceUpdate();
        delay(1000);
    }

    server.on("/", []() { server.send(200, "text/html", webpage); });
    server.on("/data", handleData);
    server.begin();
}

void loop() {
    server.handleClient();
    readSensors();

    if (digitalRead(BUTTON_PIN) == LOW) {
        delay(50); // Simple debounce
        if (digitalRead(BUTTON_PIN) == LOW) {
            Serial.println("Manual Report Triggered");
            sendEmailAlert("Manual Energy Report");
            triggerBuzzer();
            while(!digitalRead(BUTTON_PIN)); // Wait for release
        }
    }
    
    delay(100);
}