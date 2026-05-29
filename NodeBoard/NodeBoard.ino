#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- KONFIGURATION ---
const int NODE_ID = 4; // <--- ÄNDRA DENNA FÖR VARJE MICK (1, 2, 3, 4)
const int micPin = 34;
const int ledPin = 13;
unsigned long lastSampleTime = 0;

uint8_t mainNodeAddress[] = {0x34, 0x5F, 0x45, 0x37, 0xAC, 0x34}; // Byt till din Mainboards MAC!
uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#define RECORD_WINDOW_SAMPLES 4800 
#define PRE_TRIGGER_SAMPLES 1000   
#define CHUNK_SIZE 100             

enum SystemState { IDLE, RECORDING, WAITING_FOR_ORDER, SENDING };
volatile SystemState currentState = IDLE;

uint16_t audioBuffer[RECORD_WINDOW_SAMPLES];
int head = 0;
int samplesSinceTrigger = 0;
uint32_t triggerMicros = 0;

bool isArmed = false;
long silenceCounter = 0; 
unsigned long waitStart = 0;
bool pingPending = false;
unsigned long blindUntil = 0;

int32_t timeOffset = 0; 
unsigned long lastSyncReq = 0;
bool isSynced = false;

typedef struct ping_packet {
    uint8_t type;
    uint8_t nodeId;
    uint32_t syncTime;
} ping_packet;

typedef struct sync_req_packet {
    uint8_t type; // 8
    uint8_t nodeId;
    uint32_t node_t1;
} sync_req_packet;

typedef struct sync_ack_packet {
    uint8_t type; // 9
    uint32_t node_t1;
    uint32_t main_t2;
} sync_ack_packet;

typedef struct struct_packet {
    uint8_t type; 
    uint8_t nodeId; 
    uint8_t chunkId; 
    uint32_t timestampUs; 
    uint16_t samples[CHUNK_SIZE];
} struct_packet;

ping_packet myPing;
struct_packet myPacket;

float quietLevelFloat = 2048.0;
int quietLevel = 2048;
int triggerThreshold = 600; 

void IRAM_ATTR OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    uint8_t type = data[0];

    if (type == 9 && len == sizeof(sync_ack_packet)) {
        sync_ack_packet *ack = (sync_ack_packet *)data;
        uint32_t t3 = micros(); 
        
        uint32_t rtt = t3 - ack->node_t1;
        
        if (rtt < 4000) {
            uint32_t oneWay = rtt / 2; 
            uint32_t estimatedMainTime = ack->main_t2 + oneWay;
            timeOffset = estimatedMainTime - t3;
            isSynced = true;
        }
        return;
    }

    if (len < 3 || type != 5) return;
    uint8_t targetId = data[1];
    uint8_t command = data[2];

    if (command == 1 && (targetId == NODE_ID || targetId == 0)) {
        if (currentState == WAITING_FOR_ORDER) {
            currentState = SENDING;
            Serial.println("Order mottagen - Börjar sända!"); 
        }
    }
    else if (command == 2 && currentState != SENDING) {
        currentState = IDLE;
        isArmed = false;
        pingPending = false;
        silenceCounter = 0;
        digitalWrite(ledPin, LOW);
        Serial.println("Reset mottagen. Återgår till IDLE.");
    }
}

void autoTuneMic() {
    Serial.println("--- Kalibrerar Mick ---");
    long sum = 0;
    for(int i = 0; i < 2000; i++) { 
        sum += analogRead(micPin);
        delayMicroseconds(50); 
    }
    quietLevelFloat = sum / 2000.0;
    quietLevel = (int)quietLevelFloat;
    Serial.printf("Kalibrering klar. Baslinje: %d\n", quietLevel);
}

void sendBuffer() {
    int totalChunks = RECORD_WINDOW_SAMPLES / CHUNK_SIZE;
    int startIdx = (head - samplesSinceTrigger - PRE_TRIGGER_SAMPLES);
    while (startIdx < 0) startIdx += RECORD_WINDOW_SAMPLES;

    for (int c = 0; c < totalChunks; c++) {
        myPacket.type = 3;
        myPacket.nodeId = NODE_ID; 
        myPacket.chunkId = c; 
        myPacket.timestampUs = triggerMicros + timeOffset; // Skicka den synkade tiden här med!
        
        for (int s = 0; s < CHUNK_SIZE; s++) {
            int idx = (startIdx + (c * CHUNK_SIZE) + s) % RECORD_WINDOW_SAMPLES;
            myPacket.samples[s] = audioBuffer[idx];
        }
        esp_now_send(mainNodeAddress, (uint8_t *) &myPacket, sizeof(myPacket));
        delay(12);
    }
    
    Serial.println("Sändning färdig. Inväntar Reset...");
    currentState = WAITING_FOR_ORDER; 
    digitalWrite(ledPin, LOW);
}

void setup() {
    Serial.begin(115200);
    pinMode(ledPin, OUTPUT);
    
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(78);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(4, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Fel vid ESP-NOW init. Startar om...");
        ESP.restart();
    }
    
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    
    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = 4;
    peerInfo.encrypt = false;
    memcpy(peerInfo.peer_addr, mainNodeAddress, 6);
    esp_now_add_peer(&peerInfo);
    memcpy(peerInfo.peer_addr, broadcastAddr, 6);
    esp_now_add_peer(&peerInfo);
    
    autoTuneMic();
    Serial.printf("Nod %d REDO på Kanal 4.\n", NODE_ID);
}

void loop() {
    yield(); 
    unsigned long now = micros();

    // --- NYTT: Trafiksäker Tids-synk (Round Robin) ---
    unsigned long currentMillis = millis();
    unsigned long cycle = currentMillis % 2000; // Skapar en 2-sekunders loop (0-1999 ms)
    unsigned long myTurn = NODE_ID * 400;       // Nod 1 = 400ms, Nod 2 = 800ms, Nod 3 = 1200ms, Nod 4 = 1600ms

    // Synka BARA om det gått 1.5 sek, OCH noden befinner sig i sitt unika tidsfönster (en lucka på 100ms)
    if (currentState == IDLE && (currentMillis - lastSyncReq > 1500) && (cycle >= myTurn) && (cycle < myTurn + 100)) {
        sync_req_packet req;
        req.type = 8;
        req.nodeId = NODE_ID;
        req.node_t1 = micros();
        esp_now_send(mainNodeAddress, (uint8_t *)&req, sizeof(req));
        lastSyncReq = currentMillis;

        blindUntil = currentMillis + 50;
    }

    if (currentState == IDLE || currentState == RECORDING) {
        int raw = analogRead(micPin);
        int signal = abs(raw - quietLevel); 

        if (signal < (triggerThreshold / 2)) {
            quietLevelFloat = (0.9999 * quietLevelFloat) + (0.0001 * raw);
            quietLevel = (int)quietLevelFloat;
        }

        if (currentState == IDLE) {
            if (!isArmed) {
                if (signal < (triggerThreshold / 0.8)) {
                    if (++silenceCounter > 50000) { 
                        isArmed = true;
                        Serial.println("System ARMED (Och synkat)"); 
                    }
                } else { 
                    silenceCounter = 0;
                }
            } 
            else if (signal > triggerThreshold && isSynced && millis() > blindUntil) { // Måste vara synkad för att trigga!
                triggerMicros = now; // Lokal tidsstämpel
                digitalWrite(ledPin, HIGH);
                
                pingPending = true; // Fördröjd sändning (Non-blocking)
                currentState = RECORDING; 
                isArmed = false; 
                samplesSinceTrigger = 0;
                Serial.println("TRIGGAD! Inspelning startar (PING fördröjd)");
            }
        } 
        
        // --- NYTT: Skicka PING fördröjt, men med EXAKT uträknad tid ---
        if (pingPending && (now - triggerMicros >= (NODE_ID * 25000))) {
            myPing.type = 4;
            myPing.nodeId = NODE_ID;
            
            // MAGIN HÄNDER HÄR: Vi lägger vår lokala tid + offseten!
            myPing.syncTime = triggerMicros + timeOffset; 
            
            esp_now_send(mainNodeAddress, (uint8_t *) &myPing, sizeof(myPing));
            pingPending = false;
            Serial.println("Skickade PING med kompenserad tidsstämpel.");
        }

        // 2. SPARA LJUDET (Exakt 16000 Hz)
        if (now - lastSampleTime >= 62) {
            lastSampleTime = now;
            audioBuffer[head] = (uint16_t)raw;
            head = (head + 1) % RECORD_WINDOW_SAMPLES;
            
            if (currentState == RECORDING) {
                if (++samplesSinceTrigger >= (RECORD_WINDOW_SAMPLES - PRE_TRIGGER_SAMPLES)) {
                    currentState = WAITING_FOR_ORDER;
                    waitStart = millis();
                    Serial.println("Inspelning klar. Väntar på order...");
                }
            }
        }
    }
    else if (currentState == SENDING) {
        sendBuffer();
    }
    else if (currentState == WAITING_FOR_ORDER) {
        if (waitStart > 0 && (millis() - waitStart > 8000)) { 
            currentState = IDLE;
            isArmed = false; 
            pingPending = false;
            silenceCounter = -32000;
            digitalWrite(ledPin, LOW); 
            waitStart = 0;
            Serial.println("AUTO-RESET (Timeout från Mainboard)");
        }
    }
}