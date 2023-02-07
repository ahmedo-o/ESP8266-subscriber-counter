#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <YoutubeApi.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

struct Config {
	String ssid;
	String pass;
	String apiKey;
	String channelId;
	String text;
} config;

const char* CONFIG_FILE PROGMEM = "counter-config.json";
const char* HTML PROGMEM = R"(<!DOCTYPE html><head> <title>WiFi Configuration</title> <meta name="viewport" content="width=device-width, initial-scale=1"/> <style>*{font-family: Arial, Helvetica, sans-serif;}body, html{height: 90%;}#main{display: flex; align-items: center; justify-content: center; text-align: center; height: 100%;}input{margin: 1em;}input[type=text], input[type=password]{padding: .75em;}input[type=submit]{border: none; color: white; background-color: #0d6efd; border-radius: 7px; width: 90%; height: 30px;}</style></head><body> <div id="main"> <form method="post"> <div> <h2>WiFi Configuration</h2> <div> <input type="text" name="ssid" placeholder="SSID" value="%ssid%"> <br><input type="password" name="pass" placeholder="Password" id="password-input" value="%pass%"> <br><label>Show password</label> <input type="checkbox" id="show-password"> </div></div><div> <h2>YouTube Configuration</h2> <div> <input type="password" name="api_key" placeholder="YouTube API Key" id="key-input" value="%api_key%"> <br><label>Show key</label> <input type="checkbox" id="show-key"> <br><input type="text" name="channel_id" placeholder="Channel ID" value="%channel_id%"> <br><input type="text" name="text" placeholder="Optional Text" value="%text%"> </div></div><input type="submit" value="Save"> </form> </div><script>document.getElementById('show-password').addEventListener('change', (event)=>{document.getElementById('password-input').type=(event.target.checked ? 'text' : 'password');}); document.getElementById('show-key').addEventListener('change', (event)=>{document.getElementById('key-input').type=(event.target.checked ? 'text' : 'password');}); </script></body></html>)";

WiFiClientSecure client;
YoutubeApi api((char*)"", client);
LiquidCrystal_I2C lcd(0x27, 16, 2);
AsyncWebServer server(80);

unsigned int subCount;
unsigned long timeBetweenRequests = (60 * 1000) * 5;
unsigned long lastRequestMillis = millis();

void start_ap() {
	lcd.clear();
	WiFi.softAP("YouTube Sub Counter", "");
	delay(100);
	lcd.print("Open in browser:");
	lcd.setCursor(0, 1);
	lcd.print(WiFi.softAPIP());
}

void setup() {
	Serial.begin(115200);
	LittleFS.begin();
	
	lcd.init();
	lcd.backlight();

	if(LittleFS.exists(CONFIG_FILE)) {
		Serial.println("Config file exists");

		DynamicJsonDocument doc(1024);
		File configFile = LittleFS.open(CONFIG_FILE, "r");
		String configStr = configFile.readString();

		Serial.printf("Config string: %s\n", configStr.c_str());
		
		deserializeJson(doc, configStr);
		
		config.ssid = doc["ssid"].as<String>();
		config.pass = doc["pass"].as<String>();
		config.apiKey = doc["api_key"].as<String>();
		config.channelId = doc["channel_id"].as<String>();
		config.text = doc["text"].as<String>();
	}

	if(!config.ssid.isEmpty()) {
		WiFi.mode(WIFI_STA);
		WiFi.disconnect();
		delay(100);

		unsigned long connectionStartMillis = millis();

		lcd.print("Connecting...");
		WiFi.begin(config.ssid, config.pass);
		while (WiFi.status() != WL_CONNECTED) {
			if((millis() - connectionStartMillis) >= 30000) {
				start_ap();
				break;
			}
			delay(500);
		}

		if(WiFi.status() == WL_CONNECTED) {
			lcd.clear();
			lcd.print("Connected!");
			delay(2000);
			lcd.clear();
			lcd.print("For config:");
			lcd.setCursor(0, 1);
			lcd.print(WiFi.localIP());
			delay(2000);
		}
	} else {
		start_ap();
	}

	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
		String payloadHtml = HTML;
		payloadHtml.replace("%ssid%", config.ssid);
		payloadHtml.replace("%pass%", config.pass);
		payloadHtml.replace("%api_key%", config.apiKey);
		payloadHtml.replace("%channel_id%", config.channelId);
		payloadHtml.replace("%text%", config.text);

		request->send(200, "text/html", payloadHtml);
	});

	server.on("/", HTTP_POST, [](AsyncWebServerRequest *request){
		if(request->hasParam("ssid", true)) {
			config.ssid = request->getParam("ssid", true)->value();
		}

		if(request->hasParam("pass", true)) {
			config.pass = request->getParam("pass", true)->value();
		}

		if(request->hasParam("api_key", true)) {
			config.apiKey = request->getParam("api_key", true)->value();
		}

		if(request->hasParam("channel_id", true)) {
			config.channelId = request->getParam("channel_id", true)->value();
		}

		if(request->hasParam("text", true)) {
			config.text = request->getParam("text", true)->value();
		}

		Serial.printf("Got config: \nssid: %s; pass: %s; api_key: %s; channel_id: %s; text: %s;\n", 
		config.ssid.c_str(), config.pass.c_str(), config.apiKey.c_str(), config.channelId.c_str(), config.text.c_str());

		request->send(200, "text/html", "OK, restarting in 5 seconds...");

		DynamicJsonDocument doc(1024);
		doc["ssid"] = config.ssid;
		doc["pass"] = config.pass;
		doc["api_key"] = config.apiKey;
		doc["channel_id"] = config.channelId;
		doc["text"] = config.text;
		
		String configStr;
		serializeJson(doc, configStr);
		
		Serial.printf("Constructed config string: %s\n", configStr.c_str());

		File configFile = LittleFS.open(CONFIG_FILE, "w");
		configFile.write(configStr.c_str());
		configFile.close();
		LittleFS.end();
		
		delay(5000);

		ESP.reset();
	});
	server.begin();
	client.setInsecure();

	api._apiKey = (char*)config.apiKey.c_str();
}

bool hadInitialRequest = false;
void loop() {
	if((!hadInitialRequest || (millis() - lastRequestMillis) >= timeBetweenRequests) && 
			WiFi.status() == WL_CONNECTED && 
			!config.apiKey.isEmpty() && 
			!config.channelId.isEmpty() && 
			api.getChannelStatistics(config.channelId)) {
		
		Serial.println("Fetched count");
		lcd.clear();

		if(config.text.length() <= 16) {
			int padding  = (int)floorf((16 - config.text.length()) / 2);
			lcd.setCursor(padding, 0);
			lcd.print(config.text);
		}

		String subCount = String(api.channelStats.subscriberCount);
		int padding = (int)floorf((16 - subCount.length()) / 2);
		lcd.setCursor(padding, 1);
		lcd.print(api.channelStats.subscriberCount);

		lastRequestMillis = millis();
		hadInitialRequest = true;
		
		Serial.println("Updated count");
	}

	if(config.text.length() > 16) {
		String dispString;
		for (int i = 0; i < 16; i++) {
			dispString = "";
			for(int j = i; j < 16; j++) {
				dispString += ' ';
			}
			dispString += config.text.substring(0, i);
			lcd.setCursor(0, 0);
			lcd.print(dispString);
			delay(350);
 		}	
		delay(1000);
		for(int i = 0; i < (int)config.text.length(); i++) {
			dispString = config.text.substring(i, i + 16);
			if(dispString.length() != 16) {
				for(int j = 0; j < (int)(16 - dispString.length()); j++) {
					dispString += ' ';
				}
			}				
			lcd.setCursor(0, 0);
			lcd.print(dispString);
			delay(350);
		}
		delay(1000);
	}
}