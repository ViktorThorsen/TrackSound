from flask import Flask, request, jsonify
from flask_socketio import SocketIO
from flask_cors import CORS
from threading import Thread
import numpy as np

# Importera våra moduler
from config import state, NODES, update_sound_speed
from serial_handler import serial_reader
from model_logic import load_audio_model

app = Flask(__name__)
CORS(app)
socketio = SocketIO(app, cors_allowed_origins="*")

load_audio_model()

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
    
    Thread(target=serial_reader, args=(socketio,), daemon=True).start()
    
    print("🚀 Servern är igång på port 5000!")
    socketio.run(app, port=5000, debug=False)