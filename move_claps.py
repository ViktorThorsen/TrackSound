import os
import shutil
import time

source_dir = "debug_audio"
target_dir = os.path.join("dataset", "clap")

# Se till att dataset-mappen finns
os.makedirs(target_dir, exist_ok=True)

# Leta upp alla .wav-filer i din debug-mapp
files = [f for f in os.listdir(source_dir) if f.endswith(".wav")]

if not files:
    print(f"Hittade inga ljudfiler i {source_dir}.")
else:
    count = 0
    for filename in files:
        source_path = os.path.join(source_dir, filename)
        
        # Skapa en unik tidsstämpel i millisekunder
        timestamp = int(time.time() * 1000)
        new_filename = f"clap_{timestamp}.wav"
        target_path = os.path.join(target_dir, new_filename)
        
        # Flytta och döp om filen
        shutil.move(source_path, target_path)
        print(f"Flyttade: {filename}  -->  {new_filename}")
        
        # Pausa 2 millisekunder så att nästa fil garanterat får ett unikt namn
        time.sleep(0.002) 
        count += 1
        
    print(f"\nKlart! Flyttade och döpte om {count} filer till {target_dir}.")