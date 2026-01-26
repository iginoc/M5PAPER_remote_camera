# M5Paper Home Assistant Dashboard

Questo progetto trasforma un dispositivo M5Paper in un pannello di controllo touchscreen per Home Assistant, sfruttando il suo display e-ink a basso consumo. L'interfaccia è progettata per essere reattiva, personalizzabile e per massimizzare la durata della batteria attraverso l'uso del deep sleep.

![placeholder_screenshot](https://via.placeholder.com/540x960.png?text=UI+Screenshot)

## Caratteristiche

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
- **Menu Laterale (a sinistra)**: Contiene 4 pulsanti per navigare tra le pagine principali: `SENSORI`, `HOME`, `LUCI`, `SWITCH`.
- **Area Contenuti (a destra)**: Mostra il contenuto della pagina selezionata.

### Navigazione

- **Menu Laterale**: Tocca uno dei quattro pulsanti per cambiare pagina.
- **Tocco sull'Ora**: Apre la pagina dell'**orologio analogico**.
- **Tocco sulla Data**: Apre la pagina del **calendario**.
- **Tocco sulla Batteria**: Attiva/disattiva la **Dark Mode**.

### Funzionalità delle Pagine

- **Pagina Sensori**: Mostra una griglia di sensori dal `gruppo_sensori`. Se i sensori sono più di 11, appare un pulsante "NEXT" per sfogliare le pagine. Toccando un sensore si apre il suo grafico storico.
- **Pagina Home**: Pagina personalizzabile. Di default, contiene un pulsante per attivare/disattivare l'hotspot WiFi e un link alla pagina "Musica".
- **Pagina Luci/Switch**: Mostra una griglia di luci o switch presi dai rispettivi gruppi. Un tocco su un pulsante ne commuta lo stato. Un tocco prolungato (o un secondo tocco) su una luce apre la pagina di controllo dettagliato.
- **Pagina Musica**: Mostra una griglia di `script` di Home Assistant, utile per avviare playlist o automazioni musicali.
- **Pagina Controllo Luce**: Permette di accendere/spegnere la luce e di regolarne la luminosità tramite uno slider.
- **Pagina Grafico**: Mostra l'andamento di un sensore. Tocca il titolo per cambiare l'intervallo di tempo visualizzato (24h -> 6h -> 12h).

### Deep Sleep

Dopo 10 minuti di inattività, il dispositivo entra in modalità deep sleep per risparmiare energia. Si risveglia automaticamente dopo 10 minuti per aggiornare i dati, oppure immediatamente se si tocca lo schermo.

## Screenshot

*Inserisci qui gli screenshot della tua interfaccia.*

!Schermata Sensori
!Schermata Luci
!Schermata Grafico

## Licenza

Questo progetto è rilasciato sotto la licenza MIT. Vedi il file `LICENSE` per maggiori dettagli.
