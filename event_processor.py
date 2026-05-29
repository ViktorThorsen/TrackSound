import os
import time
import math  # <--- NYTT: För att kunna räkna ut diagonalen (Pythagoras)
import numpy as np
import soundfile as sf
from config import state, node_raw_waves, node_trigger_micros
from signal_processing import bandpass_filter, gcc_phat, solve_position
from model_logic import predict_sound

def process_event(socketio):
    if state['mode'] == 'IDLE': return

    current_id = state['event_counter']
    state['event_counter'] += 1
    waves, valid_micros = {}, {}
    
    for nid in range(1, 5):
        if len(node_raw_waves[nid]) >= 40:
            valid_micros[nid] = node_trigger_micros[nid]
            sorted_chunks = sorted(node_raw_waves[nid].items())
            wave_data = []
            for _, c in sorted_chunks: wave_data.extend(c)
            
            raw_signal = np.array(wave_data, dtype=float) - 2048.0
            waves[nid] = bandpass_filter(raw_signal)
            
            try:
                os.makedirs("debug_audio", exist_ok=True)
                sf.write(f"debug_audio/event_{current_id}_node_{nid}.wav", raw_signal / 2048.0, 16000)
            except Exception as e:
                print(f"  [!] Kunde inte spara ljudfil: {e}")
            
    for nid in range(1, 5):
        node_raw_waves[nid].clear()
        node_trigger_micros[nid] = 0

    if len(waves) < 3: return

    ref_nid = min(valid_micros.keys(), key=lambda n: valid_micros[n])
    arrivals = {ref_nid: 0.0}
    
    print(f"\n--- Analys Händelse #{current_id} (Ref: Nod {ref_nid}) ---")

    max_distance_meters = math.hypot(state['width'], state['height'])
    
    MAX_PHYSICAL_DIFF = (max_distance_meters / state['v_sound']) * 1.2
    # -------------------------------------------------------------

    for nid in waves.keys():
        if nid == ref_nid: continue
        diff_us = valid_micros[nid] - valid_micros[ref_nid]
        coarse_delay = diff_us / 1000000.0
        fine_delay = gcc_phat(waves[nid], waves[ref_nid])
        total_diff = coarse_delay + fine_delay
        
        print(f" Nod {nid}: Grov={coarse_delay*1000:.2f}ms, Fin={fine_delay*1000:.2f}ms, Tot={total_diff*1000:.2f}ms")
        
        if abs(total_diff) > MAX_PHYSICAL_DIFF: 
            print(f" [!] Ignorerar Nod {nid}: Orimlig tid för nuvarande skala (> {MAX_PHYSICAL_DIFF*1000:.1f}ms)")
            continue
            
        arrivals[nid] = total_diff

    if len(arrivals) >= 3:
        pos, error = solve_position(arrivals)
        if error > 0.9: return
            
        sound_type = predict_sound(waves[ref_nid])
        print(f" [+] POSITION: X={pos[0]:.2f}m, Y={pos[1]:.2f}m (Margin: {error:.2f}m) | AI: {sound_type}")
    
        socketio.emit('location_update', {
            'id': current_id, 'x': float(pos[0]), 'y': float(pos[1]),
            'label': sound_type, 'error': float(error),
            'nodes': [str(n) for n in arrivals.keys()], 'timestamp': int(time.time() * 1000)
        })