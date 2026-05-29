import numpy as np
from scipy.fft import rfft, irfft
from scipy.optimize import least_squares
from scipy.signal import butter, lfilter
from config import state, NODES

def bandpass_filter(data, lowcut=500, highcut=3000, fs=16000, order=5):
    nyq = 0.5 * fs
    b, a = butter(order, [lowcut / nyq, highcut / nyq], btype='band')
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
    
    if 0 < shift < len(cc) - 1:
        alpha = abs(cc[shift - 1])
        beta = abs(cc[shift])
        gamma = abs(cc[shift + 1])
        denominator = alpha - 2*beta + gamma
        if denominator != 0:
            correction = 0.5 * (alpha - gamma) / denominator
            return ((shift + correction) - max_shift) / fs
            
    return (shift - max_shift) / fs

def solve_position(arrivals):
    sensors, tdoa_vals = [], []
    for nid, t in arrivals.items():
        sensors.append(NODES[nid])
        tdoa_vals.append(t)
        
    sensors, tdoa_vals = np.array(sensors), np.array(tdoa_vals)

    def residuals(p, s, t):
        d = np.linalg.norm(s - p, axis=1)
        return (d - d[0]) - (t * state['v_sound'])

    initial_guess = np.array([state['width']/2, state['height']/2])
    res = least_squares(residuals, initial_guess, args=(sensors, tdoa_vals), bounds=([0, 0], [state['width'], state['height']]))
    
    return res.x, np.sqrt(2 * res.cost / len(tdoa_vals))