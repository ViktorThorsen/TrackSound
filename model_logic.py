import tensorflow as tf
import numpy as np
import librosa

# Globala inställningar
model = None
CLASSES = ['clap', 'click', 'other'] # Måste vara samma ordning som i train_ai.py
FS = 16000
DURATION = 0.4
SAMPLES = int(FS * DURATION)

def load_audio_model(model_path='sound_classifier.h5'):
    global model
    try:
        model = tf.keras.models.load_model(model_path)
        print("--- AI MODELL LADDAD ---")
    except Exception as e:
        print(f"--- FEL: Kunde inte ladda modell: {e}")

def predict_sound(raw_signal):
    if model is None:
        return "DETECTION"

    try:
        # 1. Se till att signalen är exakt 400ms (samma som vid träning)
        audio = raw_signal
        if len(audio) < SAMPLES:
            audio = np.pad(audio, (0, SAMPLES - len(audio)))
        else:
            audio = audio[:SAMPLES]

        # 2. Skapa Mel-spektrogram
        mel_spec = librosa.feature.melspectrogram(y=audio, sr=FS, n_mels=128)
        mel_spec_db = librosa.power_to_db(mel_spec, ref=np.max)

        # 3. Reshape för modellen (Batch, Height, Width, Channels)
        # Vi lägger till en batch-dimension och en kanal-dimension
        input_data = mel_spec_db[np.newaxis, ..., np.newaxis]

        # 4. Prediktera
        predictions = model.predict(input_data, verbose=0)
        class_idx = np.argmax(predictions[0])
        confidence = predictions[0][class_idx]

        # Om AI:n är osäker, returnera UNKNOWN
        if confidence < 0.6:
            return "UNKNOWN"
            
        return CLASSES[class_idx].upper()
    except Exception as e:
        print(f"AI Error: {e}")
        return "ERROR"