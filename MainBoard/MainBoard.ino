#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define CHUNK_SIZE 100
#define MAX_NODES 4
uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t nodeAddresses[5][6] = { 
    {0,0,0,0,0,0}, 
    {0xF8,0xB3,0xB7,0xC3,0x1D,0xD0}, 
    {0x34,0x5F,0x45,0x37,0xAC,0xA0}, 
    {0x34,0x5F,0x45,0x37,0xAB,0xBC}, 
    {0x34,0x5F,0x45,0x37,0xAB,0xAC} 
};

unsigned long lastSeen[5] = {0};
unsigned long lastStatusPrint = 0;
int activeNodes = 0;
int previousActiveNodes = -1;
bool nodeOnline[5] = {false};


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
    uint8_t type; uint8_t nodeId; uint8_t chunkId; uint32_t timestampUs;
    uint16_t samples[CHUNK_SIZE]; 
} struct_packet;

// --- GLOBAL STATE ---
bool isCollecting = false;
bool waitingForData = false;
unsigned long eventStartTime = 0;
unsigned long lastDataTime = 0;

bool pingsReceived[5] = {false};
uint32_t storedRefereeTimes[5] = {0}; 
int queue[5]; 
int queueSize = 0;
int currentInQueue = 0;

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
    uint8_t type = incomingData[0];

    if (type == 8) { 
        sync_req_packet *req = (sync_req_packet *)incomingData;
        lastSeen[req->nodeId] = millis();
        sync_ack_packet ack;
        ack.type = 9;
        ack.node_t1 = req->node_t1;
        ack.main_t2 = micros();

        esp_now_send(nodeAddresses[req->nodeId], (uint8_t *) &ack, sizeof(ack));
        return;
    }

    else if (type == 4) {
        ping_packet *p = (ping_packet *)incomingData;
        uint8_t nid = p->nodeId;
        
        uint32_t accurateTime = p->syncTime; 

        if (!isCollecting) {
            isCollecting = true;
            eventStartTime = millis();
            queueSize = 0;
            currentInQueue = 0;
            for(int i=1; i<=MAX_NODES; i++) { 
                pingsReceived[i] = false;
                storedRefereeTimes[i] = 0; 
            }
        }

        if (nid >= 1 && nid <= 4 && !pingsReceived[nid]) {
            pingsReceived[nid] = true;
            storedRefereeTimes[nid] = accurateTime;
            queue[queueSize] = nid;
            queueSize++;
        }
    } 
    else if (type == 3) {
        lastDataTime = millis();
        struct_packet *p = (struct_packet *)incomingData;
        
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
    Serial.setTxBufferSize(1024);
    Serial.begin(2000000);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(78); 
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(4, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    
    esp_now_init();
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = 4; 
    peerInfo.encrypt = false;
    memcpy(peerInfo.peer_addr, broadcastAddr, 6); esp_now_add_peer(&peerInfo);
    for (int i = 1; i <= MAX_NODES; i++) {
        memcpy(peerInfo.peer_addr, nodeAddresses[i], 6);
        esp_now_add_peer(&peerInfo);
    }
    Serial.println("MASTER_READY PÅ KANAL 4 (Agerar Tidsserver)");
}

void loop() {
    unsigned long now = millis();
    if (!isCollecting) {
        if (now - lastStatusPrint > 3000) { 
            int activeCount = 0;
            String missing = "";
            
            for (int i = 1; i <= 4; i++) {
                bool isNowOnline = (now - lastSeen[i] < 4000 && lastSeen[i] != 0); 
                
                if (isNowOnline && !nodeOnline[i]) {
                    Serial.printf("STATUS|[+] Nod %d upptäcktes och är nu ansluten!\n", i);
                } 
                else if (!isNowOnline && nodeOnline[i]) {
                    Serial.printf("STATUS|[-] Förlorade anslutningen till Nod %d!\n", i);
                }
                
                nodeOnline[i] = isNowOnline;
                
                if (isNowOnline) {
                    activeCount++;
                } else {
                    missing += String(i) + " ";
                }
            }
            
            if (activeCount == 0) {
                Serial.println("STATUS|Inga noder anslutna, letar...");
            } else if (activeCount < 4) {
                Serial.printf("STATUS|%d/4 noder redo. Saknar nod(er): %s\n", activeCount, missing.c_str());
            } else {
                Serial.println("STATUS|Alla 4 noder anslutna! Systemet redo för ljud.");
            }
            
            lastStatusPrint = now;
        }
    }
    if (isCollecting) {
        if (!waitingForData && (now - eventStartTime > 500)) { 
            if (queueSize >= 3) { 
                Serial.println("EVENT_START");
                Serial.flush(); 
                
                for(int i=0; i<queueSize; i++) {
                    int nid = queue[i];
                    Serial.printf("SYNC|%d|%u\n", nid, storedRefereeTimes[nid]);
                    Serial.flush(); 
                    delay(50);
                }
                
                waitingForData = true;
                lastDataTime = now;
                Serial.printf("STATUS|Ljud upptäckt av %d noder, väntar på data...\n", queueSize);
                int firstNode = queue[0];
                uint8_t sendCmd[] = {5, (uint8_t)firstNode, 1};
                esp_now_send(nodeAddresses[firstNode], sendCmd, 3);
            } 
            else {
                Serial.printf("STATUS|Ljud upptäckt av %d nod(er), ej tillräckligt. Återställer...\n", queueSize);
                uint8_t resetCmd[] = {5, 0, 2};
                esp_now_send(broadcastAddr, resetCmd, 3);
                isCollecting = false;
            }
        }

        if (waitingForData) {
            if (currentInQueue < queueSize) {
                int activeNode = queue[currentInQueue];
                if (now - lastDataTime > 400) { 
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