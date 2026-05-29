import sys
import serial
import time
from threading import Thread
from config import node_raw_waves, node_trigger_micros
from event_processor import process_event

def print_node_status(node_id, current, total=48):
    percent = int((current / total) * 100)
    bar = "█" * (percent // 5) + "-" * (20 - (percent // 5))
    sys.stdout.write(f"\r  Nod {node_id}: |{bar}| {percent}% ({current}/{total})")
    sys.stdout.flush()
    if current == total: print(f" -> Nod {node_id} OK")

def serial_reader(socketio):
    try:
        ser = serial.Serial('COM4', 2000000, timeout=0.1)
        print("--- SERIAL READY ---")
    except:
        print("--- SERIAL ERROR ---"); return

    while True:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if not line: continue
        
        parts = line.split('|')
        tag = parts[0]

        if tag == "STATUS" and len(parts) >= 2:
            print(f"\n[SYSTEM] {parts[1]}")
            continue

        if tag == "EVENT_START":
            for i in range(1, 5): 
                node_raw_waves[i].clear()
                node_trigger_micros[i] = 0
            print("\n[!] Inkommande händelse...")
            
        elif tag == "SYNC" and len(parts) >= 3:
            nid = int(parts[1])
            microTime = int(parts[2])
            node_trigger_micros[nid] = microTime
            print(f"  [SYNC] Nod {nid} triggade vid {microTime} us")
            
        elif tag == "CHUNK" and len(parts) >= 5:
            try:
                nid, cid = int(parts[1]), int(parts[2])
                samples = [int(x) for x in parts[4].split(',')]
                node_raw_waves[nid][cid] = samples
                print_node_status(nid, len(node_raw_waves[nid]))
            except ValueError:
                pass
            
        elif tag == "EVENT_END":
            time.sleep(0.2)
            Thread(target=process_event, args=(socketio,)).start()