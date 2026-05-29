import numpy as np

# --- GLOBAL STATE ---
state = {
    'width': 4.8, 
    'height': 3.7, 
    'temp': 21.5, 
    'v_sound': 344.0,
    'mode': 'DETECTING',
    'event_counter': 0,
    'offsets': {1: 0.0, 2: 0.0, 3: 0.0, 4: 0.0}
}

NODES = {
    1: np.array([0.0, 0.0]), 
    2: np.array([state['width'], 0.0]),
    3: np.array([state['width'], state['height']]), 
    4: np.array([0.0, state['height']])
}

node_raw_waves = {i: {} for i in range(1, 5)}
node_trigger_micros = {i: 0 for i in range(1, 5)} 
FS = 16000 

def update_sound_speed():
    state['v_sound'] = 331.3 + (0.606 * state['temp'])