import serial
import time
import numpy as np
import sys
from scipy.fft import rfft, irfft
from scipy.optimize import least_squares
from flask import Flask, request, jsonify
from flask_socketio import SocketIO
from threading import Thread
from flask_cors import CORS
from datetime import datetime
from scipy.signal import butter, lfilter
from model_logic import load_audio_model, predict_sound

app = Flask(__name__)
CORS(app)
socketio = SocketIO(app, cors_allowed_origins="*")
load_audio_model()

# --- GLOBAL STATE ---
state = {
    'width': 4.8, 
    'height': 3.7, 
    'temp': 21.5, 
    'v_sound': 344.0,
    'mode': 'DETECTING', # <--- ÄNDRAT FÖR TEST 
    'event_counter': 0,
    'offsets': {1: 0.0, 2: 0.0, 3: 0.0, 4: 0.0}
}

NODES = {
    1: np.array([0.0, 0.0]), 
    2: np.array([4.8, 0.0]),
    3: np.array([4.8, 3.7]), 
    4: np.array([0.0, 3.7])
}

node_raw_waves = {i: {} for i in range(1, 5)}
node_trigger_micros = {i: 0 for i in range(1, 5)} 
FS = 16000 

def update_sound_speed():
    state['v_sound'] = 331.3 + (0.606 * state['temp'])

def print_node_status(node_id, current, total=48):
    percent = int((current / total) * 100)
    bar = "█" * (percent // 5) + "-" * (20 - (percent // 5))
    sys.stdout.write(f"\r  Nod {node_id}: |{bar}| {percent}% ({current}/{total})")
    sys.stdout.flush()
    if current == total: print(f" -> Nod {node_id} OK")

def bandpass_filter(data, lowcut=300, highcut=4000, fs=16000, order=5):
    nyq = 0.5 * fs
    low = lowcut / nyq
    high = highcut / nyq
    b, a = butter(order, [low, high], btype='band')
    return lfilter(b, a, data)

def gcc_phat(sig, refsig, fs=16000):
    n = sig.shape[0] + refsig.shape[0]
    SIG = rfft(sig, n=n)
    REFSIG = rfft(refsig, n=n)
    R = SIG * np.conj(REFSIG)
    denom = np.abs(R)
    denom[denom == 0] = 1e-6
    cc = irfft(R / denom, n=n)
    max_shift = n // 2
    cc = np.concatenate((cc[-max_shift:], cc[:max_shift+1]))
    
    shift = np.argmax(np.abs(cc))
    
    # --- SUB-SAMPLE PRECISION ---
    if 0 < shift < len(cc) - 1:
        alpha = abs(cc[shift - 1])
        beta = abs(cc[shift])
        gamma = abs(cc[shift + 1])
        denominator = alpha - 2*beta + gamma
        if denominator != 0:
            correction = 0.5 * (alpha - gamma) / denominator
            shift_exact = shift + correction
        else:
            shift_exact = shift
    else:
        shift_exact = shift
        
    return (shift_exact - max_shift) / fs

def solve_position(arrivals):
    sensors = []
    tdoa_vals = []
    for nid, t in arrivals.items():
        sensors.append(NODES[nid])
        tdoa_vals.append(t)
        
    sensors = np.array(sensors)
    tdoa_vals = np.array(tdoa_vals)

    def residuals(p, s, t):
        d = np.linalg.norm(s - p, axis=1)
        return (d - d[0]) - (t * state['v_sound'])

    initial_guess = np.array([state['width']/2, state['height']/2])
    res = least_squares(residuals, initial_guess, 
                        args=(sensors, tdoa_vals),
                        bounds=([0, 0], [state['width'], state['height']]))
    
    error_margin = np.sqrt(2 * res.cost / len(tdoa_vals))
    return res.x, error_margin

last_processed_micros = {1: 0, 2: 0, 3: 0, 4: 0}
last_processed_id = -1

def process_event():
    global last_processed_id
    if state['mode'] == 'IDLE': return

    current_id = state['event_counter']
    state['event_counter'] += 1
    
    waves = {}
    valid_micros = {}
    
    # 1. Samla in data från noder som har skickat fullständiga vågformer
    for nid in range(1, 5):
        if len(node_raw_waves[nid]) >= 40:
            # --- ÄNDRING: Justera för 25ms delay (25000 us) istället för 10ms ---
            valid_micros[nid] = node_trigger_micros[nid] - (nid * 25000)
            
            # Bygg vågformen från chunks
            sorted_chunks = sorted(node_raw_waves[nid].items())
            wave_data = []
            for _, c in sorted_chunks: wave_data.extend(c)
            
            # Normalisera och filtrera
            raw_signal = np.array(wave_data, dtype=float) - 2048.0
            waves[nid] = bandpass_filter(raw_signal, lowcut=500, highcut=3000)
    
    # Rensa rådata omedelbart
    for nid in range(1, 5):
        node_raw_waves[nid].clear()
        node_trigger_micros[nid] = 0

    if len(waves) < 3:
        print(f"\n[!] Event #{current_id} ignorerat: För få noder ({len(waves)})")
        return

    # 2. Välj referensnod
    ref_nid = min(valid_micros.keys(), key=lambda n: valid_micros[n])
    arrivals = {ref_nid: 0.0}
    
    # --- ÄNDRING: Sänkt till 12ms (0.012s) för att blockera WiFi-jitter ---
    MAX_PHYSICAL_DIFF = 0.012 

    print(f"\n--- Analys Händelse #{current_id} (Ref: Nod {ref_nid}) ---")

    for nid in waves.keys():
        if nid == ref_nid: continue
        
        diff_us = valid_micros[nid] - valid_micros[ref_nid]
        coarse_delay = diff_us / 1000000.0
        fine_delay = gcc_phat(waves[nid], waves[ref_nid])
        total_diff = coarse_delay + fine_delay
        
        print(f" Nod {nid}: Grov={coarse_delay*1000:.2f}ms, Fin={fine_delay*1000:.2f}ms, Tot={total_diff*1000:.2f}ms")
        
        if abs(total_diff) > MAX_PHYSICAL_DIFF:
            print(f" [!] Ignorerar Nod {nid}: Orimlig tid ({total_diff*1000:.2f}ms)")
            continue
        
        arrivals[nid] = total_diff

    # 3. Beräkna position och kör AI
    if len(arrivals) >= 3:
        pos, error = solve_position(arrivals)
        
        # Kvalitetskontroll
        if error > 0.9: 
            print(f" [!] REJECTED: För hög osäkerhet ({error:.2f}m)")
            return
            
        sound_type = predict_sound(waves[ref_nid])
        
        print(f" [+] POSITION: X={pos[0]:.2f}m, Y={pos[1]:.2f}m (Margin: {error:.2f}m) | AI: {sound_type}")
    
        # Skicka till frontend
        socketio.emit('location_update', {
            'id': current_id,
            'x': float(pos[0]),
            'y': float(pos[1]),
            'label': sound_type,
            'error': float(error),
            'nodes': [str(n) for n in arrivals.keys()],
            'timestamp': int(time.time() * 1000)
        })
    else:
        print(" [!] Misslyckades: Inte tillräckligt med godkända noder kvar efter filtrering.")
        
def serial_reader():
    try:
        ser = serial.Serial('COM4', 2000000, timeout=0.1) # Ändra COM-port vid behov
        print("--- SERIAL READY ---")
    except:
        print("--- SERIAL ERROR ---"); return

    while True:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if not line: continue
        
        parts = line.split('|')
        tag = parts[0]

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
            nid = int(parts[1])
            cid = int(parts[2])
            samples = [int(x) for x in parts[4].split(',')]
            node_raw_waves[nid][cid] = samples
            print_node_status(nid, len(node_raw_waves[nid]))
            
        elif tag == "EVENT_END":
            time.sleep(0.2)
            Thread(target=process_event).start()

# --- API ENDPOINTS ---
@app.route('/api/config', methods=['POST'])
def update_config():
    data = request.json
    state['width'] = float(data.get('width', state['width']))
    state['height'] = float(data.get('height', state['height']))
    state['temp'] = float(data.get('temp', state['temp']))
    
    NODES[2] = np.array([state['width'], 0.0])
    NODES[3] = np.array([state['width'], state['height']])
    NODES[4] = np.array([0.0, state['height']])
    
    update_sound_speed()
    print(f"Config uppdaterad: {state['width']}x{state['height']}m")
    return jsonify({"status": "ok"})

@app.route('/api/mode', methods=['POST'])
def change_mode():
    data = request.json
    state['mode'] = data.get('mode', 'IDLE')
    print(f"System Mode: {state['mode']}")
    return jsonify({"status": "ok"})

if __name__ == '__main__':
    update_sound_speed()
    Thread(target=serial_reader, daemon=True).start()
    socketio.run(app, port=5000, debug=False)