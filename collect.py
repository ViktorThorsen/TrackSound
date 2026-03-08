import serial
import time
import os
import wave
import numpy as np
import winsound

# --- KONFIGURATION ---
SERIAL_PORT = 'COM4'       # <--- Din port
BAUD_RATE = 2000000
LABEL = "clap"             # <--- "clap", "click" eller "other"
TOTAL_SAMPLES = 50         

FS = 16000
DURATION = 0.4
PEAK_TIME = 0.085

TARGET_LEN = int(FS * DURATION)
TARGET_PEAK = int(FS * PEAK_TIME)

if not os.path.exists(f"dataset/{LABEL}"):
    os.makedirs(f"dataset/{LABEL}")

def align_and_pad(signal):
    """ Placerar peaken på exakt 85ms och fyller ut till 400ms. Inget klipps! """
    peak_idx = np.argmax(np.abs(signal))
    out = np.zeros(TARGET_LEN, dtype=np.float32)

    start_in_out = TARGET_PEAK - peak_idx
    start_in_sig = 0

    if start_in_out < 0:
        start_in_sig = -start_in_out
        start_in_out = 0

    end_in_sig = len(signal)
    end_in_out = start_in_out + (end_in_sig - start_in_sig)

    if end_in_out > TARGET_LEN:
        end_in_sig -= (end_in_out - TARGET_LEN)
        end_in_out = TARGET_LEN

    out[start_in_out:end_in_out] = signal[start_in_sig:end_in_sig]
    return out

def play_countdown():
    print("\n----------------------------------")
    print("Gör dig redo...")
    time.sleep(1)
    
    print("3...")
    winsound.Beep(800, 200)
    time.sleep(0.8)
    
    print("2...")
    winsound.Beep(800, 200)
    time.sleep(0.8)
    
    print("1...")
    winsound.Beep(800, 200)
    time.sleep(0.8)
    
    print("*** KLAPPA NU! ***")
    winsound.Beep(1500, 400)

def main():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        print(f"--- UPKOPPLAD PÅ {SERIAL_PORT} ---")
    except Exception as e:
        print(f"Kunde inte öppna port: {e}")
        return

    print(f"Spelar in: {LABEL.upper()}")
    print("Sparar HELT RÅDATA från mikrofonerna (ingen kantskärning).")

    files_saved = 0
    node_raw_waves = {i: {} for i in range(1, 5)}

    while files_saved < TOTAL_SAMPLES:
        play_countdown()
        ser.reset_input_buffer()
        
        listen_start = time.time()
        event_done = False

        while not event_done:
            if time.time() - listen_start > 4.0:
                print(" [!] Tiden gick ut. Vi tar om den!")
                break
                
            try:
                if ser.in_waiting > 0:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if not line: continue
                    
                    parts = line.split('|')
                    tag = parts[0]

                    if tag == "EVENT_START":
                        for i in range(1, 5): node_raw_waves[i].clear()
                        
                    elif tag == "CHUNK" and len(parts) >= 5:
                        nid = int(parts[1])
                        cid = int(parts[2])
                        samples = [int(x) for x in parts[4].split(',')]
                        node_raw_waves[nid][cid] = samples
                        
                    elif tag == "EVENT_END":
                        valid_nodes = [nid for nid, chunks in node_raw_waves.items() if len(chunks) >= 40]
                        
                        if not valid_nodes:
                            print(" [!] Misslyckades: För lite data.")
                            event_done = True
                            continue
                        
                        print(f"--- Bearbetar händelse ({len(valid_nodes)} noder) ---")
                        
                        for nid in valid_nodes:
                            if files_saved >= TOTAL_SAMPLES: break
                                
                            sorted_chunks = sorted(node_raw_waves[nid].items())
                            wave_data = []
                            for _, c in sorted_chunks: wave_data.extend(c)
                            
                            # Endast normalisering för att centrera ljudvågen, inget bortklippt.
                            raw_signal = np.array(wave_data, dtype=float) - 2048.0
                            aligned_signal = align_and_pad(raw_signal)
                            audio_pcm = np.clip(aligned_signal * 16.0, -32768, 32767).astype(np.int16)

                            timestamp = int(time.time() * 1000)
                            filename = f"dataset/{LABEL}/{LABEL}.{timestamp}.n{nid}.wav"
                            
                            with wave.open(filename, 'w') as f:
                                f.setnchannels(1)
                                f.setsampwidth(2)
                                f.setframerate(FS)
                                f.writeframes(audio_pcm.tobytes())
                            
                            files_saved += 1
                            print(f" [{files_saved}/{TOTAL_SAMPLES}] Sparad: Nod {nid}")
                        
                        event_done = True
                        
            except KeyboardInterrupt:
                print("\nAvbrutet av användaren.")
                return
            except Exception as e:
                print(f" Fel vid inläsning: {e}")
                event_done = True
                
        time.sleep(1)
            
    print("\n--- INSPELNING KLAR! ---")
    winsound.Beep(500, 200)
    winsound.Beep(1000, 200)
    winsound.Beep(1500, 400)

if __name__ == '__main__':
    main()