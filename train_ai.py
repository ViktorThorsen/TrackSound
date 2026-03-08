import os
import numpy as np
import librosa
import tensorflow as tf
from tensorflow.keras import layers, models
from sklearn.model_selection import train_test_split

# --- KONFIGURATION ---
FS = 16000
DURATION = 0.4 # 400ms
SAMPLES = int(FS * DURATION)
DATA_PATH = "dataset" # Mapp med undermapparna 'clap', 'snap', 'noise'
CLASSES = ['clap', 'click', 'other'] # Exempelklasser (anpassa efter dina data)

def prepare_data():
    X = []
    y = []
    
    for idx, label in enumerate(CLASSES):
        folder = os.path.join(DATA_PATH, label)
        if not os.path.exists(folder):
            print(f"Varning: Mappen {folder} saknas. Hoppar över.")
            continue
            
        print(f"Laddar {label}...")
        for file in os.listdir(folder):
            if file.endswith('.wav'):
                path = os.path.join(folder, file)
                
                # 1. Ladda ljudfilen
                audio, _ = librosa.load(path, sr=FS, duration=DURATION)
                
                # Se till att alla är exakt lika långa (padding/cropping)
                if len(audio) < SAMPLES:
                    audio = np.pad(audio, (0, SAMPLES - len(audio)))
                else:
                    audio = audio[:SAMPLES]
                
                # 2. Skapa Mel-spektrogram (AI:ns "bild" av ljudet)
                mel_spec = librosa.feature.melspectrogram(y=audio, sr=FS, n_mels=128)
                mel_spec_db = librosa.power_to_db(mel_spec, ref=np.max)
                
                X.append(mel_spec_db)
                y.append(idx)
                
    return np.array(X), np.array(y)

# --- BYGG MODELLEN ---
def build_model(input_shape):
    model = models.Sequential([
        layers.Conv2D(32, (3, 3), activation='relu', input_shape=input_shape),
        layers.MaxPooling2D((2, 2)),
        layers.Conv2D(64, (3, 3), activation='relu'),
        layers.MaxPooling2D((2, 2)),
        layers.Flatten(),
        layers.Dense(64, activation='relu'),
        layers.Dense(3, activation='softmax') 
    ])
    model.compile(optimizer='adam', loss='sparse_categorical_crossentropy', metrics=['accuracy'])
    return model

# --- KÖR TRÄNINGEN ---
print("Förbereder dataset...")
X, y = prepare_data()

# Omforma data för CNN (Batch, Height, Width, Channels)
X = X[..., np.newaxis] 

# Dela upp i träning och test (80% träning, 20% validering)
X_train, X_val, y_train, y_val = train_test_split(X, y, test_size=0.2)

print(f"Tränar på {len(X_train)} prover, validerar på {len(X_val)}...")
model = build_model(X_train[0].shape)

# Kör träningen
model.fit(X_train, y_train, epochs=20, validation_data=(X_val, y_val), batch_size=16)

# Spara modellen
model.save('sound_classifier.h5')
print("Modell sparad som sound_classifier.h5")