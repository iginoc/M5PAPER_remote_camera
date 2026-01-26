#include <M5EPD.h>
#include <M5EPD_Driver.h>
#include <WiFi.h>
#include <vector> // Aggiunto per usare std::vector
#include <HTTPClient.h>
#include <PubSubClient.h> // Aggiunta la libreria MQTT
#include <Preferences.h> // Per salvare lo stato prima del deep sleep
#include <SPIFFS.h>
#include <time.h>
#include <math.h>
#include <WebServer.h>

// Inclusione dei font necessari
// #include <Fonts/GFXFF/FreeSansBold12pt7b.h>
// #include <Fonts/GFXFF/FreeSans18pt7b.h>
// #include <Fonts/GFXFF/FreeSansBold18pt7b.h>
// #include <Fonts/GFXFF/FreeSansBold24pt7b.h>
// #include <Fonts/GFXFF/FreeSans12pt7b.h>

// --- VARIABILI GLOBALI ---
String homeAssistantAddress = ""; 
String homeAssistantToken = "";     
String ssid = ""; 
String password = ""; 

// --- IMPOSTAZIONI MQTT ---
String mqtt_server = ""; 
int mqtt_port = 1883;
String mqtt_user = ""; 
String mqtt_password = ""; 

Preferences preferences;
int sensorPage = 0;
const int SENSORS_PER_PAGE = 11; // Numero di sensori per pagina (12 - 1 per Next)
unsigned long lastActivityTime = 0;
const unsigned long SLEEP_TIMEOUT = 600000; // 10 minuti di attività prima di dormire
const unsigned long UPDATE_INTERVAL = 10000; // Controlla HA ogni 10 secondi mentre è sveglio
const uint64_t DEEP_SLEEP_TIME = 10 * 60 * 1000000; // 10 minuti di sonno (in microsecondi)

struct DeviceButton {
  const char* entity_id;
  const char* name;
  String state;
  int x;
  int y;
  int w;
  int h;
  const char* type; // "light" or "switch"
};

const int NUM_DEVICES = 4;
DeviceButton devices[NUM_DEVICES] = {
    {"switch.gruppo_switch", "SWITCH", "off", 0, 0, 0, 0, "group"},
    {"light.gruppo_luci", "LUCI", "off", 0, 0, 0, 0, "group"},
    {"", "HOME", "off", 0, 0, 0, 0, ""},
    {"", "SENSORI", "off", 0, 0, 0, 0, ""},
};

// --- NUOVA GRIGLIA 3x3 ---
struct GridButton {
  String entity_id;
  String name;
  String state;
  int x;
  int y;
  int w;
  int h;
  String type; // "light", "switch", etc.
};

const int NUM_GRID_BUTTONS = 12; // 3x4 grid
GridButton defaultGridButtons[NUM_GRID_BUTTONS] = {
    {"", "", "", 0, 0, 0, 0, ""}, {"", "", "", 0, 0, 0, 0, ""}, {"", "", "", 0, 0, 0, 0, ""},
    {"", "", "", 0, 0, 0, 0, ""}, {"", "", "", 0, 0, 0, 0, ""}, {"", "", "", 0, 0, 0, 0, ""},
    {"", "", "", 0, 0, 0, 0, ""}, {"", "", "", 0, 0, 0, 0, ""}, {"", "", "", 0, 0, 0, 0, ""},
    {"", "", "", 0, 0, 0, 0, ""}, {"", "", "", 0, 0, 0, 0, ""}, {"", "", "", 0, 0, 0, 0, ""},
};

GridButton gridButtons[NUM_GRID_BUTTONS];

// --- NUOVA SEZIONE SENSORI ---
// Aggiungi qui i tuoi sensori che contengono "level" (o "livello") nel nome
struct SensorDisplay {
  String entity_id; // Modificato in String per gestire correttamente la memoria
  String name;      // Modificato in String per gestire correttamente la memoria
  String attribute; // NUOVO: per specificare un attributo invece dello stato
  String state;
};

std::vector<SensorDisplay> sensors; // Sostituito l'array statico con un vettore dinamico

// --- COSTANTI PER LA PARSERSA ---
// Aggiungiamo dei delimitatori univoci per distinguere stati e attributi nella risposta
const char* STATE_DELIMITER = ";";
const char* ATTR_DELIMITER = "|";


// --- STATO GRAFICO ---
String currentGraphEntityId = "";
String currentGraphName = "";
int currentGraphDuration = 24;

const int CLOCK_PAGE = 6;         // Pagina orologio analogico
const int CALENDAR_PAGE = 7;      // Pagina calendario
const int SCRIPT_PAGE = 8;        // Pagina script
const int MEDIA_CONTROL_PAGE = 9; // Pagina controllo media
// --- STATO CONTROLLO LUCE ---
const int LIGHT_CONTROL_PAGE = 5;
String selectedLightEntity = "";
String selectedLightName = "";
int selectedLightBrightness = 0;
String selectedLightState = "off";

// --- STATO CONTROLLO MEDIA ---
String selectedMediaEntity = "";
String selectedMediaName = "";
String selectedMediaState = "off";
float selectedMediaVolume = 0.0; // Volume è un float da 0.0 a 1.0

// --- STATO CALENDARIO ---
int calendarMonthOffset = 0;

// --- STATO UI ---
bool isDarkMode = false;
int headerBatteryX = 0;

int currentPage = 0; // 0 = SENSORI, 1 = HOME
const int GRAPH_PAGE = 4; // Pagina grafico
M5EPD_Canvas canvas(&M5.EPD);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer server(80); // Server Web sulla porta 80

void drawFullUI(bool syncPage = true); // Dichiarazione forward della funzione
void drawGridButtons();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void loadGroupLights();
void loadGroupSwitches();
void loadGroupSensors();
void loadGroupScripts();
void reconnectMqtt();
void drawHeader(M5EPD_Canvas* c = &canvas);
void updateTimeAndDateStates(); 
void toggleWiFi(); 
void showBusyIndicator(); 
void hideBusyIndicator(); 
void fetchLightDetails(String entity_id);
void setLightBrightness(String entity_id, int brightness);
void drawLightControlPage();
void drawMediaControlPage();
void fetchMediaDetails(String entity_id);
void setMediaVolume(String entity_id, float volume);
void controlMediaPlayer(String entity_id, String action);
void drawHotspotControl(); // Sostituisce drawCalendar
void drawAnalogClock();
void handleScreenshot();
void handleWifiConfig();
void handleSaveWifi();
void handleFactoryReset();
void handleSetPage();
String encryptConfig(String input);
String decryptConfig(String input);

// Funzione helper per trovare lo stato di un sensore tramite il suo entity_id
String getSensorState(String entity_id) {
    for (const auto& sensor : sensors) {
        if (sensor.entity_id == entity_id) {
            return sensor.state;
        }
    }
    return ""; // Ritorna una stringa vuota se non trovato
}

bool testHomeAssistantConnection() {
  WiFiClient client;
  HTTPClient http;

  // Aggiungiamo /api/ per testare un endpoint valido
  String apiUrl = homeAssistantAddress + "/api/";
  Serial.println("Connecting to: " + apiUrl);

  http.begin(client, apiUrl);
  http.addHeader("Authorization", "Bearer " + homeAssistantToken);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    String payload = http.getString();
    Serial.println(payload);
    http.end();
    // Un codice 200 (OK) o simile indica successo
    return httpResponseCode == HTTP_CODE_OK;
  } else {
    Serial.printf("Error code: %s\n", http.errorToString(httpResponseCode).c_str());
    http.end();
    return false;
  }
}

void toggleDevice(String entity_id, String type) {
  if (homeAssistantAddress == "") return;
  if (WiFi.status() != WL_CONNECTED) return; // Controllo connessione
  WiFiClient client;
  HTTPClient http;
  String service_action = "toggle";
  if (type == "script" || type == "scene") {
      service_action = "turn_on";
  }
  String apiUrl = homeAssistantAddress + "/api/services/" + String(type) + "/" + service_action;
  http.begin(client, apiUrl);
  http.addHeader("Authorization", "Bearer " + homeAssistantToken);
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = "{\"entity_id\":\"" + entity_id + "\"}";

  int httpResponseCode = http.POST(jsonPayload);
  if (httpResponseCode > 0) {
    Serial.printf("[HTTP] POST... code: %d\n", httpResponseCode);
        String payload = http.getString();
        Serial.println("Payload "+payload);
  } else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  http.end();
  hideBusyIndicator();
}

void setPageInputNumber(int pageValue) {
  if (homeAssistantAddress == "") return;
  if (WiFi.status() != WL_CONNECTED) return; // Controllo connessione
  showBusyIndicator();
  WiFiClient client;
  HTTPClient http;
  String apiUrl = homeAssistantAddress + "/api/services/input_number/set_value";
  http.begin(client, apiUrl);
  http.addHeader("Authorization", "Bearer " + homeAssistantToken);
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = "{\"entity_id\":\"input_number.epaper_pagina\",\"value\":" + String(pageValue) + "}";

  int httpResponseCode = http.POST(jsonPayload);
  if (httpResponseCode > 0) {
    Serial.printf("[HTTP] setPageInputNumber... code: %d\n", httpResponseCode);
  } else {
    Serial.printf("[HTTP] setPageInputNumber... failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
  hideBusyIndicator();
}

void toggleWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Turning WiFi OFF...");
    mqttClient.disconnect();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi is OFF.");
    drawFullUI(); 
  } else {
    Serial.println("Turning WiFi ON...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    // CORREZIONE: Attendi che la connessione sia stabilita prima di ridisegnare.
    // Altrimenti, il ridisegno avviene quando WiFi.status() non è ancora WL_CONNECTED.
    Serial.print("Waiting for WiFi connection...");
    int retries = 20; // Attendi al massimo 10 secondi
    while (WiFi.status() != WL_CONNECTED && retries > 0) {
      delay(500);
      Serial.print(".");
      retries--;
    }
    Serial.println();
    drawFullUI(); // Ridisegna l'interfaccia dopo aver ottenuto la connessione (o dopo il timeout)
  }
}

// --- NUOVA FUNZIONE SUPER-OTTIMIZZATA PER AGGIORNARE GLI STATI ---
bool updateStates(bool update_devices = true, bool update_sensors = true, bool syncPage = true) {
  if (homeAssistantAddress == "") return false;
  showBusyIndicator();
  WiFiClient client;
  HTTPClient http;

  String apiUrl = homeAssistantAddress + "/api/template";
  http.begin(client, apiUrl);
  http.addHeader("Authorization", "Bearer " + homeAssistantToken);
  http.addHeader("Content-Type", "application/json");
  bool pageChanged = false;

  Serial.println("Updating states...");

  // Costruiamo dinamicamente la richiesta JSON per il template
  String jsonPayload = "{\"template\":\"{% set entities = [";
  if (update_devices) for (int i = 0; i < NUM_DEVICES; i++) {
    if (String(devices[i].entity_id) != "") {
      jsonPayload += "'" + String(devices[i].entity_id) + "',";
    }
  }
  if (update_devices) for (int i = 0; i < NUM_GRID_BUTTONS; i++) {
    if (gridButtons[i].entity_id != "") {
        jsonPayload += "'" + gridButtons[i].entity_id + "',";
    }
  }
  for (size_t i = 0; i < sensors.size(); i++) {
    // CORREZIONE: Escludi i sensori di tempo e data dalla richiesta di stato,
    // poiché vengono gestiti in modo speciale e non devono essere richiesti qui.
    if (sensors[i].attribute == "" && sensors[i].entity_id != "sensor.time" && sensors[i].entity_id != "sensor.oggi") {
      // Richiesta di stato normale
      jsonPayload += "'" + sensors[i].entity_id + "',";
    }
  }
  if (jsonPayload.endsWith(",")) {
    jsonPayload.remove(jsonPayload.length() - 1); // Rimuovi l'ultima virgola solo se sono state aggiunte entità
  }
  jsonPayload += "] %}{% for entity in entities %}{{ states(entity) }}{% if not loop.last %}" + String(STATE_DELIMITER) + "{% endif %}{% endfor %}";

  // Aggiungi le richieste per gli attributi
  for (size_t i = 0; i < sensors.size(); i++) {
    if (sensors[i].attribute != "") {
      // SEMPRE prefisso con delimitatore per separare dagli stati o dall'attributo precedente
      jsonPayload += String(ATTR_DELIMITER) + "{{ state_attr('" + sensors[i].entity_id + "', '" + sensors[i].attribute + "') }}";
    }
  }
  jsonPayload += "\"}";
  int httpResponseCode = http.POST(jsonPayload);
  if (httpResponseCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    int lastIndex = 0;
    int currentIndex = 0;
    if (update_devices) {
      // Aggiorna i pulsanti
      for (int i = 0; i < NUM_DEVICES; i++) {
        if (String(devices[i].entity_id) != "") {
            int commaIndex = payload.indexOf(STATE_DELIMITER, lastIndex);
            int pipeIndex = payload.indexOf(ATTR_DELIMITER, lastIndex);
            if (commaIndex != -1 && (pipeIndex == -1 || commaIndex < pipeIndex)) currentIndex = commaIndex;
            else if (pipeIndex != -1) currentIndex = pipeIndex;
            else currentIndex = payload.length();

            devices[i].state = payload.substring(lastIndex, currentIndex);
            if (currentIndex == commaIndex) lastIndex = currentIndex + 1; else lastIndex = currentIndex;
        }
      }
      // Aggiorna i pulsanti della griglia
      for (int i = 0; i < NUM_GRID_BUTTONS; i++) {
        if (gridButtons[i].entity_id != "") {
            int commaIndex = payload.indexOf(STATE_DELIMITER, lastIndex);
            int pipeIndex = payload.indexOf(ATTR_DELIMITER, lastIndex);
            if (commaIndex != -1 && (pipeIndex == -1 || commaIndex < pipeIndex)) currentIndex = commaIndex;
            else if (pipeIndex != -1) currentIndex = pipeIndex;
            else currentIndex = payload.length();

            gridButtons[i].state = payload.substring(lastIndex, currentIndex);
            if (currentIndex == commaIndex) lastIndex = currentIndex + 1; else lastIndex = currentIndex;
        }
      }
    }
    if (update_sensors) {
      // Passaggio 1: Aggiorna i sensori che sono STATI
      for (size_t i = 0; i < sensors.size(); i++) {
        if (sensors[i].attribute == "" && sensors[i].entity_id != "sensor.time" && sensors[i].entity_id != "sensor.oggi") {
          int commaIndex = payload.indexOf(STATE_DELIMITER, lastIndex);
          int pipeIndex = payload.indexOf(ATTR_DELIMITER, lastIndex);
          
          if (commaIndex != -1 && (pipeIndex == -1 || commaIndex < pipeIndex)) {
              currentIndex = commaIndex;
              sensors[i].state = payload.substring(lastIndex, currentIndex);
              lastIndex = currentIndex + 1; // Salta il ;
          } else if (pipeIndex != -1) {
              currentIndex = pipeIndex;
              sensors[i].state = payload.substring(lastIndex, currentIndex);
              lastIndex = currentIndex; // Fermati al |
          } else {
              currentIndex = payload.length();
              sensors[i].state = payload.substring(lastIndex, currentIndex);
              lastIndex = currentIndex;
          }
          sensors[i].state.trim(); // Rimuove eventuali spazi bianchi
        }
      }

      // Passaggio 2: Aggiorna i sensori che sono ATTRIBUTI
      for (size_t i = 0; i < sensors.size(); i++) {
        if (sensors[i].attribute != "") {
          // Cerca il prossimo delimitatore |
          if (lastIndex < payload.length() && payload.substring(lastIndex, lastIndex + 1) == ATTR_DELIMITER) {
              lastIndex++; // Salta il |
              int pipeIndex = payload.indexOf(ATTR_DELIMITER, lastIndex);
              if (pipeIndex == -1) currentIndex = payload.length();
              else currentIndex = pipeIndex;
              
              sensors[i].state = payload.substring(lastIndex, currentIndex);
              sensors[i].state.trim();
              lastIndex = currentIndex;
          }
        }
      }
    }
    Serial.println("States updated successfully.");
  } else {
    Serial.printf("[HTTP] State update failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();

  if (update_sensors && syncPage) {
    String pageStr = getSensorState("input_number.epaper_pagina");
    Serial.println("Parsed page: " + pageStr);
    if (pageStr != "") {
      int pageVal = pageStr.toInt();
      int newPage = -1;
      if (pageVal == 1) newPage = 3; // SWITCH
      else if (pageVal == 2) newPage = 2; // LUCI
      else if (pageVal == 3) newPage = 1; // HOME
      else if (pageVal == 4) newPage = 0; // SENSORI

      if (newPage != -1 && newPage != currentPage) {
        // Se siamo in una pagina di controllo (luce, media) e HA ci rimanda alla pagina "genitore", ignoriamo
        if ((currentPage == LIGHT_CONTROL_PAGE && newPage == 2) || (currentPage == MEDIA_CONTROL_PAGE && newPage == 1)) {
            // Ignora il cambio pagina
        } else {
        Serial.printf("Changing page from %d to %d\n", currentPage, newPage);
        currentPage = newPage;
        pageChanged = true;
        if (currentPage == 3) {
            loadGroupSwitches(); // Ora recupera anche lo stato, non serve updateStates
        } else if (currentPage == 2) {
            loadGroupLights(); // Ora recupera anche lo stato, non serve updateStates
        } else if (currentPage == 1) {
           // Pulisci e configura solo i pulsanti per la pagina HOME
           for (int k = 0; k < NUM_GRID_BUTTONS; k++) {
               gridButtons[k].entity_id = "";
               gridButtons[k].name = "";
               gridButtons[k].type = "";
               gridButtons[k].state = "off";
           }
           gridButtons[0].entity_id = "";
           gridButtons[0].name = "Hotspot";
           gridButtons[0].type = "hotspot";
           wifi_mode_t mode = WiFi.getMode();
           gridButtons[0].state = ((mode == WIFI_AP) || (mode == WIFI_AP_STA)) ? "on" : "off";
           gridButtons[1].entity_id = "";
           gridButtons[1].name = "Script";
           gridButtons[1].type = "script_page";
           gridButtons[1].state = "off";
        } else if (currentPage == 0) {
           loadGroupSensors();
        } else if (currentPage == SCRIPT_PAGE) {
           loadGroupScripts();
        }
        }
      }
    }
  }
  hideBusyIndicator();
  return pageChanged;
}

void drawButtons() {
    // Imposta un font "bello" (FreeFont) per i pulsanti
    canvas.setFreeFont(&FreeSansBold12pt7b);
    for (int i = 0; i < NUM_DEVICES; i++) {
        bool isActive = false;
        if (i == 0) { // SWITCH
            if (currentPage == 3) isActive = true;
        } else if (i == 1) { // LUCI
            if (currentPage == 2 || currentPage == LIGHT_CONTROL_PAGE) isActive = true;
        } else if (i == 2) { // HOME
            if (currentPage == 1) isActive = true;
        } else if (i == 3) { // SENSORI
            if (currentPage == 0) isActive = true;
        }

        // Con SetColorReverse(true), il nero diventa bianco e il bianco diventa nero
        if (isActive) {
            // Pulsante "acceso": Sfondo bianco (disegnato come BLACK), testo nero (disegnato come WHITE)
            canvas.fillRect(devices[i].x, devices[i].y, devices[i].w, devices[i].h, BLACK);
            canvas.setTextColor(WHITE);
        } else {
            // Pulsante "spento": Sfondo nero (disegnato come WHITE), testo bianco (disegnato come BLACK)
            canvas.fillRect(devices[i].x, devices[i].y, devices[i].w, devices[i].h, WHITE);
            canvas.setTextColor(BLACK);
        }
        
        canvas.setTextDatum(MC_DATUM); // Imposta il punto di riferimento del testo al centro
        canvas.drawString(devices[i].name, devices[i].x + devices[i].w / 2, devices[i].y + devices[i].h / 2);
    }

    // Disegna una cornice spessa 1 pixel attorno a ogni pulsante
    for (int i = 0; i < NUM_DEVICES; i++) {
        canvas.drawRect(devices[i].x, devices[i].y, devices[i].w, devices[i].h, BLACK);
    }
}

void drawBatteryIcon(int x, int y, int percentage, M5EPD_Canvas* c = &canvas, uint32_t bgColor = WHITE, uint32_t fgColor = BLACK) {
    // Dimensioni dell'icona
    const int w = 60; // larghezza corpo batteria
    const int h = 28; // altezza corpo batteria
    const int term_w = 6; // larghezza terminale
    const int term_h = 12; // altezza terminale
    const int border = 2; // spessore bordo

    // Disegna il bordo esterno della batteria
    c->fillRect(x, y, w + term_w, h, bgColor); // Pulisci l'area prima di disegnare
    c->drawRect(x, y, w, h, fgColor);

    // Disegna il terminale
    c->fillRect(x + w, y + (h - term_h) / 2, term_w, term_h, fgColor);

    // Calcola e disegna il livello di carica interno
    int charge_w = (w - 2 * border) * percentage / 100;
    if (charge_w > 0) {
        c->fillRect(x + border, y + border, charge_w, h - 2 * border, fgColor);
    }
}

void drawTemperatureIcon(int x, int y) {
    // Icona semplice di un termometro
    const int bulb_r = 10;
    const int body_w = 10;
    const int body_h = 20;

    // Corpo del termometro
    canvas.fillRect(x + bulb_r - (body_w / 2), y - body_h, body_w, body_h, BLACK);
    // Bulbo del termometro
    canvas.fillCircle(x + bulb_r, y, bulb_r, BLACK);
    // Linea interna (simula il mercurio)
    canvas.fillRect(x + bulb_r - 1, y - body_h + 2, 3, body_h, WHITE);
}

void drawPowerIcon(int x, int y) {
    // Icona semplice di un fulmine per la potenza
    canvas.fillTriangle(x, y - 15, x - 10, y + 5, x + 5, y + 5, BLACK);
    canvas.fillTriangle(x + 5, y, x - 5, y + 20, x + 10, y, BLACK);
}

void drawCarIcon(int x, int y) {
    // x, y è il centro dell'area dell'icona
    // Carrozzeria
    canvas.fillRect(x - 30, y - 5, 60, 15, BLACK);
    // Tetto
    canvas.fillRect(x - 15, y - 15, 30, 10, BLACK);
    // Ruote
    canvas.fillCircle(x - 15, y + 15, 8, BLACK);
    canvas.fillCircle(x + 15, y + 15, 8, BLACK);
}

int getInternalBatteryPercentage() {
    // La tensione della batteria del M5Paper va da circa 3.2V (0%) a 4.2V (100%)
    float voltage = M5.getBatteryVoltage() / 1000.0; // La funzione ritorna mV
    if (voltage < 3.2) voltage = 3.2;
    if (voltage > 4.2) voltage = 4.2;
    return (int)((voltage - 3.2) / (4.2 - 3.2) * 100.0);
}

void drawSensors() {
    // Imposta un font "bello" (FreeFont) per i sensori
    canvas.setFreeFont(&FreeSansBold12pt7b); 
    canvas.setTextColor(BLACK); // Testo bianco su sfondo nero (per via dell'inversione)
 
    // Calcoliamo una posizione di partenza fissa sotto i pulsanti dell'header
    const int header_height = 80;
    const int start_y = header_height + 20;
    const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
    const int col2_width = M5EPD_PANEL_W - col2_start_x;
    const int cols = 3;
    const int rows = 4;
    const int btn_w = col2_width / cols;
    const int btn_h = (M5EPD_PANEL_H - start_y) / rows;

    // Pulisci l'intera area destra
    canvas.fillRect(col2_start_x, header_height, col2_width, M5EPD_PANEL_H - header_height, WHITE);
    canvas.drawRect(col2_start_x, header_height, col2_width, M5EPD_PANEL_H - header_height, WHITE); // Clean borders

    // Filtra i sensori visualizzabili
    std::vector<int> displayableIndices;
    for (size_t i = 0; i < sensors.size(); i++) {
        if (sensors[i].entity_id != "sensor.time" && sensors[i].entity_id != "sensor.oggi" && sensors[i].entity_id != "input_number.epaper_pagina") {
            displayableIndices.push_back(i);
        }
    }

    if (displayableIndices.empty()) {
        canvas.setTextDatum(TL_DATUM);
        canvas.drawString("Nessun sensore trovato.", col2_start_x + 20, header_height + 50);
        return;
    }

    // Calcola paginazione
    int totalPages = (displayableIndices.size() + SENSORS_PER_PAGE - 1) / SENSORS_PER_PAGE;
    if (sensorPage >= totalPages) sensorPage = totalPages - 1;
    if (sensorPage < 0) sensorPage = 0;

    int startIdx = sensorPage * SENSORS_PER_PAGE;
    int endIdx = min((int)displayableIndices.size(), startIdx + SENSORS_PER_PAGE);

    for (int i = 0; i < SENSORS_PER_PAGE; i++) {
        int sensorIdx = startIdx + i;
        int row = i / cols;
        int col = i % cols;
        int x = col2_start_x + col * btn_w;
        int y = start_y + row * btn_h;

        // Disegna sfondo e bordo cella
        canvas.fillRect(x, y, btn_w, btn_h, WHITE);
        canvas.drawRect(x, y, btn_w, btn_h, BLACK);
        
        if (sensorIdx < displayableIndices.size()) {
            int idx = displayableIndices[sensorIdx];
            
            // Determina unità e icona
            String unit = "";
            bool hasIcon = false;
            
            // Rimuovi decimali dal valore
            String valStr = sensors[idx].state;
            int dotIndex = valStr.indexOf('.');
            if (dotIndex != -1) {
                valStr = valStr.substring(0, dotIndex);
            }

            int iconX = x + 20;
            int iconY = y + btn_h/2 - 10; // Alza l'icona di 10px
            
            if (sensors[idx].entity_id.indexOf("battery") != -1 || sensors[idx].entity_id.indexOf("livello") != -1) {
                unit = "%";
                int percentage = valStr.toInt();
                drawBatteryIcon(iconX, iconY - 14, percentage, &canvas, WHITE, BLACK);
                hasIcon = true;
            } else if (sensors[idx].entity_id.indexOf("temp") != -1 || sensors[idx].entity_id.indexOf("temperatura") != -1) {
                unit = " °C";
                drawTemperatureIcon(iconX + 15, iconY + 5);
                hasIcon = true;
            } else if (sensors[idx].entity_id.indexOf("potenza") != -1) {
                unit = " W";
                drawPowerIcon(iconX + 15, iconY);
                hasIcon = true;
            }

            // Disegna Valore (Grande) - Allineato a destra
            canvas.setFreeFont(&FreeSansBold18pt7b);
            canvas.setTextDatum(MR_DATUM);
            int valY = y + btn_h/2 - 25; // Alza il valore di 10px
            int valX = x + btn_w - 20;
            
            canvas.drawString(valStr + unit, valX, valY);

            // Disegna Nome (Piccolo)
            canvas.setFreeFont(&FreeSans12pt7b);
            canvas.setTextDatum(ML_DATUM);
            int nameX = x + 10;
            int nameY = y + btn_h/2 + 15; // Alza il nome di 10px
            int maxNameWidth = btn_w - 20;
            String name = sensors[idx].name;
            if (canvas.textWidth(name) > maxNameWidth) {
                String ellipsis = "...";
                int ellipsisWidth = canvas.textWidth(ellipsis);
                while (name.length() > 0 && canvas.textWidth(name) + ellipsisWidth > maxNameWidth) {
                    name.remove(name.length() - 1);
                }
                name += ellipsis;
            }
            canvas.drawString(name, nameX, nameY);
        }
    }

    // Disegna pulsante NEXT nell'ultima casella (indice 11)
    int next_x = col2_start_x + 2 * btn_w;
    int next_y = start_y + 3 * btn_h;
    
    canvas.fillRect(next_x, next_y, btn_w, btn_h, WHITE);
    canvas.drawRect(next_x, next_y, btn_w, btn_h, BLACK);
    
    if (totalPages > 1) {
        canvas.setFreeFont(&FreeSansBold12pt7b);
        canvas.setTextDatum(MC_DATUM);
        canvas.drawString("NEXT >", next_x + btn_w/2, next_y + btn_h/2 - 10);
        canvas.setFreeFont(&FreeSans9pt7b);
        canvas.drawString(String(sensorPage + 1) + "/" + String(totalPages), next_x + btn_w/2, next_y + btn_h/2 + 20);
    }
}

void drawGridButtons() {
    // Calcola la posizione Y di partenza basandosi sulla posizione e sul numero di sensori
    const int header_height = 80;
    int start_y = header_height + 20; // Inizia a disegnare la griglia sotto l'header

    // SPOSTA LA GRIGLIA NELLA SECONDA COLONNA (70%)
    const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
    const int col2_width = M5EPD_PANEL_W - col2_start_x;
    const int cols = 3;
    const int rows = 4;

    // Rimuovi i margini e calcola la dimensione dei pulsanti per riempire lo spazio
    const int margin_x = 0;
    const int margin_y = 0;
    const int btn_w = col2_width / cols;
    const int btn_h = (M5EPD_PANEL_H - start_y) / rows;
    
    // Pulisci l'area della griglia prima di disegnarla per evitare sovrapposizioni
    canvas.fillRect(col2_start_x, header_height, col2_width, M5EPD_PANEL_H - header_height, WHITE);

    canvas.setFreeFont(&FreeSansBold12pt7b);

    for (int i = 0; i < NUM_GRID_BUTTONS; i++) {
        if (gridButtons[i].name == "" && gridButtons[i].entity_id == "") {
            continue; // Salta i pulsanti non configurati
        }

        int row = i / cols;
        int col = i % cols;
        gridButtons[i].w = btn_w;
        gridButtons[i].h = btn_h;
        gridButtons[i].x = col2_start_x + margin_x + col * (btn_w + margin_x); // Posizione X nella seconda colonna
        gridButtons[i].y = start_y + row * (btn_h + margin_y); // Posizione Y del pulsante

        // CORREZIONE: Pulisci sempre lo sfondo del riquadro prima di disegnare per evitare ghosting.
        canvas.fillRect(gridButtons[i].x, gridButtons[i].y, gridButtons[i].w, gridButtons[i].h, WHITE);

        canvas.fillRect(gridButtons[i].x, gridButtons[i].y, gridButtons[i].w, gridButtons[i].h, (gridButtons[i].state == "on" || gridButtons[i].state == "active") ? BLACK : WHITE);
        canvas.setTextColor((gridButtons[i].state == "on" || gridButtons[i].state == "active") ? WHITE : BLACK);
        canvas.drawRect(gridButtons[i].x, gridButtons[i].y, gridButtons[i].w, gridButtons[i].h, BLACK);
        canvas.setTextDatum(MC_DATUM);

        if (gridButtons[i].type == "hotspot") {
            // Sposta il nome un po' in su per fare spazio all'IP
            canvas.drawString(gridButtons[i].name, gridButtons[i].x + gridButtons[i].w / 2, gridButtons[i].y + gridButtons[i].h / 2 - 15);
            
            // Aggiungi l'etichetta IP sotto
            canvas.setFreeFont(&FreeSans9pt7b); // Usa un font più piccolo
            canvas.setTextDatum(TC_DATUM);
            String ipText = "";
            wifi_mode_t mode = WiFi.getMode();
            bool isApOn = (mode == WIFI_AP) || (mode == WIFI_AP_STA);
            if (isApOn) {
                ipText = WiFi.softAPIP().toString();
            } else if (WiFi.status() == WL_CONNECTED) {
                ipText = WiFi.localIP().toString();
            }
            if (ipText != "") {
                canvas.drawString(ipText, gridButtons[i].x + gridButtons[i].w / 2, gridButtons[i].y + gridButtons[i].h / 2 + 10);
            }
            canvas.setFreeFont(&FreeSansBold12pt7b); // Ripristina il font principale per il prossimo ciclo
        } else {
            canvas.drawString(gridButtons[i].name, gridButtons[i].x + gridButtons[i].w / 2, gridButtons[i].y + gridButtons[i].h / 2);
        }
    }
}

void loadGroupLights() {
  if (homeAssistantAddress == "") return;
  showBusyIndicator();
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  WiFiClient client;
  String apiUrl = homeAssistantAddress + "/api/template";
  
  // Template per ottenere entity_id, friendly_name e state separati da | e coppie separate da ;
  String templateBody = "{% set entities = state_attr('light.gruppo_luci', 'entity_id') %}{% if entities %}{% for entity in entities %}{{ entity }}|{{ state_attr(entity, 'friendly_name') }}|{{ states(entity) }}{% if not loop.last %};{% endif %}{% endfor %}{% endif %}";
  String jsonPayload = "{\"template\":\"" + templateBody + "\"}";

  http.begin(client, apiUrl);
  http.addHeader("Authorization", "Bearer " + homeAssistantToken);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(jsonPayload);
  if (httpResponseCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // Pulisci i pulsanti
    for (int i = 0; i < NUM_GRID_BUTTONS; i++) {
        gridButtons[i].entity_id = "";
        gridButtons[i].name = "";
        gridButtons[i].type = "";
        gridButtons[i].state = "off";
    }

    int startIndex = 0;
    int btnIndex = 0;
    while (startIndex < payload.length() && btnIndex < NUM_GRID_BUTTONS) {
        int endIndex = payload.indexOf(';', startIndex);
        if (endIndex == -1) endIndex = payload.length();
        
        String triplet = payload.substring(startIndex, endIndex);
        int firstPipe = triplet.indexOf('|');
        int secondPipe = triplet.indexOf('|', firstPipe + 1);

        if (firstPipe != -1 && secondPipe != -1) {
            gridButtons[btnIndex].entity_id = triplet.substring(0, firstPipe);
            gridButtons[btnIndex].name = triplet.substring(firstPipe + 1, secondPipe);
            gridButtons[btnIndex].type = "light"; 
            gridButtons[btnIndex].state = triplet.substring(secondPipe + 1);
            btnIndex++;
        }
        startIndex = endIndex + 1;
    }

  }
  http.end();
  hideBusyIndicator();
}

void loadGroupSwitches() {
  if (homeAssistantAddress == "") return;
  showBusyIndicator();
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  WiFiClient client;
  String apiUrl = homeAssistantAddress + "/api/template";
  
  // Template per ottenere entity_id, friendly_name e state separati da | e coppie separate da ;
  String templateBody = "{% set entities = state_attr('switch.gruppo_switch', 'entity_id') %}{% if entities %}{% for entity in entities %}{{ entity }}|{{ state_attr(entity, 'friendly_name') }}|{{ states(entity) }}{% if not loop.last %};{% endif %}{% endfor %}{% endif %}";
  String jsonPayload = "{\"template\":\"" + templateBody + "\"}";

  http.begin(client, apiUrl);
  http.addHeader("Authorization", "Bearer " + homeAssistantToken);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(jsonPayload);
  if (httpResponseCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // Pulisci i pulsanti
    for (int i = 0; i < NUM_GRID_BUTTONS; i++) {
        gridButtons[i].entity_id = "";
        gridButtons[i].name = "";
        gridButtons[i].type = "";
        gridButtons[i].state = "off";
    }

    int startIndex = 0;
    int btnIndex = 0;
    while (startIndex < payload.length() && btnIndex < NUM_GRID_BUTTONS) {
        int endIndex = payload.indexOf(';', startIndex);
        if (endIndex == -1) endIndex = payload.length();
        
        String triplet = payload.substring(startIndex, endIndex);
        int firstPipe = triplet.indexOf('|');
        int secondPipe = triplet.indexOf('|', firstPipe + 1);

        if (firstPipe != -1 && secondPipe != -1) {
            gridButtons[btnIndex].entity_id = triplet.substring(0, firstPipe);
            gridButtons[btnIndex].name = triplet.substring(firstPipe + 1, secondPipe);
            gridButtons[btnIndex].type = "switch"; 
            gridButtons[btnIndex].state = triplet.substring(secondPipe + 1);
            btnIndex++;
        }
        startIndex = endIndex + 1;
    }
  }
  http.end();
  hideBusyIndicator();
}

void loadGroupScripts() {
  if (homeAssistantAddress == "") return;
  showBusyIndicator();
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  WiFiClient client;
  String apiUrl = homeAssistantAddress + "/api/template";
  
  // Template per ottenere tutti gli script
  String templateBody = "{% for state in states.script %}{{ state.entity_id }}|{{ state.name }}|{{ state.state }}{% if not loop.last %};{% endif %}{% endfor %}";
  String jsonPayload = "{\"template\":\"" + templateBody + "\"}";

  http.begin(client, apiUrl);
  http.addHeader("Authorization", "Bearer " + homeAssistantToken);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(jsonPayload);
  if (httpResponseCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // Pulisci i pulsanti
    for (int i = 0; i < NUM_GRID_BUTTONS; i++) {
        gridButtons[i].entity_id = "";
        gridButtons[i].name = "";
        gridButtons[i].type = "";
        gridButtons[i].state = "off";
    }

    int startIndex = 0;
    int btnIndex = 0;
    while (startIndex < payload.length() && btnIndex < NUM_GRID_BUTTONS) {
        int endIndex = payload.indexOf(';', startIndex);
        if (endIndex == -1) endIndex = payload.length();
        
        String triplet = payload.substring(startIndex, endIndex);
        int firstPipe = triplet.indexOf('|');
        int secondPipe = triplet.indexOf('|', firstPipe + 1);

        if (firstPipe != -1 && secondPipe != -1) {
            gridButtons[btnIndex].entity_id = triplet.substring(0, firstPipe);
            gridButtons[btnIndex].name = triplet.substring(firstPipe + 1, secondPipe);
            gridButtons[btnIndex].type = "script"; 
            gridButtons[btnIndex].state = triplet.substring(secondPipe + 1);
            btnIndex++;
        }
        startIndex = endIndex + 1;
    }
  }
  http.end();
  hideBusyIndicator();
}

void fetchLightDetails(String entity_id) {
    if (homeAssistantAddress == "") return;
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    WiFiClient client;
    String url = homeAssistantAddress + "/api/states/" + entity_id;
    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + homeAssistantToken);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        // Parse state
        int stateIdx = payload.indexOf("\"state\"");
        if (stateIdx != -1) {
            int valStart = payload.indexOf("\"", stateIdx + 7) + 1;
            int valEnd = payload.indexOf("\"", valStart);
            selectedLightState = payload.substring(valStart, valEnd);
        }
        // Parse brightness
        int briIdx = payload.indexOf("\"brightness\"");
        if (briIdx != -1) {
            int valStart = payload.indexOf(":", briIdx) + 1;
            int valEnd = payload.indexOf(",", valStart);
            if (valEnd == -1) valEnd = payload.indexOf("}", valStart);
            String briStr = payload.substring(valStart, valEnd);
            selectedLightBrightness = briStr.toInt();
        } else {
            selectedLightBrightness = (selectedLightState == "on") ? 255 : 0;
        }
    }
    http.end();
}

void setLightBrightness(String entity_id, int brightness) {
    if (homeAssistantAddress == "") return;
    WiFiClient client;
    HTTPClient http;
    String apiUrl = homeAssistantAddress + "/api/services/light/turn_on";
    http.begin(client, apiUrl);
    http.addHeader("Authorization", "Bearer " + homeAssistantToken);
    http.addHeader("Content-Type", "application/json");
    String jsonPayload = "{\"entity_id\":\"" + entity_id + "\", \"brightness\":" + String(brightness) + "}";
    http.POST(jsonPayload);
    http.end();
}

void drawLightControlPage() {
    const int header_height = 80;
    const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
    const int col2_width = M5EPD_PANEL_W - col2_start_x;
    
    // Pulisci area
    canvas.fillRect(col2_start_x, header_height, col2_width, M5EPD_PANEL_H - header_height, WHITE);
    
    // Titolo (Nome Luce)
    canvas.setFreeFont(&FreeSansBold18pt7b);
    canvas.setTextColor(BLACK);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(selectedLightName, col2_start_x + col2_width / 2, header_height + 30);
    
    // Switch ON/OFF
    int swW = 200;
    int swH = 80;
    int swX = col2_start_x + (col2_width - swW) / 2;
    int swY = header_height + 100;
    
    canvas.fillRect(swX, swY, swW, swH, (selectedLightState == "on") ? BLACK : WHITE);
    canvas.drawRect(swX, swY, swW, swH, BLACK);
    canvas.setTextColor((selectedLightState == "on") ? WHITE : BLACK);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString((selectedLightState == "on") ? "ON" : "OFF", swX + swW / 2, swY + swH / 2);
    
    // Slider Luminosità
    int slX = col2_start_x + 40;
    int slY = swY + swH + 80;
    int slW = col2_width - 80;
    int slH = 40;
    
    canvas.drawRect(slX, slY, slW, slH, BLACK);
    int fillW = map(selectedLightBrightness, 0, 255, 0, slW);
    if (fillW > 0) canvas.fillRect(slX, slY, fillW, slH, BLACK);
    
    canvas.setTextColor(BLACK);
    canvas.setFreeFont(&FreeSans12pt7b);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString("Luminosita': " + String(map(selectedLightBrightness, 0, 255, 0, 100)) + "%", col2_start_x + col2_width / 2, slY - 30);

    // Pulsante Indietro (riutilizzato dal grafico ma posizionato qui)
    // Non serve disegnarlo esplicitamente se usiamo la logica di navigazione laterale, 
    // ma per chiarezza aggiungiamo un pulsante esplicito in basso.
    // (Opzionale, l'utente può premere "LUCI" a sinistra per tornare indietro)
    
    canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}

void drawHotspotControl() {
    const int header_height = 80;
    const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
    const int col2_width = M5EPD_PANEL_W - col2_start_x;
    const int area_h = M5EPD_PANEL_H - header_height;

    // Pulisci area
    canvas.fillRect(col2_start_x, header_height, col2_width, area_h, WHITE);

    // Titolo
    canvas.setFreeFont(&FreeSansBold18pt7b);
    canvas.setTextColor(BLACK);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString("Hotspot WiFi", col2_start_x + col2_width / 2, header_height + 30);

    // Stato Hotspot
    wifi_mode_t mode = WiFi.getMode();
    bool isApOn = (mode == WIFI_AP) || (mode == WIFI_AP_STA);
    
    // Switch ON/OFF
    int swW = 200;
    int swH = 80;
    int swX = col2_start_x + (col2_width - swW) / 2;
    int swY = header_height + 150;
    
    canvas.fillRect(swX, swY, swW, swH, isApOn ? BLACK : WHITE);
    canvas.drawRect(swX, swY, swW, swH, BLACK);
    canvas.setTextColor(isApOn ? WHITE : BLACK);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString(isApOn ? "ON" : "OFF", swX + swW / 2, swY + swH / 2);

    // Info IP
    canvas.setTextColor(BLACK);
    canvas.setFreeFont(&FreeSans12pt7b);
    canvas.setTextDatum(TC_DATUM);
    if (isApOn) {
        canvas.drawString("IP: " + WiFi.softAPIP().toString(), col2_start_x + col2_width / 2, swY + swH + 40);
        canvas.drawString("SSID: M5Paper_Hotspot", col2_start_x + col2_width / 2, swY + swH + 70);
        canvas.drawString("Password: (nessuna)", col2_start_x + col2_width / 2, swY + swH + 100);
    } else {
        canvas.drawString("Hotspot disattivato", col2_start_x + col2_width / 2, swY + swH + 40);
    }
}

void drawCalendar() {
    const int header_height = 80;
    const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
    const int col2_width = M5EPD_PANEL_W - col2_start_x;
    const int area_h = M5EPD_PANEL_H - header_height;

    // Pulisci area (WHITE = Sfondo Nero con inversione)
    canvas.fillRect(col2_start_x, header_height, col2_width, area_h, WHITE);
    
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        return;
    }

    int realYear = timeinfo.tm_year + 1900;
    int realMonth = timeinfo.tm_mon;
    int realDay = timeinfo.tm_mday;

    int displayMonth = realMonth + calendarMonthOffset;
    int displayYear = realYear;

    while (displayMonth > 11) {
        displayMonth -= 12;
        displayYear++;
    }
    while (displayMonth < 0) {
        displayMonth += 12;
        displayYear--;
    }

    const char* months[] = {"Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno", "Luglio", "Agosto", "Settembre", "Ottobre", "Novembre", "Dicembre"};
    
    canvas.setFreeFont(&FreeSansBold18pt7b);
    canvas.setTextColor(BLACK); // Testo Bianco
    canvas.setTextDatum(TC_DATUM);
    int titleY = header_height + 30;
    canvas.drawString(String(months[displayMonth]) + " " + String(displayYear), col2_start_x + col2_width / 2, titleY);

    // Frecce navigazione
    canvas.drawString("<", col2_start_x + 40, titleY);
    canvas.drawString(">", col2_start_x + col2_width - 40, titleY);

    const char* weekDays[] = {"Lun", "Mar", "Mer", "Gio", "Ven", "Sab", "Dom"};
    int start_y = header_height + 80;
    int cell_w = col2_width / 7;
    int cell_h = (area_h - 100) / 6; 

    canvas.setFreeFont(&FreeSans12pt7b);
    for(int i=0; i<7; i++) {
        canvas.setTextDatum(MC_DATUM);
        canvas.drawString(weekDays[i], col2_start_x + i*cell_w + cell_w/2, start_y);
    }

    // Primo giorno del mese visualizzato
    struct tm firstDayStruct = {0};
    firstDayStruct.tm_year = displayYear - 1900;
    firstDayStruct.tm_mon = displayMonth;
    firstDayStruct.tm_mday = 1;
    mktime(&firstDayStruct);
    int firstWeekDay = firstDayStruct.tm_wday; // 0=Sun
    int startCol = (firstWeekDay + 6) % 7; // 0=Mon

    int daysInMonth;
    if (displayMonth == 1) {
        if ((displayYear % 4 == 0 && displayYear % 100 != 0) || (displayYear % 400 == 0)) daysInMonth = 29;
        else daysInMonth = 28;
    } else if (displayMonth == 3 || displayMonth == 5 || displayMonth == 8 || displayMonth == 10) {
        daysInMonth = 30;
    } else {
        daysInMonth = 31;
    }

    int currentDay = 1;
    int row = 1;
    int col = startCol;
    int grid_y_start = start_y + 40;

    canvas.setFreeFont(&FreeSansBold12pt7b);

    // Disegna Griglia
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 7; c++) {
             int x = col2_start_x + c * cell_w;
             int y = grid_y_start + r * cell_h;
             canvas.drawRect(x, y, cell_w, cell_h, BLACK);
        }
    }

    while (currentDay <= daysInMonth) {
        int x = col2_start_x + col * cell_w;
        int y = grid_y_start + (row - 1) * cell_h;

        if (displayMonth == realMonth && displayYear == realYear && currentDay == realDay) {
            canvas.fillCircle(x + cell_w/2, y + cell_h/2, min(cell_w, cell_h)/2 - 5, BLACK); // Cerchio Bianco
            canvas.setTextColor(WHITE); // Testo Nero
        } else {
            canvas.setTextColor(BLACK); // Testo Bianco
        }
        
        canvas.setTextDatum(MC_DATUM);
        canvas.drawString(String(currentDay), x + cell_w/2, y + cell_h/2);

        currentDay++;
        col++;
        if (col > 6) {
            col = 0;
            row++;
        }
    }
}

void drawAnalogClock() {
    const int header_height = 80;
    const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
    const int col2_width = M5EPD_PANEL_W - col2_start_x;
    const int area_h = M5EPD_PANEL_H - header_height;
    
    int cx = col2_start_x + col2_width / 2;
    int cy = header_height + area_h / 2;
    int r = min(col2_width, area_h) / 2 - 20;

    // Pulisci area
    canvas.fillRect(col2_start_x, header_height, col2_width, area_h, WHITE);

    // Disegna Quadrante
    canvas.drawCircle(cx, cy, r, BLACK);
    canvas.drawCircle(cx, cy, r - 1, BLACK);
    
    canvas.setTextDatum(MC_DATUM);
    canvas.setFreeFont(&FreeSansBold18pt7b);
    canvas.setTextColor(BLACK);

    for (int i = 0; i < 12; i++) {
        float angle = i * 30 * 0.0174532925;
        int x1 = cx + (r - 20) * sin(angle);
        int y1 = cy - (r - 20) * cos(angle);
        int x2 = cx + r * sin(angle);
        int y2 = cy - r * cos(angle);
        canvas.drawLine(x1, y1, x2, y2, 3, BLACK);

        int numR = r - 50;
        int nx = cx + numR * sin(angle);
        int ny = cy - numR * cos(angle);
        int hour = i == 0 ? 12 : i;
        canvas.drawString(String(hour), nx, ny);
    }

    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        return;
    }

    // Lancette
    float h_angle = (timeinfo.tm_hour % 12 + timeinfo.tm_min / 60.0) * 30 * 0.0174532925;
    float m_angle = (timeinfo.tm_min + timeinfo.tm_sec / 60.0) * 6 * 0.0174532925;
    float s_angle = timeinfo.tm_sec * 6 * 0.0174532925;

    // Ore
    int hx = cx + (r * 0.5) * sin(h_angle);
    int hy = cy - (r * 0.5) * cos(h_angle);
    canvas.drawLine(cx, cy, hx, hy, 6, BLACK);

    // Minuti
    int mx = cx + (r * 0.75) * sin(m_angle);
    int my = cy - (r * 0.75) * cos(m_angle);
    canvas.drawLine(cx, cy, mx, my, 4, BLACK);

    // Secondi
    int sx = cx + (r * 0.85) * sin(s_angle);
    int sy = cy - (r * 0.85) * cos(s_angle);
    canvas.drawLine(cx, cy, sx, sy, 2, BLACK);
    
    // Centro
    canvas.fillCircle(cx, cy, 5, BLACK);
}

void loadGroupSensors() {
  if (homeAssistantAddress == "") return;
  showBusyIndicator();
  if (WiFi.status() != WL_CONNECTED) return;

  // Preserva gli stati esistenti per evitare "N/A" durante il cambio pagina
  String timeState = getSensorState("sensor.time");
  String dateState = getSensorState("sensor.oggi");
  String pageState = getSensorState("input_number.epaper_pagina");

  if (timeState == "") timeState = "N/A";
  if (dateState == "") dateState = "N/A";
  if (pageState == "") pageState = "0";

  // Reset sensors vector to default system sensors
  sensors.clear();
  sensors.push_back({"sensor.time", "Time", "", timeState});
  sensors.push_back({"sensor.oggi", "Oggi", "", dateState});
  sensors.push_back({"input_number.epaper_pagina", "Pagina", "", pageState});

  HTTPClient http;
  WiFiClient client;
  String apiUrl = homeAssistantAddress + "/api/template";
  
  // Template per ottenere entity_id, friendly_name e state
  String templateBody = "{% set entities = state_attr('sensor.gruppo_sensori', 'entity_id') %}{% if entities %}{% for entity in entities %}{{ entity }}|{{ state_attr(entity, 'friendly_name') }}|{{ states(entity) }}{% if not loop.last %};{% endif %}{% endfor %}{% endif %}";
  String jsonPayload = "{\"template\":\"" + templateBody + "\"}";

  http.begin(client, apiUrl);
  http.addHeader("Authorization", "Bearer " + homeAssistantToken);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(jsonPayload);
  if (httpResponseCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    int startIndex = 0;
    while (startIndex < payload.length()) {
        int endIndex = payload.indexOf(';', startIndex);
        if (endIndex == -1) endIndex = payload.length();
        
        String triplet = payload.substring(startIndex, endIndex);
        int firstPipe = triplet.indexOf('|');
        int secondPipe = triplet.indexOf('|', firstPipe + 1);

        if (firstPipe != -1 && secondPipe != -1) {
            String entity_id = triplet.substring(0, firstPipe);
            String name = triplet.substring(firstPipe + 1, secondPipe);
            String state = triplet.substring(secondPipe + 1);
            
            sensors.push_back({entity_id, name, "", state});
        }
        startIndex = endIndex + 1;
    }
  }
  http.end();
  hideBusyIndicator();
}

void showBusyIndicator() {
    // Disegna un piccolo indicatore (quadrato nero) in alto a destra
    M5EPD_Canvas busyCanvas(&M5.EPD);
    busyCanvas.createCanvas(20, 20);
    busyCanvas.fillCanvas(BLACK);
    busyCanvas.pushCanvas(M5EPD_PANEL_W - 20, 0, UPDATE_MODE_DU);
}

void hideBusyIndicator() {
    // Nasconde l'indicatore (disegna bianco)
    M5EPD_Canvas busyCanvas(&M5.EPD);
    busyCanvas.createCanvas(20, 20);
    busyCanvas.fillCanvas(WHITE);
    busyCanvas.pushCanvas(M5EPD_PANEL_W - 20, 0, UPDATE_MODE_DU);
}

void enterDeepSleep() {
    Serial.println("Entering Deep Sleep...");

    // Mostra messaggio "Zzz..." al centro dello schermo prima di dormire
    int w = 200;
    int h = 100;
    int x = (M5EPD_PANEL_W - w) / 2;
    int y = (M5EPD_PANEL_H - h) / 2;

    // Usa una canvas temporanea per il messaggio popup
    M5EPD_Canvas sleepCanvas(&M5.EPD);
    sleepCanvas.createCanvas(w, h);

    // Con SetColorReverse(true): BLACK=Bianco, WHITE=Nero
    sleepCanvas.fillRect(0, 0, w, h, WHITE); // Sfondo bianco
    sleepCanvas.drawRect(0, 0, w, h, BLACK); // Bordo nero
    sleepCanvas.setFreeFont(&FreeSansBold24pt7b);
    sleepCanvas.setTextDatum(MC_DATUM);
    sleepCanvas.setTextColor(BLACK); // Testo nero
    sleepCanvas.drawString("Zzz...", w / 2, h / 2); // Coordinate relative alla canvas locale
    sleepCanvas.pushCanvas(x, y, UPDATE_MODE_GL16);

    preferences.begin("epaper", false);
    preferences.putInt("page", currentPage);
    preferences.putBool("darkMode", isDarkMode);
    preferences.end();
    
    // Disconnetti WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    // Configura il risveglio
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, 0); // Risveglio al tocco (GPIO 36 LOW)
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_TIME); // Risveglio col timer
    
    M5.EPD.Sleep(); // Spegne il controller del display
    esp_deep_sleep_start();
}

// --- FUNZIONI SERVER WEB ---
void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;text-align:center;padding:20px;background-color:#f0f0f0;}";
    html += "button{padding:15px 30px;font-size:18px;margin:10px;cursor:pointer;border:none;border-radius:5px;background-color:#007bff;color:white;}";
    html += "button:hover{background-color:#0056b3;}</style></head><body>";
    html += "<h1>M5Paper Control</h1>";
    html += "<p><strong>Batteria:</strong> " + String(getInternalBatteryPercentage()) + "%</p>";
    html += "<p><strong>Pagina Corrente:</strong> " + String(currentPage) + "</p>";
    html += "<p><a href='/refresh'><button>Aggiorna Schermo</button></a></p>";
    html += "<p><a href='/toggle_dark'><button>Toggle Dark Mode</button></a></p>";
    html += "<p><a href='/screenshot' target='_blank'><button>Screenshot</button></a></p>";
    html += "<h3>Cambia Pagina</h3>";
    html += "<p><a href='/set_page?page=0'><button>Pagina Sensori</button></a></p>";
    html += "<p><a href='/set_page?page=1'><button>Pagina Home</button></a></p>";
    html += "<p><a href='/set_page?page=2'><button>Pagina Luci</button></a></p>";
    html += "<p><a href='/set_page?page=3'><button>Pagina Switch</button></a></p>";
    html += "<p><a href='/wifi'><button>Configurazione</button></a></p>";
    html += "<p><a href='/reset' onclick=\"return confirm('Sei sicuro di voler cancellare tutte le impostazioni e riavviare?');\"><button style='background-color:red;'>Factory Reset</button></a></p>";
    html += "<p><a href='/status'><button>Stato JSON</button></a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleStatus() {
    String json = "{";
    json += "\"uptime\":" + String(millis() / 1000) + ",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"battery\":" + String(getInternalBatteryPercentage()) + ",";
    json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"page\":" + String(currentPage);
    json += "}";
    server.send(200, "application/json", json);
}

void handleWebRefresh() {
    drawFullUI(false);
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleWebToggleDark() {
    // Simula il tocco sulla batteria per cambiare modalità
    isDarkMode = !isDarkMode;
    preferences.begin("epaper", false);
    preferences.putBool("darkMode", isDarkMode);
    preferences.end();
    drawFullUI(false);
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleScreenshot() {
    WiFiClient client = server.client();
    if (!client) return;

    // Dimensioni e parametri BMP
    uint32_t width = M5EPD_PANEL_W;
    uint32_t height = M5EPD_PANEL_H;
    uint16_t depth = 4; // 4 bit per pixel (16 colori)
    uint32_t rowSize = (width * depth + 31) / 32 * 4; // Allineamento a 4 byte
    uint32_t imageSize = rowSize * height;
    uint32_t paletteSize = 16 * 4;
    uint32_t headerSize = 14 + 40 + paletteSize;
    uint32_t fileSize = headerSize + imageSize;

    // Invia header HTTP
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: image/bmp\r\n";
    response += "Content-Length: " + String(fileSize) + "\r\n";
    response += "Connection: close\r\n\r\n";
    client.print(response);

    // Bitmap File Header (14 bytes)
    uint8_t fileHeader[14];
    fileHeader[0] = 'B'; fileHeader[1] = 'M';
    memcpy(&fileHeader[2], &fileSize, 4);
    memset(&fileHeader[6], 0, 4);
    memcpy(&fileHeader[10], &headerSize, 4);
    client.write(fileHeader, 14);

    // Bitmap Info Header (40 bytes)
    uint8_t infoHeader[40] = {0};
    uint32_t biSize = 40;
    int32_t biWidth = width;
    int32_t biHeight = -(int32_t)height; // Negativo per top-down (origine in alto a sinistra)
    uint16_t biPlanes = 1;
    uint16_t biBitCount = depth;
    
    memcpy(&infoHeader[0], &biSize, 4);
    memcpy(&infoHeader[4], &biWidth, 4);
    memcpy(&infoHeader[8], &biHeight, 4);
    memcpy(&infoHeader[12], &biPlanes, 2);
    memcpy(&infoHeader[14], &biBitCount, 2);
    memcpy(&infoHeader[20], &imageSize, 4);
    client.write(infoHeader, 40);

    // Color Table (Palette)
    uint8_t palette[16 * 4];
    for (int i = 0; i < 16; i++) {
        uint8_t val = i * 17; // Scala 0-15 a 0-255
        if (isDarkMode) val = 255 - val; // Inverti se in Dark Mode per riflettere lo schermo
        palette[i * 4 + 0] = val; // B
        palette[i * 4 + 1] = val; // G
        palette[i * 4 + 2] = val; // R
        palette[i * 4 + 3] = 0;   // Reserved
    }
    client.write(palette, 16 * 4);

    // Image Data
    // Il buffer della canvas è già packed 4bpp (2 pixel per byte), compatibile con BMP
    uint8_t* buffer = (uint8_t*)canvas.frameBuffer();
    
    // Invia in blocchi per efficienza
    uint32_t chunkSize = 4096;
    uint32_t sent = 0;
    while (sent < imageSize) {
        uint32_t toSend = min(chunkSize, imageSize - sent);
        client.write(buffer + sent, toSend);
        sent += toSend;
    }
}

void handleWifiConfig() {
    int n = WiFi.scanNetworks();
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;text-align:center;padding:20px;}input,select,button{padding:10px;font-size:16px;margin:10px;width:80%;max-width:300px;}</style></head><body>";
    html += "<h1>Configurazione</h1>";
    html += "<form action='/save_wifi' method='POST'>";
    html += "<h3>WiFi</h3>";
    html += "<label>Seleziona Rete:</label><br>";
    html += "<select name='ssid'>";
    for (int i = 0; i < n; ++i) {
        String selected = (WiFi.SSID(i) == ssid) ? " selected" : "";
        html += "<option value='" + WiFi.SSID(i) + "'" + selected + ">" + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + "dBm)</option>";
    }
    html += "</select><br>";
    html += "<input type='password' name='password' placeholder='Password' value='" + password + "'><br>";
    
    html += "<h3>Home Assistant</h3>";
    html += "<label>Indirizzo:</label><br>";
    html += "<input type='text' name='ha_address' value='" + homeAssistantAddress + "'><br>";
    html += "<label>Token:</label><br>";
    html += "<input type='text' name='ha_token' value='" + homeAssistantToken + "'><br>";
    
    html += "<h3>MQTT</h3>";
    html += "<label>Server:</label><br>";
    html += "<input type='text' name='mqtt_server' value='" + mqtt_server + "'><br>";
    html += "<label>Porta:</label><br>";
    html += "<input type='number' name='mqtt_port' value='" + String(mqtt_port) + "'><br>";
    html += "<label>Utente:</label><br>";
    html += "<input type='text' name='mqtt_user' value='" + mqtt_user + "'><br>";
    html += "<label>Password:</label><br>";
    html += "<input type='password' name='mqtt_password' value='" + mqtt_password + "'><br>";

    html += "<button type='submit'>Salva e Connetti</button>";
    html += "</form>";
    html += "<p><a href='/'>Indietro</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleSaveWifi() {
    if (server.hasArg("ssid")) {
        String new_ssid = server.arg("ssid");
        String new_pass = server.arg("password");
        String new_ha_address = server.arg("ha_address");
        String new_ha_token = server.arg("ha_token");
        
        preferences.begin("epaper", false);
        preferences.putString("ssid", encryptConfig(new_ssid));
        preferences.putString("password", encryptConfig(new_pass));
        if (new_ha_address.length() > 0) preferences.putString("ha_address", encryptConfig(new_ha_address));
        if (new_ha_token.length() > 0) preferences.putString("ha_token", encryptConfig(new_ha_token));
        
        if (server.hasArg("mqtt_server")) preferences.putString("mqtt_server", encryptConfig(server.arg("mqtt_server")));
        if (server.hasArg("mqtt_port")) preferences.putInt("mqtt_port", server.arg("mqtt_port").toInt());
        if (server.hasArg("mqtt_user")) preferences.putString("mqtt_user", encryptConfig(server.arg("mqtt_user")));
        if (server.hasArg("mqtt_password")) preferences.putString("mqtt_password", encryptConfig(server.arg("mqtt_password")));
        
        preferences.end();
        
        String html = "<html><body><h1>Salvato!</h1><p>Riavvio in corso...</p></body></html>";
        server.send(200, "text/html", html);
        delay(1000);
        ESP.restart();
    } else {
        server.send(400, "text/plain", "Dati mancanti");
    }
}

void handleFactoryReset() {
    preferences.begin("epaper", false);
    preferences.clear();
    preferences.end();
    
    String html = "<!DOCTYPE html><html><body><h1>Reset Eseguito</h1><p>Tutte le impostazioni sono state cancellate. Il dispositivo si riavvier&agrave; tra pochi secondi.</p></body></html>";
    if (server.client()) {
        server.send(200, "text/html", html);
    }
    delay(2000);
    ESP.restart();
}

void handleSetPage() {
    if (server.hasArg("page")) {
        int page = server.arg("page").toInt();
        int haPageValue = 0;
        if (page == 0) haPageValue = 4; // SENSORI
        else if (page == 1) haPageValue = 3; // HOME
        else if (page == 2) haPageValue = 2; // LUCI
        else if (page == 3) haPageValue = 1; // SWITCH

        if (haPageValue != 0) {
            setPageInputNumber(haPageValue);
            // Non è necessario ridisegnare qui, il loop principale lo farà tramite updateStates
        }
        server.sendHeader("Location", "/");
        server.send(303);
    } else {
        server.send(400, "text/plain", "Parametro 'page' mancante");
    }
}

// --- CRITTOGRAFIA ---
String getEncryptionKey() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    return mac;
}

String encryptConfig(String input) {
    if (input.length() == 0) return "";
    String key = getEncryptionKey();
    String output = "ENC:";
    for (int i = 0; i < input.length(); i++) {
        unsigned char c = (unsigned char)input[i] ^ (unsigned char)key[i % key.length()];
        if (c < 16) output += "0";
        output += String(c, HEX);
    }
    return output;
}

String decryptConfig(String input) {
    if (!input.startsWith("ENC:")) return input;
    input = input.substring(4);
    String key = getEncryptionKey();
    String output = "";
    for (unsigned int i = 0; i < input.length(); i += 2) {
        String byteStr = input.substring(i, i + 2);
        unsigned char c = (unsigned char)strtol(byteStr.c_str(), NULL, 16);
        output += (char)(c ^ (unsigned char)key[(i / 2) % key.length()]);
    }
    return output;
}

void setup() {
  // --- INIZIALIZZAZIONE DISPOSITIVO ---
  // Inizializza il dispositivo M5Paper
  M5.begin();
  Serial.begin(115200);

  // --- PROMPT RESET IMPOSTAZIONI ---
  M5.EPD.Clear(true);
  M5.EPD.SetColorReverse(false);
  canvas.createCanvas(M5EPD_PANEL_W, M5EPD_PANEL_H);
  canvas.fillCanvas(WHITE);
  canvas.setTextColor(BLACK);
  canvas.setFreeFont(&FreeSansBold18pt7b);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawString("Tocca per RESETTARE", M5EPD_PANEL_W / 2, M5EPD_PANEL_H / 2);
  canvas.pushCanvas(0, 0, UPDATE_MODE_DU);

  unsigned long startWait = millis();
  bool performReset = false;
  while (millis() - startWait < 3000) {
      M5.TP.update();
      if (!M5.TP.isFingerUp()) {
          performReset = true;
          break;
      }
      delay(10);
  }

  if (performReset) {
      canvas.fillCanvas(WHITE);
      canvas.drawString("Reset in corso...", M5EPD_PANEL_W / 2, M5EPD_PANEL_H / 2);
      canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
      
      preferences.begin("epaper", false);
      preferences.clear();
      preferences.end();
      
      delay(1000);
      canvas.fillCanvas(WHITE);
      canvas.drawString("Fatto! Riavvio...", M5EPD_PANEL_W / 2, M5EPD_PANEL_H / 2);
      canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
      delay(1000);
      ESP.restart();
  }
  
  canvas.fillCanvas(WHITE);
  canvas.drawString("Avvio...", M5EPD_PANEL_W / 2, M5EPD_PANEL_H / 2);
  canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
 
  // Inizializza il filesystem SPIFFS per il font (se necessario)
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  // Recupera l'ultima pagina salvata
  currentPage = 1; // Imposta sempre la pagina HOME all'avvio

  preferences.begin("epaper", true);
  isDarkMode = preferences.getBool("darkMode", false);
  ssid = decryptConfig(preferences.getString("ssid", ""));
  password = decryptConfig(preferences.getString("password", ""));
  
  String savedHaAddress = decryptConfig(preferences.getString("ha_address", ""));
  if (savedHaAddress != "") homeAssistantAddress = savedHaAddress;
  
  String savedHaToken = decryptConfig(preferences.getString("ha_token", ""));
  if (savedHaToken != "") homeAssistantToken = savedHaToken;
  
  mqtt_server = decryptConfig(preferences.getString("mqtt_server", mqtt_server));
  mqtt_port = preferences.getInt("mqtt_port", mqtt_port);
  mqtt_user = decryptConfig(preferences.getString("mqtt_user", mqtt_user));
  mqtt_password = decryptConfig(preferences.getString("mqtt_password", mqtt_password));

  preferences.end();

  pinMode(36, INPUT); // Configura il pin del touch per il risveglio

  // Popola la lista dei sensori
  // I sensori di tempo e data sono necessari per l'header, ma non verranno visualizzati nella lista.
  sensors.push_back({"sensor.time", "Time", "", "N/A"});
  sensors.push_back({"sensor.oggi", "Oggi", "", "N/A"});
  sensors.push_back({"input_number.epaper_pagina", "Pagina", "", "0"});

  // Inizializza gridButtons con i valori di default
  for (int i = 0; i < NUM_GRID_BUTTONS; i++) {
      gridButtons[i] = defaultGridButtons[i];
  }

  // --- CONNESSIONE WIFI ---
  // Attiva AP+STA per avere sia hotspot che connessione client
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("M5Paper_Hotspot"); // Hotspot senza password
  WiFi.setSleep(false); // Disabilita il risparmio energetico per rendere il server web reattivo
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  if (ssid != "") {
      Serial.println("Connecting to saved WiFi: " + ssid);
      WiFi.begin(ssid.c_str(), password.c_str());
      
      // Timeout di 10 secondi per la connessione, poi prosegue
      int retries = 0;
      while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
      }
  }

  if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected");
      WiFi.mode(WIFI_STA); // Disabilita l'hotspot se connesso al router
      Serial.println("Hotspot disabled.");
      configTime(0, 0, "pool.ntp.org"); 
      setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
      tzset();
  } else {
      Serial.println("\nWiFi not connected (AP Mode active)");
  }

  // --- AVVIO SERVER WEB ---
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/refresh", handleWebRefresh);
  server.on("/toggle_dark", handleWebToggleDark);
  server.on("/screenshot", handleScreenshot);
  server.on("/wifi", handleWifiConfig);
  server.on("/save_wifi", handleSaveWifi);
  server.on("/reset", handleFactoryReset);
  server.on("/set_page", handleSetPage);
  server.begin();
  Serial.println("HTTP server started");

  // Sincronizza Home Assistant sulla pagina HOME (valore 3)
  if (homeAssistantAddress != "") {
      setPageInputNumber(3);
  }

  if (currentPage == 0) {
      loadGroupSensors();
  }

  // --- IMPOSTAZIONI MQTT ---
  // Impostazioni MQTT
  mqttClient.setServer(mqtt_server.c_str(), mqtt_port);
  mqttClient.setCallback(mqttCallback);

  // --- DISEGNO INIZIALE ---
  // Disegna l'interfaccia completa per la prima volta
  drawFullUI(false); // false = non sincronizzare la pagina da HA (forza HOME)
  lastActivityTime = millis();
}
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  Serial.println("Message arrived on topic: " + topicStr);

  // Se arriva un messaggio sul topic di aggiornamento, ridisegna tutto (tranne pagine speciali).
  if (topicStr == "m5paper/update" && currentPage != GRAPH_PAGE && currentPage != CALENDAR_PAGE && currentPage != MEDIA_CONTROL_PAGE && currentPage != SCRIPT_PAGE) {
    Serial.println("Update command received via MQTT. Refreshing screen...");
    // CORREZIONE: Esegui un aggiornamento parziale invece di un refresh completo.
    updateStates(true, true); // Ottieni i nuovi stati
    drawHeader();
    drawButtons();            // Ridisegna i pulsanti a sinistra
    if (currentPage == 0) drawSensors(); 
    else if (currentPage == CLOCK_PAGE) drawAnalogClock();
    else {
        if (currentPage == 1) {
             wifi_mode_t mode = WiFi.getMode();
             gridButtons[0].state = ((mode == WIFI_AP) || (mode == WIFI_AP_STA)) ? "on" : "off";
        }
        drawGridButtons(); // Ridisegna la griglia
    }
    canvas.pushCanvas(0, 0, UPDATE_MODE_DU); // Applica le modifiche con un refresh veloce
    lastActivityTime = millis(); // Resetta il timer di sonno
  } else {
    // Qui puoi gestire altri topic se necessario in futuro
  }
}

void reconnectMqtt() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect("M5PaperClient", mqtt_user.c_str(), mqtt_password.c_str())) {
      Serial.println("connected");
      // Iscriviti al topic di aggiornamento generale
      mqttClient.subscribe("m5paper/update");
      Serial.println("Subscribed to topic: m5paper/update");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void drawFullUI(bool syncPage) {
  // CORREZIONE: Resetta la posizione Y globale all'inizio di ogni ridisegno completo
  // CORREZIONE COLORI: Assicura che la tavolozza non sia invertita. 0=nero, 255=bianco.
  M5.EPD.SetColorReverse(isDarkMode); // False = Light Theme, True = Dark Theme
  M5.EPD.Clear(true);
  // Crea il canvas con le dimensioni corrispondenti alla rotazione (540x960)
  canvas.createCanvas(M5EPD_PANEL_W, M5EPD_PANEL_H);

  // Controllo configurazione Home Assistant
  if (homeAssistantAddress == "") {
      M5.EPD.SetColorReverse(false); // Forza sfondo bianco (modalità chiara) per leggibilità
      canvas.fillCanvas(WHITE);
      canvas.setTextColor(BLACK);
      
      int y = 80;
      canvas.setFreeFont(&FreeSansBold18pt7b);
      canvas.setTextDatum(TC_DATUM); // Allineamento Top-Center
      canvas.drawString("Home Assistant non configurato!", M5EPD_PANEL_W / 2, y);
      y += 50;
      
      canvas.setFreeFont(&FreeSans12pt7b);
      canvas.drawString("Configura l'indirizzo IP tramite Web Server", M5EPD_PANEL_W / 2, y);
      y += 40;
      
      String ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
      String url = "http://" + ipStr;
      canvas.drawString(url, M5EPD_PANEL_W / 2, y);
      y += 40;
      
      int qrSize = 200;
      int qrX = (M5EPD_PANEL_W - qrSize) / 2;
      canvas.qrcode(url.c_str(), qrX, y, qrSize, 2);
      
      // Pulsante Factory Reset
      int btnW = 220;
      int btnH = 50;
      int btnX = (M5EPD_PANEL_W - btnW) / 2;
      int btnY = M5EPD_PANEL_H - 80;
      
      canvas.fillRect(btnX, btnY, btnW, btnH, BLACK);
      canvas.setTextColor(WHITE);
      canvas.setTextDatum(MC_DATUM);
      canvas.drawString("Factory Reset", btnX + btnW/2, btnY + btnH/2);
      
      drawHeader();
      canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
      return;
  }

  // IMPOSTA LA POSIZIONE DEI PULSANTI DELLA GRIGLIA 1x4 NELLA PRIMA COLONNA
  const int col1_width = (M5EPD_PANEL_W * 0.3) - 22;
  const int start_y_col1 = 80 + 20; // Sotto l'header
  const int btn_x_margin = 5; // Sposta a sinistra riducendo il margine
  const int btn_y_margin = 0; // Rimuovi lo spazio verticale
  const int btn_w_col1 = col1_width - (2 * btn_x_margin);
  const int btn_h_col1 = (M5EPD_PANEL_H - start_y_col1) / NUM_DEVICES; // Calcola l'altezza per riempire lo spazio

  for (int i = 0; i < NUM_DEVICES; i++) {
    devices[i].w = btn_w_col1;
    devices[i].h = btn_h_col1;
    devices[i].x = btn_x_margin;
    devices[i].y = start_y_col1 + i * (btn_h_col1 + btn_y_margin);
  }

  // Imposta un font "bello" e più grande per il titolo
  canvas.setFreeFont(&FreeSansBold24pt7b); // Aumentata la dimensione del font per il titolo a 24pt
  // canvas.setTextSize(4); // Non più necessario, la dimensione è nel font
  canvas.fillCanvas(WHITE); // Sfondo bianco
  // La cornice è stata rimossa.
  canvas.setTextColor(BLACK); // Testo nero

  // Ottieni lo stato di ora e data prima di tutto per l'header
  updateTimeAndDateStates(); // Carica i dati per ora e data

  // Ottieni lo stato iniziale di tutti i dispositivi e sensori con una sola chiamata
  // Escludiamo i sensori perché li abbiamo già gestiti (o li gestiremo separatamente)
  // In questo caso, carichiamo tutto per semplicità.
  updateStates(true, true, syncPage); 

  drawHeader();
  drawButtons();
  if (currentPage == 0) drawSensors(); 
  else if (currentPage == CLOCK_PAGE) drawAnalogClock();
  else {
      if (currentPage == 1) {
          gridButtons[0].entity_id = "";
          gridButtons[0].name = "Hotspot";
          gridButtons[0].type = "hotspot";
          wifi_mode_t mode = WiFi.getMode();
          gridButtons[0].state = ((mode == WIFI_AP) || (mode == WIFI_AP_STA)) ? "on" : "off";
           gridButtons[1].entity_id = "";
           gridButtons[1].name = "Script";
           gridButtons[1].type = "script_page";
           gridButtons[1].state = "off";
           for (int k = 2; k < NUM_GRID_BUTTONS; k++) {
               gridButtons[k].name = "";
           }
      }
      drawGridButtons();
  }

  canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}

void drawHeader(M5EPD_Canvas* c) {
  // La scelta del darkmode deve essere sempre invertita per la barra in alto
  uint32_t headerBg = BLACK;
  uint32_t headerFg = WHITE;

  // Pulisci l'area dell'header prima di disegnare
  c->fillRect(0, 0, M5EPD_PANEL_W, 80, headerBg); 
  c->setTextColor(headerFg); 

  // Imposta il font per ora e data
  c->setFreeFont(&FreeSansBold24pt7b);

  // Disegna il titolo, l'ora e la data
  struct tm timeinfo;
  bool timeSynced = getLocalTime(&timeinfo);
  String time_str;
  
  if (timeSynced) {
      char timeBuffer[10];
      strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &timeinfo);
      time_str = String(timeBuffer);
  } else {
      time_str = getSensorState("sensor.time");
  }
  
  String date_str = getSensorState("sensor.oggi");

  if (WiFi.status() != WL_CONNECTED) {
    c->setTextDatum(TC_DATUM);
    c->drawString("AP: " + WiFi.softAPIP().toString(), M5EPD_PANEL_W / 2, 25);
  } else if (time_str == "" || time_str == "unknown") {
    c->setTextDatum(TC_DATUM);
    c->drawString("ERRORE DI COLLEGAMENTO", M5EPD_PANEL_W / 2, 25);
  } else {
    // Disegna l'ora a sinistra e la data a destra
    c->setTextDatum(TL_DATUM);
    c->drawString(time_str, 25, 25);

    // Disegna l'icona della batteria accanto all'ora
    int time_width = c->textWidth(time_str);
    int battery_icon_x = 25 + time_width + 20; // Posiziona dopo l'ora con un margine
    headerBatteryX = battery_icon_x; // Aggiorna la posizione globale per il tocco
    int internal_bat_perc = getInternalBatteryPercentage();
    drawBatteryIcon(battery_icon_x, 20, internal_bat_perc, c, headerBg, headerFg);

    c->setTextDatum(TR_DATUM);
    c->drawString(date_str, M5EPD_PANEL_W - 25, 25);
  }
}

void updateTimeAndDateStates() {
    if (homeAssistantAddress == "") return;
    showBusyIndicator();
    HTTPClient http;
    WiFiClient client;

    String apiUrl = homeAssistantAddress + "/api/template";
    http.begin(client, apiUrl);
    http.addHeader("Authorization", "Bearer " + homeAssistantToken);
    http.addHeader("Content-Type", "application/json");

    // Richiesta specifica solo per ora e data, separati da un carattere univoco
    String jsonPayload = "{\"template\":\"{{ states('sensor.time') }}|{{ states('sensor.oggi') }}\"}";

    int httpResponseCode = http.POST(jsonPayload);
    if (httpResponseCode == HTTP_CODE_OK) {
        String payload = http.getString();
        int firstSep = payload.indexOf('|');
        if (firstSep != -1) {
            String time_str = payload.substring(0, firstSep);
            String date_str = payload.substring(firstSep + 1);

            // Aggiorna direttamente gli stati nel vettore dei sensori
            for (auto& sensor : sensors) {
                if (sensor.entity_id == "sensor.time") {
                    sensor.state = time_str;
                } else if (sensor.entity_id == "sensor.oggi") {
                    sensor.state = date_str;
                }
            }
            Serial.println("Time and date updated: " + time_str + " - " + date_str);
        }
    } else {
        Serial.printf("[HTTP] Time/Date update failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
    hideBusyIndicator();
}

std::vector<float> fetchHistory(String entity_id, int hours) {
    std::vector<float> values;
    if (homeAssistantAddress == "") return values;
    if (WiFi.status() != WL_CONNECTED) return values;
    
    // Calcola il timestamp di inizio (UTC)
    time_t now;
    time(&now);
    
    // Se l'orario non è sincronizzato (es. anno 1970), evita la richiesta
    if (now < 1600000000) { 
        Serial.println("Time not synced, skipping history fetch");
        // Ritorna un vettore vuoto ma valido per evitare crash
        return values;
    }

    time_t start = now - (hours * 3600);
    struct tm * timeinfo;
    timeinfo = gmtime(&start);
    
    char buffer[30];
    strftime(buffer, 30, "%Y-%m-%dT%H:%M:%S", timeinfo);
    String timestamp = String(buffer) + "Z"; // Aggiunge Z per UTC

    HTTPClient http;
    WiFiClient client;
    
    // Recupera la storia a partire dal timestamp calcolato
    String url = homeAssistantAddress + "/api/history/period/" + timestamp + "?filter_entity_id=" + entity_id;
    
    Serial.println("Fetching history: " + url);
    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + homeAssistantToken);
    http.addHeader("Content-Type", "application/json");
    
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        // UTILIZZO STREAMING PER RISPARMIARE MEMORIA
        // Invece di caricare tutta la stringa (che causa crash), leggiamo byte per byte
        WiFiClient *stream = http.getStreamPtr();
        
        // Cerca la chiave "state" nel flusso JSON
        while (stream->available()) {
            if (stream->find("\"state\"")) {
                // Trovato "state", cerca il valore tra virgolette
                if (stream->find("\"")) {
                    String val = stream->readStringUntil('\"');
                    if (val != "unavailable" && val != "unknown") {
                        values.push_back(val.toFloat());
                        // Limite di sicurezza per evitare Out Of Memory con troppi punti
                        if (values.size() >= 2000) {
                            Serial.println("Max points limit reached (2000)");
                            break;
                        }
                    }
                }
            }
        }
        Serial.printf("Fetched %d points\n", values.size());
    } else {
        Serial.printf("[HTTP] History failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
    return values;
}

void drawGraph(String entity_id, String name, int hours) {
    currentGraphEntityId = entity_id;
    currentGraphName = name;
    currentGraphDuration = hours;
    Serial.println("Drawing graph for: " + name);

    showBusyIndicator();
    std::vector<float> data = fetchHistory(entity_id, hours);
    hideBusyIndicator();
    
    currentPage = GRAPH_PAGE;
    
    // Aggiorna l'header sulla canvas globale prima di disegnare il grafico
    drawHeader();

    // Definisci l'area di disegno (colonna destra)
    const int header_height = 80;
    const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
    const int col2_width = M5EPD_PANEL_W - col2_start_x;
    const int area_h = M5EPD_PANEL_H - header_height;

    // Pulisci solo l'area del grafico
    canvas.fillRect(col2_start_x, header_height, col2_width, area_h, WHITE);

    canvas.setTextSize(1);
    canvas.setFreeFont(&FreeSansBold18pt7b);
    canvas.setTextColor(BLACK);
    canvas.setTextDatum(TC_DATUM);
    // Disegna il titolo centrato nell'area del grafico
    String suffix = " (" + String(hours) + "h)";
    String displayName = name;
    int maxW = col2_width - 10;

    if (canvas.textWidth(displayName + suffix) > maxW) {
        String ellipsis = "...";
        int suffixW = canvas.textWidth(suffix);
        int ellipsisW = canvas.textWidth(ellipsis);
        while (displayName.length() > 0 && canvas.textWidth(displayName) + ellipsisW + suffixW > maxW) {
            displayName.remove(displayName.length() - 1);
        }
        displayName += ellipsis;
    }
    canvas.drawString(displayName + suffix, col2_start_x + col2_width / 2, header_height + 30);
    
    if (data.empty()) {
        canvas.drawString("Nessun dato", col2_start_x + col2_width / 2, header_height + area_h / 2);
    } else {
        int margin = 70;
        int w = col2_width - 2 * margin;
        int h = area_h - 120; // Altezza grafico
        int x0 = col2_start_x + margin;
        int y0 = header_height + area_h - 50; // Posizione asse X (dal basso dell'area)
        
        float minVal = data[0];
        float maxVal = data[0];
        for (float v : data) {
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
        }
        
        float range = maxVal - minVal;
        if (range == 0) range = 1;
        
        // Assi
        canvas.drawLine(x0, y0, x0 + w, y0, 2, BLACK);
        canvas.drawLine(x0, y0, x0, y0 - h, 2, BLACK);
        
        // Etichette Min/Max
        canvas.setFreeFont(&FreeSans12pt7b);
        canvas.setTextDatum(MR_DATUM);
        canvas.drawString(String(minVal, 1), x0 - 5, y0);
        canvas.drawString(String(maxVal, 1), x0 - 5, y0 - h);
        
        // Plot
        int count = data.size();
        if (count > 1) {
            for (int i = 0; i < count - 1; i++) {
                int x1 = x0 + (i * w) / (count - 1);
                int y1 = y0 - ((data[i] - minVal) * h) / range;
                int x2 = x0 + ((i + 1) * w) / (count - 1);
                int y2 = y0 - ((data[i+1] - minVal) * h) / range;
                canvas.drawLine(x1, y1, x2, y2, 2, BLACK);
            }
        }
    }
    canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}

void loop() {
  // Gestione richieste Web
  server.handleClient();

  // Polling periodico per aggiornamenti da Home Assistant (es. cambio pagina)
  static unsigned long lastUpdate = 0;

  if (millis() - lastUpdate > UPDATE_INTERVAL) {
      lastUpdate = millis();
      // Non aggiornare se siamo in pagine speciali (controllo, grafico, musica, ecc.)
      if (WiFi.status() == WL_CONNECTED && currentPage != GRAPH_PAGE && currentPage != LIGHT_CONTROL_PAGE && currentPage != CLOCK_PAGE && currentPage != CALENDAR_PAGE && currentPage != MEDIA_CONTROL_PAGE && currentPage != SCRIPT_PAGE) {
          // Se updateStates ritorna true, significa che la pagina è cambiata
          bool pageChanged = updateStates(true, true);
          if (pageChanged) {
              drawHeader();
              drawButtons();
              if (currentPage == 0) drawSensors(); 
              else if (currentPage == CLOCK_PAGE) drawAnalogClock();
              else {
                  if (currentPage == 1) {
                      wifi_mode_t mode = WiFi.getMode();
                      gridButtons[0].state = ((mode == WIFI_AP) || (mode == WIFI_AP_STA)) ? "on" : "off";
                  }
                  drawGridButtons();
              }
              canvas.pushCanvas(0, 0, UPDATE_MODE_GC16); // Refresh completo per cambio pagina
              lastActivityTime = millis(); // Mantieni sveglio se c'è attività
          } else if (currentPage == 0) {
              // Aggiornamento periodico per la pagina sensori
              drawSensors();
              canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
          }
      }
  }

  // Aggiornamento orologio
  static unsigned long lastClockUpdate = 0;
  unsigned long clockInterval = (currentPage == CLOCK_PAGE) ? 1000 : 10000;

  if (millis() - lastClockUpdate > clockInterval) {
      lastClockUpdate = millis();
      if (currentPage == CLOCK_PAGE) {
          drawHeader();
          drawAnalogClock();
          canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
      } else if (currentPage != GRAPH_PAGE && currentPage != LIGHT_CONTROL_PAGE && currentPage != CALENDAR_PAGE && currentPage != MEDIA_CONTROL_PAGE && currentPage != SCRIPT_PAGE) {
          M5EPD_Canvas headerCanvas(&M5.EPD);
          headerCanvas.createCanvas(M5EPD_PANEL_W, 80);
          drawHeader(&headerCanvas);
          headerCanvas.pushCanvas(0, 0, UPDATE_MODE_DU);
      }
  }

  // Controllo Deep Sleep
  // DISABILITATO per mantenere il server web attivo
  // if (millis() - lastActivityTime > SLEEP_TIMEOUT) {
  //     enterDeepSleep();
  // }

  // Gestione robusta della connessione
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      reconnectMqtt();
    }
    mqttClient.loop();
  } else {
    // Se il WiFi non è connesso, non fare nulla che richieda la rete.
  }

  M5.update(); // Aggiorna lo stato dei pulsanti fisici e del tocco

  // Controlla se il pulsante fisico centrale è stato premuto
  if (M5.BtnP.wasPressed()) {
    Serial.println("Pulsante centrale premuto! Invio MQTT...");
    if (mqttClient.connected()) {
        mqttClient.publish("m5paper/keypressed", "P");
    }
  }

  // Controlla se il pulsante fisico sinistro è stato premuto
  if (M5.BtnL.wasPressed()) {
    Serial.println("Pulsante sinistro premuto! Invio MQTT...");
    if (mqttClient.connected()) {
        mqttClient.publish("m5paper/keypressed", "L");
    }
  }

  // Controlla se il pulsante fisico destro è stato premuto
  if (M5.BtnR.wasPressed()) {
    Serial.println("Pulsante destro premuto! Invio MQTT...");
    if (mqttClient.connected()) {
        mqttClient.publish("m5paper/keypressed", "R");
    }
  }

  if (M5.TP.available()) {
    M5.TP.update();
    if (!M5.TP.isFingerUp()) {
      lastActivityTime = millis(); // Resetta il timer di sonno al tocco
      tp_finger_t finger = M5.TP.readFinger(0);

      // Gestione tocco nella schermata di avviso (HA non configurato)
      if (homeAssistantAddress == "") {
          int btnW = 220;
          int btnH = 50;
          int btnX = (M5EPD_PANEL_W - btnW) / 2;
          int btnY = M5EPD_PANEL_H - 80;
          
          if (finger.x >= btnX && finger.x <= btnX + btnW &&
              finger.y >= btnY && finger.y <= btnY + btnH) {
              
              // Feedback visivo
              M5EPD_Canvas btnCanvas(&M5.EPD);
              btnCanvas.createCanvas(btnW, btnH);
              btnCanvas.fillCanvas(WHITE);
              btnCanvas.drawRect(0, 0, btnW, btnH, BLACK);
              btnCanvas.setTextColor(BLACK);
              btnCanvas.setFreeFont(&FreeSans12pt7b);
              btnCanvas.setTextDatum(MC_DATUM);
              btnCanvas.drawString("Resetting...", btnW/2, btnH/2);
              btnCanvas.pushCanvas(btnX, btnY, UPDATE_MODE_DU);
              
              delay(500);
              handleFactoryReset(); // Riutilizza la funzione di reset
          }
          // Ignora altri tocchi in questa schermata
          while(!M5.TP.isFingerUp()) { M5.TP.update(); }
          return;
      }
      
      // Controllo tocco Header (Batteria per Dark Mode)
      if (finger.y < 80) {
          // Area approssimativa della batteria (y=20, h=28, w=66)
          if (finger.y >= 10 && finger.y <= 60 && finger.x >= headerBatteryX - 10 && finger.x <= headerBatteryX + 80) {
              Serial.println("Tapped Battery Icon: Toggling Dark Mode");
              isDarkMode = !isDarkMode;
              preferences.begin("epaper", false);
              preferences.putBool("darkMode", isDarkMode);
              preferences.end();
              drawFullUI(false);
              while(!M5.TP.isFingerUp()) { M5.TP.update(); }
              return;
          }
          // Controllo tocco Data (Lato destro) -> Vai al Calendario
          else if (finger.x > M5EPD_PANEL_W - 400) {
              Serial.println("Tapped Date: Showing Calendar");
              currentPage = CALENDAR_PAGE;
              drawHeader();
              drawButtons();
              drawCalendar();
              canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
              while(!M5.TP.isFingerUp()) { M5.TP.update(); }
              return;
          }
          // Controllo tocco Orologio (Lato sinistro, prima della batteria) -> Vai all'Orologio Analogico
          else if (finger.x < headerBatteryX - 10) {
              Serial.println("Tapped Clock: Showing Analog Clock");
              currentPage = CLOCK_PAGE;
              // Ripristina i pulsanti di default
              for (int k = 0; k < NUM_GRID_BUTTONS; k++) {
                  gridButtons[k] = defaultGridButtons[k];
              }
              updateStates(true, false);
              drawHeader();
              drawButtons();
              drawAnalogClock();
              canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
              while(!M5.TP.isFingerUp()) { M5.TP.update(); }
              return;
          }
      }

      // Controlla se il tocco è all'interno di uno dei pulsanti della colonna sinistra
      for (int i = 0; i < NUM_DEVICES; i++) {
        if (finger.x >= devices[i].x && finger.x <= (devices[i].x + devices[i].w) &&
            finger.y >= devices[i].y && finger.y <= (devices[i].y + devices[i].h)) {
          
          Serial.print("Tapped ");
          Serial.println(devices[i].name);

          if (i == 0) { // SWITCH
              currentPage = 3;
              setPageInputNumber(1);
              loadGroupSwitches();
              drawHeader();
              drawButtons();
              drawGridButtons();
              canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
          } else if (i == 1) { // LUCI
              currentPage = 2;
              setPageInputNumber(2);
              loadGroupLights();
              drawHeader();
              drawButtons();
              drawGridButtons();
              canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
          } else if (i == 2) { // HOME
              currentPage = 1;
              setPageInputNumber(3);
              // Pulisci e configura solo i pulsanti per la pagina HOME
              for (int k = 0; k < NUM_GRID_BUTTONS; k++) {
                  gridButtons[k].entity_id = "";
                  gridButtons[k].name = "";
                  gridButtons[k].type = "";
              }
              gridButtons[0].entity_id = "";
              gridButtons[0].name = "Hotspot";
              gridButtons[0].type = "hotspot";
              wifi_mode_t mode = WiFi.getMode();
              gridButtons[0].state = ((mode == WIFI_AP) || (mode == WIFI_AP_STA)) ? "on" : "off";
              gridButtons[1].name = "Script";
              gridButtons[1].type = "script_page";

              drawHeader();
              drawButtons();
              drawGridButtons();
              canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
          } else if (i == 3) { // SENSORI
              currentPage = 0;
              setPageInputNumber(4);
              loadGroupSensors();
              drawHeader();
              drawButtons();
              drawSensors();
              canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
          } else {
              toggleDevice(devices[i].entity_id, devices[i].type);
              delay(250); // Attendi solo se chiami Home Assistant
              updateStates(true, false); // Aggiorna solo i dispositivi HA
              drawHeader();
              drawButtons();
              canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
          }
          break; // Exit loop once a button is found
        }
      }

      // Controlla se il tocco è all'interno di uno dei pulsanti della griglia
      if (currentPage == 1 || currentPage == 2 || currentPage == 3 || currentPage == SCRIPT_PAGE) {
       for (int i = 0; i < NUM_GRID_BUTTONS; i++) {
        if (finger.x >= gridButtons[i].x && finger.x <= (gridButtons[i].x + gridButtons[i].w) &&
            finger.y >= gridButtons[i].y && finger.y <= (gridButtons[i].y + gridButtons[i].h)) {
          
          Serial.print("Tapped Grid Button ");
          Serial.println(gridButtons[i].name);

          if (String(gridButtons[i].type) == "light") {
              // Apri pagina controllo luce
              selectedLightEntity = gridButtons[i].entity_id;
              selectedLightName = gridButtons[i].name;
              currentPage = LIGHT_CONTROL_PAGE;
              fetchLightDetails(selectedLightEntity);
              drawHeader();
              drawLightControlPage();
              break;
          } else if (String(gridButtons[i].type) == "media_player") {
              // Apri pagina controllo media player
              selectedMediaEntity = gridButtons[i].entity_id;
              selectedMediaName = gridButtons[i].name;
              currentPage = MEDIA_CONTROL_PAGE;
              fetchMediaDetails(selectedMediaEntity);
              drawHeader();
              drawMediaControlPage();
              break;
          } else if (String(gridButtons[i].type) == "hotspot") {
               // Gestione Toggle Hotspot
               wifi_mode_t currentMode = WiFi.getMode();
               if (currentMode == WIFI_AP || currentMode == WIFI_AP_STA) {
                   if (currentMode == WIFI_AP_STA) WiFi.mode(WIFI_STA);
                   else WiFi.mode(WIFI_OFF);
                   gridButtons[i].state = "off";
               } else {
                   if (currentMode == WIFI_STA) WiFi.mode(WIFI_AP_STA);
                   else WiFi.mode(WIFI_AP);
                   WiFi.softAP("M5Paper_Hotspot");
                   gridButtons[i].state = "on";
               }
               drawGridButtons();
               canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
               break;
          } else if (String(gridButtons[i].type) == "script_page") {
               // Vai alla pagina Script
               currentPage = SCRIPT_PAGE;
               loadGroupScripts();
               drawHeader();
               drawButtons();
               drawGridButtons();
               canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
               break;
          } else if (String(gridButtons[i].type) != "sensor") {
            toggleDevice(gridButtons[i].entity_id, gridButtons[i].type);
            delay(250);
            updateStates(true, false);
            drawHeader();
            drawGridButtons(); // Ridisegna solo la griglia
            canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
            break;
          } else {
            // Se è un sensore, apri il grafico
            drawHeader();
            drawGraph(gridButtons[i].entity_id, gridButtons[i].name, 24);
            break;
          }
        }
      }
      } else if (currentPage == 0) {
          // Gestione paginazione sensori
          int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
          const int header_height = 80;
          const int start_y = header_height + 20;
          const int col2_width = M5EPD_PANEL_W - col2_start_x;
          const int cols = 3;
          const int rows = 4;
          const int btn_w = col2_width / cols;
          const int btn_h = (M5EPD_PANEL_H - start_y) / rows;

          if (finger.x >= col2_start_x && finger.y >= start_y) {
              int col = (finger.x - col2_start_x) / btn_w;
              int row = (finger.y - start_y) / btn_h;

              if (col >= 0 && col < cols && row >= 0 && row < rows) {
                  int index = row * cols + col;

                  if (index == 11) {
                      // Pulsante NEXT
                      int sensors_count = 0;
                      for (const auto& s : sensors) if (s.entity_id != "sensor.time" && s.entity_id != "sensor.oggi" && s.entity_id != "input_number.epaper_pagina") sensors_count++;
                      int totalPages = (sensors_count + SENSORS_PER_PAGE - 1) / SENSORS_PER_PAGE;

                      if (totalPages > 1) {
                          sensorPage++;
                          if (sensorPage >= totalPages) sensorPage = 0;
                          drawSensors();
                          canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
                      }
                  } else {
                      // Click su un sensore
                      std::vector<int> displayableIndices;
                      for (size_t i = 0; i < sensors.size(); i++) {
                          if (sensors[i].entity_id != "sensor.time" && sensors[i].entity_id != "sensor.oggi" && sensors[i].entity_id != "input_number.epaper_pagina") {
                              displayableIndices.push_back(i);
                          }
                      }
                      
                      int sensorIdx = sensorPage * SENSORS_PER_PAGE + index;
                      if (sensorIdx < displayableIndices.size()) {
                          int realIdx = displayableIndices[sensorIdx];
                          drawGraph(sensors[realIdx].entity_id, sensors[realIdx].name, 24);
                      }
                  }
              }
          }
      } else if (currentPage == GRAPH_PAGE) {
          // Gestione cambio intervallo (tocco sul titolo del grafico)
          const int header_height = 80;
          const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
          
          // Se tocco nella zona del titolo del grafico (parte alta della colonna destra)
          if (finger.x > col2_start_x && finger.y > header_height && finger.y < header_height + 80) {
              int nextDuration = 24;
              if (currentGraphDuration == 24) nextDuration = 6;
              else if (currentGraphDuration == 6) nextDuration = 12;
              else if (currentGraphDuration == 12) nextDuration = 24;
              drawGraph(currentGraphEntityId, currentGraphName, nextDuration);
          }
      }
      else if (currentPage == CALENDAR_PAGE) {
          const int header_height = 80;
          const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
          const int col2_width = M5EPD_PANEL_W - col2_start_x;
          int titleY = header_height + 30;

          // Left arrow
          if (finger.x > col2_start_x && finger.x < col2_start_x + 80 && finger.y > titleY - 20 && finger.y < titleY + 20) {
              calendarMonthOffset--;
              drawCalendar();
              canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
          }
          // Right arrow
          else if (finger.x > col2_start_x + col2_width - 80 && finger.x < col2_start_x + col2_width && finger.y > titleY - 20 && finger.y < titleY + 20) {
              calendarMonthOffset++;
              drawCalendar();
              canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
          }
      }
      else if (currentPage == LIGHT_CONTROL_PAGE) {
          const int header_height = 80;
          const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
          const int col2_width = M5EPD_PANEL_W - col2_start_x;
          
          // Coordinate Switch
          int swW = 200;
          int swH = 80;
          int swX = col2_start_x + (col2_width - swW) / 2;
          int swY = header_height + 100;
          
          // Coordinate Slider
          int slX = col2_start_x + 40;
          int slY = swY + swH + 80;
          int slW = col2_width - 80;
          int slH = 40;

          if (finger.x >= swX && finger.x <= swX + swW && finger.y >= swY && finger.y <= swY + swH) {
              // Tocco su Switch
              toggleDevice(selectedLightEntity, "light");
              // Aggiorna stato locale e ridisegna
              selectedLightState = (selectedLightState == "on") ? "off" : "on";
              if (selectedLightState == "on" && selectedLightBrightness == 0) selectedLightBrightness = 255;
              drawLightControlPage();
          } else if (finger.x >= slX && finger.x <= slX + slW && finger.y >= slY - 20 && finger.y <= slY + slH + 20) {
              // Tocco su Slider
              int newBri = map(finger.x, slX, slX + slW, 0, 255);
              if (newBri < 0) newBri = 0;
              if (newBri > 255) newBri = 255;
              
              selectedLightBrightness = newBri;
              if (newBri > 0) selectedLightState = "on";
              
              setLightBrightness(selectedLightEntity, selectedLightBrightness);
              drawLightControlPage();
          }
      }
      else if (currentPage == MEDIA_CONTROL_PAGE) {
          const int header_height = 80;
          const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
          const int col2_width = M5EPD_PANEL_W - col2_start_x;
          
          // Coordinate Pulsanti di controllo
          int btnY = header_height + 150;
          int btnW = 100;
          int btnH = 80;
          int btnSpacing = (col2_width - (3 * btnW)) / 4;
          int prevX = col2_start_x + btnSpacing;
          int playX = prevX + btnW + btnSpacing;
          int nextX = playX + btnW + btnSpacing;

          // Coordinate Slider Volume
          int slX = col2_start_x + 40;
          int slY = btnY + btnH + 80;
          int slW = col2_width - 80;
          int slH = 40;

          if (finger.y >= btnY && finger.y <= btnY + btnH) {
              if (finger.x >= prevX && finger.x <= prevX + btnW) {
                  controlMediaPlayer(selectedMediaEntity, "media_previous_track");
              } else if (finger.x >= playX && finger.x <= playX + btnW) {
                  controlMediaPlayer(selectedMediaEntity, "media_play_pause");
              } else if (finger.x >= nextX && finger.x <= nextX + btnW) {
                  controlMediaPlayer(selectedMediaEntity, "media_next_track");
              }
              delay(250); // Attendi che HA processi il comando
              fetchMediaDetails(selectedMediaEntity);
              drawMediaControlPage();

          } else if (finger.x >= slX && finger.x <= slX + slW && finger.y >= slY - 20 && finger.y <= slY + slH + 20) {
              // Tocco su Slider
              float newVol = (float)(finger.x - slX) / (float)slW;
              if (newVol < 0.0) newVol = 0.0;
              if (newVol > 1.0) newVol = 1.0;
              
              selectedMediaVolume = newVol;
              
              setMediaVolume(selectedMediaEntity, selectedMediaVolume);
              // Ridisegna solo la parte dello slider per reattività
              drawMediaControlPage(); // Per semplicità ridisegnamo tutto
          }
      }
      while(!M5.TP.isFingerUp()) { M5.TP.update(); } // Attendi il rilascio del dito
    }
  }
}

void fetchMediaDetails(String entity_id) {
    if (homeAssistantAddress == "") return;
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    WiFiClient client;
    String url = homeAssistantAddress + "/api/states/" + entity_id;
    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + homeAssistantToken);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        // Simple parsing for state and volume_level
        int stateIdx = payload.indexOf("\"state\": \"");
        if (stateIdx != -1) {
            int valStart = stateIdx + 10;
            int valEnd = payload.indexOf("\"", valStart);
            selectedMediaState = payload.substring(valStart, valEnd);
        }
        int volIdx = payload.indexOf("\"volume_level\": ");
        if (volIdx != -1) {
            int valStart = volIdx + 15;
            int valEnd = payload.indexOf(",", valStart);
            if (valEnd == -1) valEnd = payload.indexOf("}", valStart);
            String volStr = payload.substring(valStart, valEnd);
            selectedMediaVolume = volStr.toFloat();
        } else {
            selectedMediaVolume = 0.0;
        }
    }
    http.end();
}

void setMediaVolume(String entity_id, float volume) {
    if (homeAssistantAddress == "") return;
    WiFiClient client;
    HTTPClient http;
    String apiUrl = homeAssistantAddress + "/api/services/media_player/volume_set";
    http.begin(client, apiUrl);
    http.addHeader("Authorization", "Bearer " + homeAssistantToken);
    http.addHeader("Content-Type", "application/json");
    String jsonPayload = "{\"entity_id\":\"" + entity_id + "\", \"volume_level\":" + String(volume, 2) + "}";
    http.POST(jsonPayload);
    http.end();
}

void controlMediaPlayer(String entity_id, String action) {
    if (homeAssistantAddress == "") return;
    WiFiClient client;
    HTTPClient http;
    String apiUrl = homeAssistantAddress + "/api/services/media_player/" + action;
    http.begin(client, apiUrl);
    http.addHeader("Authorization", "Bearer " + homeAssistantToken);
    http.addHeader("Content-Type", "application/json");
    String jsonPayload = "{\"entity_id\":\"" + entity_id + "\"}";
    http.POST(jsonPayload);
    http.end();
}

void drawMediaControlPage() {
    const int header_height = 80;
    const int col2_start_x = (M5EPD_PANEL_W * 0.3) - 22;
    const int col2_width = M5EPD_PANEL_W - col2_start_x;
    
    // Pulisci area
    canvas.fillRect(col2_start_x, header_height, col2_width, M5EPD_PANEL_H - header_height, WHITE);
    
    // Titolo (Nome Media Player)
    canvas.setFreeFont(&FreeSansBold18pt7b);
    canvas.setTextColor(BLACK);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(selectedMediaName, col2_start_x + col2_width / 2, header_height + 50);
    
    // Pulsanti di controllo
    int btnY = header_height + 150;
    int btnW = 100;
    int btnH = 80;
    int btnSpacing = (col2_width - (3 * btnW)) / 4;

    int prevX = col2_start_x + btnSpacing;
    int playX = prevX + btnW + btnSpacing;
    int nextX = playX + btnW + btnSpacing;

    canvas.setTextDatum(MC_DATUM);
    canvas.setFreeFont(&FreeSansBold24pt7b);

    // Prev button
    canvas.drawRect(prevX, btnY, btnW, btnH, BLACK);
    canvas.drawString("<<", prevX + btnW / 2, btnY + btnH / 2);

    // Play/Pause button
    String playIcon = (selectedMediaState == "playing") ? "||" : ">";
    canvas.fillRect(playX, btnY, btnW, btnH, BLACK);
    canvas.setTextColor(WHITE);
    canvas.drawString(playIcon, playX + btnW / 2, btnY + btnH / 2);
    canvas.setTextColor(BLACK);

    // Next button
    canvas.drawRect(nextX, btnY, btnW, btnH, BLACK);
    canvas.drawString(">>", nextX + btnW / 2, btnY + btnH / 2);
    
    // Slider Volume
    int slX = col2_start_x + 40;
    int slY = btnY + btnH + 80;
    int slW = col2_width - 80;
    int slH = 40;
    
    canvas.drawRect(slX, slY, slW, slH, BLACK);
    int fillW = selectedMediaVolume * slW;
    if (fillW > 0) canvas.fillRect(slX, slY, fillW, slH, BLACK);
    
    canvas.setFreeFont(&FreeSans12pt7b);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString("Volume: " + String((int)(selectedMediaVolume * 100)) + "%", col2_start_x + col2_width / 2, slY - 30);
    
    canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}
