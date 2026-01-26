[English](#english) | [Italiano](#italiano)

# M5Paper Home Assistant Dashboard

This project transforms an M5Paper device into a touchscreen control panel for Home Assistant, leveraging its low-power e-ink display. The interface is designed to be responsive, customizable, and to maximize battery life through the use of deep sleep.

<p align="center"><img src="screenshots/output.gif" width="600" alt="Home Screen" /></p>

## <a name="english"></a>Features

- **Multi-Page Interface**: Navigate through different screens to control lights, switches, scenes, and view sensors.
- **Low Power**: Utilizes the ESP32's deep sleep and the e-ink display's persistence for long battery life.
- **Web Configuration**: Easily configure WiFi and Home Assistant credentials via a web interface, without needing to modify the code.
- **Fallback Hotspot**: If the WiFi connection fails or is not configured, the device starts a hotspot for direct access.
- **Dynamic Control**:
  - **Lights and Switches**: Control groups of devices displayed in a grid.
  - **Detailed Control**: Dedicated page to adjust light brightness and control media players (volume, play/pause, etc.).
  - **Sensors**: View sensor data with pagination.
  - **History Graphs**: Tap a sensor to view its history over the last 6, 12, or 24 hours.
- **Special Views**: Includes an analog clock page and a monthly calendar page.
- **Dark Mode**: Switch between a light and dark display mode with a tap.
- **MQTT Synchronization**: Receives screen update commands via MQTT.
- **Remote Screenshot**: Get a screenshot of the current interface via the web interface.

## Required Hardware

- **M5Stack M5Paper**

## Home Assistant Configuration

For the panel to work correctly, you need to configure some entities in Home Assistant. Add the following to your `configuration.yaml` file (or separate files if you use a different organization).

```yaml
# configuration.yaml

input_number:
  epaper_pagina:
    name: Epaper Page
    initial: 3 # HOME page on startup
    min: 1
    max: 4
    step: 1
    mode: box

sensor:
  # Sensor for the current time
  - platform: time_date
    display_options:
      - 'time'

  # Sensor for the current date in "Mon 01 Jan" format
  - platform: template
    sensors:
      oggi:
        friendly_name: "Today"
        value_template: >
          {% set months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"] %}
          {% set weekdays = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"] %}
          {{ weekdays[now().weekday()] }} {{ now().day }} {{ months[now().month-1] }}

# Groups to populate the pages
group:
  gruppo_luci:
    name: Epaper Lights Group
    entities:
      - light.luce_sala
      - light.luce_cucina
      # Add more lights here

  gruppo_switch:
    name: Epaper Switches Group
    entities:
      - switch.presa_smart
      - switch.altro_switch
      # Add more switches here

  gruppo_sensori:
    name: Epaper Sensors Group
    entities:
      - sensor.temperatura_esterna
      - sensor.livello_batteria_auto
      # Add more sensors here
```

## Installation and Setup

1.  **Flash Firmware**: Compile and upload the project to your M5Paper using PlatformIO.
2.  **First-Time Setup**:
    - On the first boot, or if credentials are invalid, the device will show a configuration screen.
    - Connect to the **"M5Paper_Hotspot"** WiFi hotspot.
    - Open a browser and navigate to `http://192.168.4.1`.
    - Enter your WiFi credentials, your Home Assistant URL (e.g., `http://192.168.1.100:8123`), and a **Long-Lived Access Token**.
    - Save the settings. The device will reboot and connect to your network.
3.  **Subsequent Configuration**:
    - When the device is connected to your WiFi network, you can access the web interface by visiting its IP address to change settings, take a screenshot, or reset the device.
    - The IP address is shown in the header if the connection to Home Assistant fails.

## Usage

### User Interface

The interface is divided into three main sections:

- **Header (top)**: Shows the time, date, and battery icon.
- **Side Menu (left)**: Contains 4 buttons to navigate between the main pages: `SENSORS`, `HOME`, `LIGHTS`, `SWITCH`.
- **Content Area (right)**: Shows the content of the selected page.

### Navigation

- **Side Menu**: Tap one of the four buttons to change the page.
- **Tap on Time**: Opens the **analog clock** page.
- **Tap on Date**: Opens the **calendar** page.
- **Tap on Battery**: Toggles **Dark Mode**.

### Page Features

- **Sensors Page**: Displays a grid of sensors from the `gruppo_sensori`. If there are more than 11 sensors, a "NEXT" button appears to browse pages. Tapping a sensor opens its history graph.
- **Home Page**: Customizable page. By default, it contains a button to toggle the WiFi hotspot and a link to the "Script" page.
- **Lights/Switches Page**: Displays a grid of lights or switches from their respective groups. A tap on a button toggles its state. A long press (or a second tap) on a light opens the detailed control page.
- **Script Page**: Displays a grid of Home Assistant `script` entities, useful for starting playlists or automations.
- **Light Control Page**: Allows turning the light on/off and adjusting its brightness via a slider.
- **Graph Page**: Shows the history of a sensor. Tap the title to change the displayed time range (24h -> 6h -> 12h).

### Deep Sleep

After 10 minutes of inactivity, the device enters deep sleep mode to save power. It wakes up automatically after 10 minutes to update data, or immediately if the screen is touched.

## Screenshot

<p float="left" align="center">
  <img src="screenshots/sensors.png" width="260" alt="Sensors Screen" />
  <img src="screenshots/lights.png" width="260" alt="Lights Screen" />
  <img src="screenshots/graph.png" width="260" alt="Graph Screen" />
</p>

## License

This project is released under the MIT license. See the `LICENSE` file for more details.

---

English | Italiano

# M5Paper Home Assistant Dashboard

Questo progetto trasforma un dispositivo M5Paper in un pannello di controllo touchscreen per Home Assistant, sfruttando il suo display e-ink a basso consumo. L'interfaccia è progettata per essere reattiva, personalizzabile e per massimizzare la durata della batteria attraverso l'uso del deep sleep.

<p align="center"><img src="screenshots/output.gif" width="600" alt="Home Screen" /></p>

## <a name="italiano"></a>Caratteristiche

- **Interfaccia Multi-Pagina**: Naviga tra diverse schermate per controllare luci, interruttori, scene e visualizzare sensori.
- **Basso Consumo**: Sfrutta il deep sleep dell'ESP32 e la persistenza del display e-ink per una lunga durata della batteria.
- **Configurazione Web**: Configura facilmente le credenziali WiFi e Home Assistant tramite un'interfaccia web, senza dover modificare il codice.
- **Hotspot di Fallback**: Se la connessione WiFi fallisce o non è configurata, il dispositivo avvia un hotspot per l'accesso diretto.
- **Controllo Dinamico**:
  - **Luci e Switch**: Controlla gruppi di dispositivi visualizzati in una griglia.
  - **Controllo Dettagliato**: Pagina dedicata per regolare la luminosità delle luci e controllare i media player (volume, play/pausa, etc.).
  - **Sensori**: Visualizza i dati dei sensori con paginazione.
  - **Grafici Storici**: Tocca un sensore per visualizzare il suo andamento storico nelle ultime 6, 12 o 24 ore.
- **Viste Speciali**: Include una pagina con orologio analogico e una con calendario mensile.
- **Dark Mode**: Passa da una modalità di visualizzazione chiara a una scura con un tocco.
- **Sincronizzazione MQTT**: Riceve comandi per l'aggiornamento dello schermo tramite MQTT.
- **Screenshot Remoto**: Ottieni uno screenshot dell'interfaccia corrente tramite l'interfaccia web.

## Hardware Richiesto

- **M5Stack M5Paper**

## Configurazione di Home Assistant

Per il corretto funzionamento del pannello, è necessario configurare alcune entità in Home Assistant. Aggiungi quanto segue al tuo file `configuration.yaml` (o file separati se usi un'organizzazione diversa).

```yaml
# configuration.yaml

input_number:
  epaper_pagina:
    name: Epaper Pagina
    initial: 3 # Pagina HOME all'avvio
    min: 1
    max: 4
    step: 1
    mode: box

sensor:
  # Sensore per l'ora corrente
  - platform: time_date
    display_options:
      - 'time'

  # Sensore per la data corrente in formato "Lun 01 Gen"
  - platform: template
    sensors:
      oggi:
        friendly_name: "Oggi"
        value_template: >
          {% set months = ["Gen", "Feb", "Mar", "Apr", "Mag", "Giu", "Lug", "Ago", "Set", "Ott", "Nov", "Dic"] %}
          {% set weekdays = ["Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"] %}
          {{ weekdays[now().weekday()] }} {{ now().day }} {{ months[now().month-1] }}

# Gruppi per popolare le pagine
group:
  gruppo_luci:
    name: Gruppo Luci Epaper
    entities:
      - light.luce_sala
      - light.luce_cucina
      # Aggiungi altre luci qui

  gruppo_switch:
    name: Gruppo Switch Epaper
    entities:
      - switch.presa_smart
      - switch.altro_switch
      # Aggiungi altri switch qui

  gruppo_sensori:
    name: Gruppo Sensori Epaper
    entities:
      - sensor.temperatura_esterna
      - sensor.livello_batteria_auto
      # Aggiungi altri sensori qui



## Installazione e Configurazione

1.  **Flash del Firmware**: Compila e carica il progetto sul tuo M5Paper usando PlatformIO.
2.  **Prima Configurazione**:
   - Al primo avvio, o se le credenziali non sono valide, il dispositivo mostrerà una schermata di configurazione.
   - Connettiti all'hotspot WiFi **"M5Paper\_Hotspot"**.
   - Apri un browser e naviga all'indirizzo `http://192.168.4.1`.
   - Inserisci le credenziali della tua rete WiFi, l'URL del tuo Home Assistant (es. `http://192.168.1.100:8123`) e un **Token di Accesso di Lunga Durata**.
   - Salva le impostazioni. Il dispositivo si riavvierà e si connetterà alla tua rete.
3.  **Configurazione Successiva**:
   - Quando il dispositivo è connesso alla tua rete WiFi, puoi accedere all'interfaccia web visitando il suo indirizzo IP per modificare le impostazioni, fare uno screenshot o resettare il dispositivo.
   - L'indirizzo IP viene mostrato nell'header se la connessione ad Home Assistant fallisce.

## Utilizzo

### Interfaccia Utente

L'interfaccia è divisa in tre sezioni principali:

- **Header (in alto)**: Mostra l'ora, la data e l'icona della batteria.
- **Menu Laterale (a sinistra)**: Contiene 4 pulsanti per navigare tra le pagine principali: `SENSORS`, `HOME`, `LIGHTS`, `SWITCH`.
- **Area Contenuti (a destra)**: Mostra il contenuto della pagina selezionata.

### Navigazione

- **Menu Laterale**: Tocca uno dei quattro pulsanti per cambiare pagina.
- **Tocco sull'Ora**: Apre la pagina dell'**orologio analogico**.
- **Tocco sulla Data**: Apre la pagina del **calendario**.
- **Tocco sulla Batteria**: Attiva/disattiva la **Dark Mode**.

### Funzionalità delle Pagine

- **Pagina Sensori**: Mostra una griglia di sensori dal `gruppo_sensori`. Se i sensori sono più di 11, appare un pulsante "NEXT" per sfogliare le pagine. Toccando un sensore si apre il suo grafico storico.
- **Pagina Home**: Pagina personalizzabile. Di default, contiene un pulsante per attivare/disattivare l'hotspot WiFi e un link alla pagina "Script".
- **Pagina Luci/Switch**: Mostra una griglia di luci o switch presi dai rispettivi gruppi. Un tocco su un pulsante ne commuta lo stato. Un tocco prolungato (o un secondo tocco) su una luce apre la pagina di controllo dettagliato.
- **Pagina Script**: Mostra una griglia di `script` di Home Assistant, utile per avviare playlist o automazioni.
- **Pagina Controllo Luce**: Permette di accendere/spegnere la luce e di regolarne la luminosità tramite uno slider.
- **Pagina Grafico**: Mostra l'andamento di un sensore. Tocca il titolo per cambiare l'intervallo di tempo visualizzato (24h -> 6h -> 12h).

### Deep Sleep

Dopo 10 minuti di inattività, il dispositivo entra in modalità deep sleep per risparmiare energia. Si risveglia automaticamente dopo 10 minuti per aggiornare i dati, oppure immediatamente se si tocca lo schermo.

## Screenshot

<p float="left" align="center">
  <img src="screenshots/sensors.png" width="260" alt="Schermata Sensori" />
  <img src="screenshots/lights.png" width="260" alt="Schermata Luci" />
  <img src="screenshots/graph.png" width="260" alt="Schermata Grafico" />
</p>

## Licenza

Questo progetto è rilasciato sotto la licenza MIT. Vedi il file `LICENSE` per maggiori dettagli.
