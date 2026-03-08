#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- KONFIGURATION ---
const int NODE_ID = 2; // ÄNDRA DENNA FÖR VARJE MICK (1, 2, 3, 4)
const int micPin = 34;
const int ledPin = 13; 

unsigned long lastSampleTime = 0;

uint8_t mainNodeAddress[] = {0x34, 0x5F, 0x45, 0x37, 0xAC, 0x34}; // Byt till din Mainboards riktiga MAC!
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

typedef struct ping_packet {
    uint8_t type;
    uint8_t nodeId;
} ping_packet;

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
int currentGain = 1;

// Snabb callback för att växla state när Mainboard skickar kommandon
void IRAM_ATTR OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len < 3 || data[0] != 5) return;
    
    uint8_t targetId = data[1];
    uint8_t command = data[2];

    if (command == 1 && (targetId == NODE_ID || targetId == 0)) {
        if (currentState == WAITING_FOR_ORDER) {
            currentState = SENDING;
            Serial.println("Order mottagen - Börjar sända!"); 
        }
    }
    else if (command == 2 && currentState != SENDING) {
        // Reset-kommando från Mainboard
        currentState = IDLE;
        isArmed = false;
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
        myPacket.timestampUs = triggerMicros;
        
        for (int s = 0; s < CHUNK_SIZE; s++) {
            int idx = (startIdx + (c * CHUNK_SIZE) + s) % RECORD_WINDOW_SAMPLES;
            myPacket.samples[s] = audioBuffer[idx];
        }
        esp_now_send(mainNodeAddress, (uint8_t *) &myPacket, sizeof(myPacket));
        delay(12); // Ge radion tid att skicka varje chunk
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
    
    // --- NYTT: WiFi Max effekt och Kanal 4 ---
    esp_wifi_set_max_tx_power(78); // 19.5 dBm
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(4, WIFI_SECOND_CHAN_NONE); // Låst till kanal 4
    esp_wifi_set_promiscuous(false);
    // ------------------------------------------

    if (esp_now_init() != ESP_OK) {
        Serial.println("Fel vid ESP-NOW init. Startar om...");
        ESP.restart();
    }
    
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    
    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = 4; // Måste vara Kanal 4
    peerInfo.encrypt = false;
    
    // Lägg till Mainboard
    memcpy(peerInfo.peer_addr, mainNodeAddress, 6);
    esp_now_add_peer(&peerInfo);
    
    // Lägg till Broadcast för reset-kommandon
    memcpy(peerInfo.peer_addr, broadcastAddr, 6);
    esp_now_add_peer(&peerInfo);
    
    autoTuneMic();
    Serial.printf("Nod %d REDO på Kanal 4.\n", NODE_ID);
}

void loop() {
    yield(); 
    unsigned long now = micros();
    
    if (currentState == IDLE || currentState == RECORDING) {
        
        // 1. LÄS MICKEN BLIXTSNABBT
        int raw = analogRead(micPin);
        int signal = abs(raw - quietLevel); 

        // Glidande medelvärde för att hålla baslinjen uppdaterad i tystnad
        if (signal < (triggerThreshold / 2)) {
            quietLevelFloat = (0.9999 * quietLevelFloat) + (0.0001 * raw);
            quietLevel = (int)quietLevelFloat;
        }

        if (currentState == IDLE) {
            if (!isArmed) {
                // Vänta på en stunds tystnad innan vi "armar" systemet
                if (signal < (triggerThreshold / 0.8)) {
                    if (++silenceCounter > 50000) { 
                        isArmed = true; 
                        Serial.println("System ARMED"); 
                    }
                } else { 
                    silenceCounter = 0; 
                }
            } 
            else if (signal > triggerThreshold) {
                // SMÄLL UPPTÄCKT!
                triggerMicros = now; // Tidsstämpla omedelbart
                digitalWrite(ledPin, HIGH);
                
                // --- NYTT: 25 ms tidsluckor istället för 10 ms ---
                delayMicroseconds(NODE_ID * 25000); 
                // -------------------------------------------------
                
                myPing.type = 4; 
                myPing.nodeId = NODE_ID;
                esp_now_send(mainNodeAddress, (uint8_t *) &myPing, sizeof(myPing));

                currentState = RECORDING; 
                isArmed = false; 
                samplesSinceTrigger = 0;
                Serial.println("TRIGGAD! Skickade PING.");
            }
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
        Serial.println("!!! SÄNDER DATA TILL MAINBOARD !!!");
        sendBuffer();
    }
    else if (currentState == WAITING_FOR_ORDER) {
        // Auto-återställning om Mainboard dör/tappar bort oss
        if (waitStart > 0 && (millis() - waitStart > 8000)) { 
            currentState = IDLE; 
            isArmed = false; 
            silenceCounter = -32000;
            digitalWrite(ledPin, LOW); 
            waitStart = 0;
            Serial.println("AUTO-RESET (Timeout från Mainboard)");
        }
    }
}