#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define CHUNK_SIZE 100
#define MAX_NODES 4
uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t nodeAddresses[5][6] = { {0,0,0,0,0,0}, {0xF8,0xB3,0xB7,0xC3,0x1D,0xD0}, {0x34,0x5F,0x45,0x37,0xAC,0xA0}, {0x34,0x5F,0x45,0x37,0xAB,0xBC}, {0x34,0x5F,0x45,0x37,0xAB,0xAC} };

typedef struct ping_packet {
    uint8_t type;
    uint8_t nodeId;
} ping_packet;

typedef struct struct_packet { 
    uint8_t type; uint8_t nodeId; uint8_t chunkId; uint32_t timestampUs; uint16_t samples[CHUNK_SIZE]; 
} struct_packet;

// --- GLOBAL STATE ---
bool isCollecting = false;
bool waitingForData = false;
unsigned long eventStartTime = 0;
unsigned long lastDataTime = 0;

bool pingsReceived[5] = {false};
uint32_t storedRefereeTimes[5] = {0}; // <--- Den viktiga nollställningen
int queue[5]; 
int queueSize = 0;
int currentInQueue = 0;

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
    uint8_t type = incomingData[0];
    
    if (type == 4) { // PING
        uint32_t nowMicros = micros(); // STÄMPLA DIREKT!
        ping_packet *p = (ping_packet *)incomingData;
        uint8_t nid = p->nodeId;
        
        if (!isCollecting) {
            isCollecting = true;
            eventStartTime = millis();
            queueSize = 0;
            currentInQueue = 0;
            for(int i=1; i<=MAX_NODES; i++) { 
                pingsReceived[i] = false; 
                storedRefereeTimes[i] = 0; // Nollställ gammalt skräp!
            }
        }

        if (nid >= 1 && nid <= 4 && !pingsReceived[nid]) {
            pingsReceived[nid] = true;
            storedRefereeTimes[nid] = nowMicros; 
            queue[queueSize] = nid;
            queueSize++;
        }
    } 
    else if (type == 3) { // DATA (Chunks)
        lastDataTime = millis();
        struct_packet *p = (struct_packet *)incomingData;
        
        // Skicka rådata till Python
        Serial.printf("CHUNK|%d|%d|%u|", p->nodeId, p->chunkId, p->timestampUs);
        for (int i=0; i<CHUNK_SIZE; i++) { 
            Serial.print(p->samples[i]);
            if (i < CHUNK_SIZE-1) Serial.print(","); 
        }
        Serial.println();

        if (p->chunkId == 47) {
            currentInQueue++; 
        }
    }
}

void setup() {
    Serial.begin(2000000);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    
    // --- NYTT FÖR ATT MATCHA DINA MICKAR ---
    esp_wifi_set_max_tx_power(78); 
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(4, WIFI_SECOND_CHAN_NONE); // Låst till kanal 4
    esp_wifi_set_promiscuous(false);
    // ----------------------------------------
    
    esp_now_init();
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    
    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = 4; // <--- MATCHAR KANAL 4 HÄR OCKSÅ
    peerInfo.encrypt = false;
    
    memcpy(peerInfo.peer_addr, broadcastAddr, 6); esp_now_add_peer(&peerInfo);
    for (int i = 1; i <= MAX_NODES; i++) {
        memcpy(peerInfo.peer_addr, nodeAddresses[i], 6); esp_now_add_peer(&peerInfo);
    }
    Serial.println("MASTER_READY PÅ KANAL 4");
}

void loop() {
    unsigned long now = millis();
    
    if (isCollecting) {
        // FAS 1: Vänta in alla PINGs
        if (!waitingForData && (now - eventStartTime > 500)) { // 500ms räcker gott
            
            if (queueSize >= 3) { // Acceptera om 3 eller 4 mickar svarar
                Serial.println("EVENT_START");
                
                // --- NU SKICKAR VI ALLA SYNC-MEDDELANDEN SAMTIDIGT ---
                for(int i=0; i<queueSize; i++) {
                    int nid = queue[i];
                    Serial.printf("SYNC|%d|%u\n", nid, storedRefereeTimes[nid]);
                }
                
                waitingForData = true;
                lastDataTime = now;
                Serial.printf("VALIDERAT: %d noder OK. Startar hämtning...\n", queueSize);
                
                // Be första noden börja sända chunks
                int firstNode = queue[0];
                uint8_t sendCmd[] = {5, (uint8_t)firstNode, 1};
                esp_now_send(nodeAddresses[firstNode], sendCmd, 3);
            } 
            else {
                Serial.printf("AVBRUTET: Endast %d noder svarade. Återställer...\n", queueSize);
                uint8_t resetCmd[] = {5, 0, 2};
                esp_now_send(broadcastAddr, resetCmd, 3);
                isCollecting = false;
            }
        }

        // FAS 2: Hantera kön för chunks (Hämta en nod i taget)
        if (waitingForData) {
            if (currentInQueue < queueSize) {
                int activeNode = queue[currentInQueue];
                if (now - lastDataTime > 400) { // Timeout/Retry
                    uint8_t retryCmd[] = {5, (uint8_t)activeNode, 1};
                    esp_now_send(nodeAddresses[activeNode], retryCmd, 3);
                    lastDataTime = now;
                }
            } 
            else {
                Serial.println("EVENT_END|COMPLETE");
                uint8_t resetCmd[] = {5, 0, 2};
                esp_now_send(broadcastAddr, resetCmd, 3);
                delay(100);
                isCollecting = false;
                waitingForData = false;
                Serial.println("--- READY ---");
            }
        }
    }
}