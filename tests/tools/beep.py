# Detect audio peaks with Librosa (https://librosa.github.io/librosa/)

# imports
from __future__ import print_function

import json
import sys

import librosa
import numpy as np
import datetime

# Load local audio file
y, sr = librosa.load(sys.argv[1])

# Get file duration in seconds
duration = librosa.get_duration(y=y)



tempo, beat_frames = librosa.beat.beat_track(y=y, sr=sr)

# 4. Convert the frame indices of beat events into timestamps
beat_times = librosa.frames_to_time(beat_frames, sr=sr)

t = 0

if (np.isscalar(tempo)):
    t = tempo

else:
    t = tempo[0]

json.dump({
    "duration": duration,
    "tempo": t,
    "beat_times": beat_times.tolist(),
}, sys.stdout)
