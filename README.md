# TrackSound: Akustisk Lokalisering & AI-Klassificering

TrackSound är ett kostnadseffektivt realtidssystem utvecklat för att identifiera och lokalisera ljudhändelser. Systemet använder ett distribuerat trådlöst nätverk av ljudsensorer för att beräkna ett ljuds fysiska ursprung, samtidigt som en integrerad AI-modell klassificerar vad som lät.

Projektet utvecklades som ett examensarbete vid NBI/Handelsakademin (2026).

## Funktioner

* **Trådlöst sensornätverk:** ESP32-baserade noder samlar in ljuddata i 16 000 Hz och kommunicerar blixtsnabbt via ESP-NOW.
* **Akustisk lokalisering (TDoA):** Mjukvaran beräknar små tidsskillnader i ljudets ankomsttid (Time Difference of Arrival) med hjälp av signalmatchning och optimeringsalgoritmer för multilateration.
* **AI-klassificering i realtid:** Ljudvågor konverteras till visuella Mel-spektrogram via Librosa, varpå ett Convolutional Neural Network (CNN) byggt i Keras/TensorFlow klassificerar händelsen (ex. Clap, Click, Noise).
* **Asynkron Backend:** En Python-server hanterar multithreading och signalbehandling (bandpassfilter, distansfilter) utan att blockera dataströmmen från hårdvaran.
* **Live-visualisering:** Ett React-baserat frontend tar emot processad data via WebSockets och ritar ut ljudhändelserna på en karta direkt i webbläsaren.

---

## Systemarkitektur

Systemet består av tre huvuddelar:

1. **Hårdvara (C++ / Arduino)**
   * **Sensor Nodes (`NodeBoard.ino`):** 4 stycken ESP32 utrustade med MAX9814-mikrofoner som kontinuerligt samplar ljud och sparar i en rullande buffert. Vid en händelse skickas data via ESP-NOW.
   * **Mainboard (`MainBoard.ino`):** En central ESP32 som dirigerar nätverket, upprätthåller mikrosekundssynkronisering av klockor och skickar ljudpaketen vidare till PC via USB.

2. **Backend (Python)**
   * **Inläsning (`serial_handler.py`):** Läser asynkront in datachunks från Mainboarden.
   * **Orkestrering (`event_processor.py` & `server.py`):** Hanterar händelseflödet och WebSocket-kommunikationen.
   * **Signalbehandling (`signal_processing.py`):** Filtrerar ljudet och beräknar koordinater med `scipy.optimize`.
   * **Maskininlärning (`model_logic.py` & `sound_classifier.h5`):** Laddar den tränade modellen och förutsäger ljudklassen. (Träningsskript finns i `train_ai.py`).

3. **Frontend (React / TypeScript)**
   * **UI (`App.tsx` & `main.tsx`):** Ett webbgränssnitt som låter användaren konfigurera fältets storlek och loggar inkommande händelser visuellt.

---

## Installation och Användning

### 1. Förbered Hårdvaran
* Flasha koden i `NodeBoard.ino` till 4 stycken ESP32-mikrokontrollers kopplade till MAX9814-mikrofoner.
* Notera nodernas MAC-adresser (konfigurera dessa i systemet).
* Flasha koden i `MainBoard.ino` till en femte ESP32 som kopplas till datorn via USB.

### 2. Starta Backend (Python)
Kräver Python 3.8+. Installera nödvändiga bibliotek:
```bash
pip install numpy scipy librosa tensorflow websockets pyserial
python server.py
npm install
cd frontend
cd frontend
npm run dev
```

* Öppna http://localhost:5173 i din webbläsare.
