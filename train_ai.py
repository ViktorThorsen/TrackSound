import os
import numpy as np
import librosa
import tensorflow as tf
from tensorflow.keras.callbacks import EarlyStopping
from tensorflow.keras import layers, models
from sklearn.model_selection import train_test_split

# --- KONFIGURATION ---
FS = 16000
DURATION = 0.4
SAMPLES = int(FS * DURATION)
DATA_PATH = "dataset"
CLASSES = ['clap', 'click', 'other']

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
                
                audio, _ = librosa.load(path, sr=FS, duration=DURATION)
                
                if len(audio) < SAMPLES:
                    audio = np.pad(audio, (0, SAMPLES - len(audio)))
                else:
                    audio = audio[:SAMPLES]
                
                mel_spec = librosa.feature.melspectrogram(y=audio, sr=FS, n_mels=128)
                mel_spec_db = librosa.power_to_db(mel_spec, ref=np.max)
                
                X.append(mel_spec_db)
                y.append(idx)
                
    return np.array(X), np.array(y)

def build_model(input_shape):
    model = models.Sequential([
        layers.Conv2D(32, (3, 3), activation='relu', input_shape=input_shape),
        layers.MaxPooling2D((2, 2)),
        layers.Conv2D(64, (3, 3), activation='relu'),
        layers.MaxPooling2D((2, 2)),
        layers.Flatten(),
        layers.Dense(64, activation='relu'),
        layers.Dropout(0.5),
        layers.Dense(3, activation='softmax') 
    ])
    model.compile(optimizer='adam', loss='sparse_categorical_crossentropy', metrics=['accuracy'])
    return model

print("Förbereder dataset...")
X, y = prepare_data()

X = X[..., np.newaxis] 

X_train, X_val, y_train, y_val = train_test_split(X, y, test_size=0.2)

print(f"Tränar på {len(X_train)} prover, validerar på {len(X_val)}...")

model = build_model(X_train[0].shape)

early_stopping = EarlyStopping(
    monitor='val_loss',
    patience=4,
    restore_best_weights=True
)

model.fit(
    X_train, 
    y_train, 
    epochs=30,
    validation_data=(X_val, y_val), 
    batch_size=16,
    callbacks=[early_stopping]
)

model.save('sound_classifier.h5')
print("Modell sparad som sound_classifier.h5")