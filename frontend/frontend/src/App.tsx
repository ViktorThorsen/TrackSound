import React, { useState, useEffect, useRef } from 'react';
import { io, Socket } from 'socket.io-client';
import { Radar, Settings, Play, Crosshair, Trash2, Thermometer, Square, Wifi, WifiOff, Target, User, Scaling, BoxSelect } from 'lucide-react';

// --- TYPER ---
interface ShotEvent {
  id: number;
  x: number;
  y: number;
  label: string;
  nodes: string[];
  timestamp: number;
}

interface Config {
  temp: number;
  width: number;
  height: number;
}

type SystemMode = 'IDLE' | 'CALIBRATING' | 'DETECTING';

// --- CSS STYLES ---
const styles = `
  :root {
    --primary: #00ff41;
    --bg-dark: #050505;
    --panel-bg: #111;
    --border: #333;
    --danger: #ff003c;
    --warning: #f9d71c;
    --user-color: #00aaff;
    --range-color: #ff9900;
  }
  
  html, body {
    width: 100vw !important;
    height: 100vh !important;
    margin: 0 !important;
    padding: 0 !important;
    overflow: hidden !important;
    background: var(--bg-dark);
    font-family: 'Courier New', monospace;
    color: var(--primary);
    display: block !important; 
    place-items: start !important;
    min-width: 0 !important;
    user-select: none;
  }

  #root {
    width: 100% !important;
    height: 100% !important;
    margin: 0 !important;
    padding: 0 !important;
    max-width: none !important; 
    text-align: left !important;
    display: flex;
    flex-direction: column;
  }
  
  .app-container { 
    display: flex; 
    flex-direction: column; 
    width: 100%;
    height: 100%; 
    padding: 20px; 
    box-sizing: border-box; 
  }
  
  .header { 
    flex: 0 0 auto;
    display: flex; justify-content: space-between; align-items: center; 
    padding-bottom: 15px; border-bottom: 2px solid var(--primary); margin-bottom: 20px; 
  }
  
  .main-grid { 
    flex: 1; 
    min-height: 0; 
    display: grid; 
    grid-template-columns: 320px 1fr; 
    gap: 20px; 
  }
  
  .sidebar { display: flex; flex-direction: column; gap: 20px; height: 100%; overflow: hidden; }
  .panel { background: var(--panel-bg); border: 1px solid var(--border); padding: 15px; }
  .panel-header { display: flex; align-items: center; gap: 10px; margin: 0 0 15px 0; font-size: 1.1rem; border-bottom: 1px solid #222; padding-bottom: 10px; }
  
  .input-group { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
  input { background: #000; color: var(--primary); border: 1px solid var(--primary); padding: 5px; width: 70px; text-align: center; font-family: inherit; }
  input[type="checkbox"] { width: auto; cursor: pointer; accent-color: var(--primary); }
  
  .btn { width: 100%; padding: 15px; border: none; font-weight: bold; cursor: pointer; display: flex; align-items: center; justify-content: center; gap: 10px; font-family: inherit; transition: all 0.2s; text-transform: uppercase; }
  .btn-primary { background: var(--primary); color: #000; }
  .btn-danger { background: var(--danger); color: #fff; }
  .btn-neutral { background: #222; color: #666; cursor: not-allowed; border: 1px solid #333; }
  
  .log-container { flex-grow: 1; overflow-y: auto; display: flex; flex-direction: column; gap: 5px; }
  .log-item { background: #0a0a0a; padding: 10px; border-left: 3px solid #333; font-size: 0.85rem; display: flex; justify-content: space-between; align-items: center; }
  .log-item.latest { border-left-color: var(--primary); background: #0f1a0f; }
  .log-nodes { font-size: 0.7rem; color: #666; margin-top: 4px; }

  /* Map Wrapper */
  .map-wrapper { 
    position: relative; 
    background: #080808; 
    border: 1px solid #333; 
    width: 100%; 
    height: 100%; 
    overflow: hidden; 
  }
  .map-wrapper.detecting { border-color: var(--primary); box-shadow: 0 0 20px rgba(0, 255, 65, 0.1); }
  .map-wrapper.calibrating { border-color: var(--danger); }
  
  /* Room Physics */
  .room-physics { 
    position: absolute; 
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    background-color: #0a0a0a; 
    border: 1px dashed #444; 
    box-shadow: inset 0 0 50px #000;
    transition: width 0.1s, height 0.1s, background-image 0.3s ease-in-out; 
    touch-action: none; 
    background-size: cover;
    background-position: center;
    background-repeat: no-repeat;
  }
  
  .grid-lines { position: absolute; inset: 0; background-image: linear-gradient(rgba(26,26,26,0.8) 1px, transparent 1px), linear-gradient(90deg, rgba(26,26,26,0.8) 1px, transparent 1px); background-size: 50px 50px; opacity: 0.5; pointer-events: none; }
  .cross-center { position: absolute; top: 50%; left: 50%; width: 20px; height: 20px; border-left: 1px solid rgba(255,255,255,0.3); border-top: 1px solid rgba(255,255,255,0.3); transform: translate(-50%, -50%); pointer-events: none; }

  .shot-marker { position: absolute; transform: translate(-50%, -50%); border-radius: 50%; display: flex; justify-content: center; align-items: center; transition: all 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275); pointer-events: none; }
  .shot-marker.latest { width: 16px; height: 16px; background: var(--danger); z-index: 10; box-shadow: 0 0 15px var(--danger); }
  .shot-marker.old { width: 8px; height: 8px; background: var(--warning); opacity: 0.6; z-index: 1; }
  .shot-label { position: absolute; bottom: 20px; background: rgba(0,0,0,0.8); color: #fff; padding: 2px 6px; font-size: 10px; border: 1px solid var(--primary); white-space: nowrap; pointer-events: none; }
  
  /* User Marker */
  .user-marker {
    position: absolute;
    transform: translate(-50%, -50%);
    width: 34px;
    height: 34px;
    border-radius: 50%;
    background: rgba(0, 170, 255, 0.15);
    border: 2px solid var(--user-color);
    display: flex; justify-content: center; align-items: center;
    z-index: 15; cursor: grab; transition: box-shadow 0.2s, background 0.2s;
  }
  .user-marker:hover { background: rgba(0, 170, 255, 0.3); }
  .user-marker.dragging { cursor: grabbing; box-shadow: 0 0 20px rgba(0, 170, 255, 0.5); }

  /* Shooting Range */
  .shooting-range {
    position: absolute;
    transform: translate(-50%, -50%);
    background: rgba(255, 153, 0, 0.2);
    border: 2px dashed var(--range-color);
    z-index: 5;
    cursor: grab;
    transition: background 0.2s;
  }
  .shooting-range:hover { background: rgba(255, 153, 0, 0.3); }
  .shooting-range.dragging { cursor: grabbing; border: 2px solid var(--range-color); box-shadow: 0 0 20px rgba(255, 153, 0, 0.3); }
  .range-label {
    position: absolute;
    top: -25px;
    left: 50%;
    transform: translateX(-50%);
    background: var(--bg-dark);
    color: var(--range-color);
    border: 1px solid var(--range-color);
    padding: 2px 8px;
    font-size: 0.7rem;
    font-weight: bold;
    white-space: nowrap;
    pointer-events: none;
  }
  
  @keyframes ripple { 0% { width: 0; height: 0; opacity: 1; } 100% { width: 100px; height: 100px; opacity: 0; } }
  .ripple { position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); border: 2px solid var(--danger); border-radius: 50%; animation: ripple 1s ease-out infinite; }
  @keyframes blink { 0% { opacity: 1; } 50% { opacity: 0.4; } 100% { opacity: 1; } }
  .pulse { animation: blink 2s infinite; }
`;

export default function App() {
  const [socket, setSocket] = useState<Socket | null>(null);
  const [connected, setConnected] = useState(false);
  
  const [config, setConfig] = useState<Config>({ temp: 21.5, width: 4.8, height: 3.7 });
  const [mode, setMode] = useState<SystemMode>('IDLE');
  const [calibCount, setCalibCount] = useState(0);
  //const [events, setEvents] = useState<ShotEvent[]>([]);
  // --- TILLFÄLLIG MOCK DATA FÖR ATT TESTA UI ---
  const [events, setEvents] = useState<ShotEvent[]>(() => {
    // Räkna ut ungefärlig mittpunkt baserat på din standard-config (4.8 / 2 = 2.4, 3.7 / 2 = 1.85)
    const centerX = 2.4;
    const centerY = 1.85;
    
    return Array.from({ length: 5 }).map((_, i) => {
      // Lägg till en slumpmässig spridning (mellan -0.6 och +0.6 meter från mitten)
      const randomX = centerX + (Math.random() * 1.2 - 0.6);
      const randomY = centerY + (Math.random() * 1.2 - 0.6);
      
      return {
        id: 9900 + i, // Tillfälligt högt ID så de inte krockar med nya riktiga skott
        x: randomX,
        y: randomY,
        label: i === 4 ? "CLAP" : "CLAP",
        nodes: ['N1', 'N2', 'N3', 'N4'],
        timestamp: Date.now() - (5000 - i * 1000)
      };
    }).reverse(); // Vänder arrayen så den sista blir index 0
  });

  // --- ANVÄNDARPOSITION ---
  const [userPos, setUserPos] = useState({ x: 2.4, y: 1.85 }); 
  const [isDraggingUser, setIsDraggingUser] = useState(false);

  // --- SHOOTING RANGE ---
  const [shootingRange, setShootingRange] = useState({ x: 1.5, y: 1.5, w: 1.5, h: 1.0 });
  const [isDraggingRange, setIsDraggingRange] = useState(false);

  // --- UI TOGGLES (NYTT) ---
  const [isCommunityScale, setIsCommunityScale] = useState(false);
  const [showShootingRange, setShowShootingRange] = useState(false);
  const [showUserPos, setShowUserPos] = useState(false);

  const scaleMult = isCommunityScale ? 100 : 1; 

  // --- RESIZE OBSERVER ---
  const mapContainerRef = useRef<HTMLDivElement>(null);
  const roomRef = useRef<HTMLDivElement>(null);
  const [roomDims, setRoomDims] = useState({ w: 0, h: 0, pxPerMeter: 0 });

  useEffect(() => {
    if (!mapContainerRef.current) return;
    const observer = new ResizeObserver((entries) => {
      for (let entry of entries) {
        const { width: availableW, height: availableH } = entry.contentRect;
        const safeW = availableW - 40;
        const safeH = availableH - 40;
        if (safeW <= 0 || safeH <= 0) return;

        const roomRatio = config.width / config.height;
        const screenRatio = safeW / safeH;

        let finalW, finalH, scale;
        if (roomRatio > screenRatio) {
          finalW = safeW;
          finalH = finalW / roomRatio;
          scale = finalW / config.width;
        } else {
          finalH = safeH;
          finalW = finalH * roomRatio;
          scale = finalH / config.height;
        }
        setRoomDims({ w: finalW, h: finalH, pxPerMeter: scale });
      }
    });
    observer.observe(mapContainerRef.current);
    return () => observer.disconnect();
  }, [config.width, config.height]);

  useEffect(() => {
    setUserPos(prev => ({
      x: Math.max(0, Math.min(prev.x, config.width)),
      y: Math.max(0, Math.min(prev.y, config.height))
    }));
    setShootingRange(prev => ({
      ...prev,
      x: Math.max(0, Math.min(prev.x, config.width)),
      y: Math.max(0, Math.min(prev.y, config.height))
    }));
  }, [config.width, config.height]);

  // --- SOCKETS ---
  useEffect(() => {
    const newSocket = io('http://localhost:5000', { reconnectionAttempts: 5, timeout: 2000 });
    newSocket.on('connect', () => setConnected(true));
    newSocket.on('disconnect', () => setConnected(false));
    
    newSocket.on('location_update', (data: any) => {
      const newEvent = { ...data, timestamp: Date.now() };
      setEvents(prev => [newEvent, ...prev].slice(0, 20));
    });

    newSocket.on('label_update', (data: { id: number, label: string }) => {
      setEvents(prev => prev.map(e => e.id === data.id ? { ...e, label: data.label } : e));
    });

    setSocket(newSocket);
    return () => { newSocket.close(); };
  }, []);

  const changeMode = async (targetMode: SystemMode) => {
    if (mode === targetMode) targetMode = 'IDLE';
    if (targetMode === 'CALIBRATING' || targetMode === 'DETECTING') setEvents([]);
    try {
      await fetch('http://localhost:5000/api/mode', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mode: targetMode })
      });
      setMode(targetMode);
    } catch (e) { alert("Kunde inte nå servern!"); }
  };

  const applyConfig = async () => {
    try {
      await fetch('http://localhost:5000/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config)
      });
      alert("Config uppdaterad!");
    } catch (e) { alert("Error saving config"); }
  };

  // --- DRAG LOGIK ---
  const handlePointerDown = (e: React.PointerEvent, target: 'user' | 'range') => {
    // Tillåt bara drag om Community Scale är igång och vi faktiskt visar komponenten
    if (!isCommunityScale) return;
    if (target === 'user' && !showUserPos) return;
    if (target === 'range' && !showShootingRange) return;
    
    e.stopPropagation(); 
    if (!roomRef.current) return;
    
    const isUser = target === 'user';
    if (isUser) setIsDraggingUser(true);
    else setIsDraggingRange(true);

    const updatePosition = (ev: PointerEvent) => {
      const rect = roomRef.current!.getBoundingClientRect();
      let xPx = ev.clientX - rect.left;
      let yPx = ev.clientY - rect.top;

      xPx = Math.max(0, Math.min(xPx, rect.width));
      yPx = Math.max(0, Math.min(yPx, rect.height));

      const newX = (xPx / rect.width) * config.width;
      const newY = (1 - (yPx / rect.height)) * config.height;

      if (isUser) {
        setUserPos({ x: newX, y: newY });
      } else {
        setShootingRange(prev => ({ ...prev, x: newX, y: newY }));
      }
    };

    updatePosition(e.nativeEvent);

    const handlePointerUp = () => {
      if (isUser) setIsDraggingUser(false);
      else setIsDraggingRange(false);
      window.removeEventListener('pointerup', handlePointerUp);
      window.removeEventListener('pointermove', updatePosition);
    };

    window.addEventListener('pointerup', handlePointerUp);
    window.addEventListener('pointermove', updatePosition);
  };

  return (
    <>
      <style>{styles}</style>
      <div className="app-container">
        
        {/* HEADER */}
        <header className="header">
          <div style={{ display: 'flex', alignItems: 'center', gap: '15px' }}>
            <Radar size={32} className={mode === 'DETECTING' ? 'pulse' : ''} />
            <div>
              <h1 style={{ margin: 0, fontSize: '1.5rem', letterSpacing: '2px' }}>GSR PRO <span style={{fontSize:'0.8rem', color:'#666'}}>v4.9</span></h1>
              <div style={{ fontSize: '0.7rem', color: '#666' }}>ACOUSTIC MULTILATERATION SYSTEM</div>
            </div>
          </div>
          <div style={{ display: 'flex', gap: '20px', alignItems: 'center' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: '5px', fontSize: '0.8rem', color: connected ? 'var(--primary)' : 'var(--danger)' }}>
              {connected ? <Wifi size={16} /> : <WifiOff size={16} />}
              {connected ? 'ONLINE' : 'DISCONNECTED'}
            </div>
            <div style={{ padding: '5px 15px', background: mode === 'IDLE' ? '#333' : mode === 'CALIBRATING' ? 'var(--danger)' : 'var(--primary)', color: mode === 'DETECTING' ? '#000' : '#fff', fontWeight: 'bold', borderRadius: '4px' }}>
              {mode}
            </div>
          </div>
        </header>

        <div className="main-grid">
          
          {/* SIDEBAR */}
          <div className="sidebar" style={{ overflowY: 'auto', paddingRight: '5px' }}>
            
            <div className="panel" style={{ opacity: mode === 'IDLE' ? 1 : 0.5, pointerEvents: mode === 'IDLE' ? 'auto' : 'none' }}>
              <div className="panel-header"><Settings size={18} /> FIELD CONFIG</div>
              
              {/* NYTT: Toggles är nu inbäddade under Community Scale */}
              <div style={{ marginBottom: '15px', paddingBottom: '10px', borderBottom: '1px solid #222', display: 'flex', flexDirection: 'column', gap: '10px' }}>
                <label style={{ display: 'flex', alignItems: 'center', gap: '8px', cursor: 'pointer', color: 'var(--warning)', fontWeight: 'bold' }}>
                  <input type="checkbox" checked={isCommunityScale} onChange={e => setIsCommunityScale(e.target.checked)} />
                  Community Scale (x100)
                </label>

                {/* VISAS BARA OM COMMUNITY SCALE ÄR IKRYSsat */}
                {isCommunityScale && (
                  <div style={{ display: 'flex', flexDirection: 'column', gap: '8px', marginLeft: '25px' }}>
                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', cursor: 'pointer', color: 'var(--range-color)', fontSize: '0.9rem' }}>
                      <input type="checkbox" checked={showShootingRange} onChange={e => setShowShootingRange(e.target.checked)} />
                      Show Shooting Range
                    </label>
                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', cursor: 'pointer', color: 'var(--user-color)', fontSize: '0.9rem' }}>
                      <input type="checkbox" checked={showUserPos} onChange={e => setShowUserPos(e.target.checked)} />
                      Show My Position
                    </label>
                  </div>
                )}
              </div>

              <div className="input-group">
                <span>Width (m)</span>
                <input type="number" step="0.1" value={config.width} onChange={e => setConfig({...config, width: parseFloat(e.target.value)})} />
              </div>
              <div className="input-group">
                <span>Height (m)</span>
                <input type="number" step="0.1" value={config.height} onChange={e => setConfig({...config, height: parseFloat(e.target.value)})} />
              </div>
              <button onClick={applyConfig} className="btn btn-primary" style={{ marginTop: '10px', padding: '10px' }}>SAVE CONFIG</button>
            </div>

            {/* SHOOTING RANGE PANEL - SYNS BARA OM BÅDA ÄR IKRYSsade */}
            {isCommunityScale && showShootingRange && (
              <div className="panel" style={{ borderLeft: '3px solid var(--range-color)' }}>
                <div className="panel-header" style={{ color: 'var(--range-color)' }}><BoxSelect size={18} /> SHOOTING RANGE</div>
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '1.2rem', fontWeight: 'bold', marginBottom: '10px' }}>
                  <span>X: {(shootingRange.x * scaleMult).toFixed(1)}</span>
                  <span>Y: {(shootingRange.y * scaleMult).toFixed(1)}</span>
                </div>
                <div className="input-group">
                  <span style={{ fontSize: '0.8rem', color: '#888' }}>Size W</span>
                  <input type="number" step="0.1" value={shootingRange.w} onChange={e => setShootingRange({...shootingRange, w: parseFloat(e.target.value) || 0})} />
                </div>
                <div className="input-group">
                  <span style={{ fontSize: '0.8rem', color: '#888' }}>Size H</span>
                  <input type="number" step="0.1" value={shootingRange.h} onChange={e => setShootingRange({...shootingRange, h: parseFloat(e.target.value) || 0})} />
                </div>
              </div>
            )}

            {/* MY POSITION PANEL - SYNS BARA OM BÅDA ÄR IKRYSsade */}
            {isCommunityScale && showUserPos && (
              <div className="panel" style={{ borderLeft: '3px solid var(--user-color)' }}>
                <div className="panel-header" style={{ color: 'var(--user-color)' }}><User size={18} /> MY POSITION</div>
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '1.2rem', fontWeight: 'bold' }}>
                  <span>X: {(userPos.x * scaleMult).toFixed(1)}</span>
                  <span>Y: {(userPos.y * scaleMult).toFixed(1)}</span>
                </div>
              </div>
            )}

            <div className="panel" style={{ display: 'flex', flexDirection: 'column', gap: '10px' }}>
              <button onClick={() => changeMode('DETECTING')} disabled={mode === 'CALIBRATING'} className={`btn ${mode === 'DETECTING' ? 'btn-primary' : mode === 'CALIBRATING' ? 'btn-neutral' : 'btn-primary'}`} style={{ background: mode === 'IDLE' ? 'var(--primary)' : undefined, border: mode === 'DETECTING' ? 'none' : '1px solid var(--primary)', color: mode === 'IDLE' ? '#000' : undefined }}>
                {mode === 'DETECTING' ? <Square size={18} /> : <Play size={18} />}
                {mode === 'DETECTING' ? 'STOP SYSTEM' : 'START SYSTEM'}
              </button>
            </div>

            <div className="panel" style={{ flexGrow: 1, display: 'flex', flexDirection: 'column' }}>
              <div className="panel-header" style={{ justifyContent: 'space-between' }}>
                <span>EVENT LOG</span>
                <Trash2 size={16} style={{ cursor: 'pointer', opacity: 0.7 }} onClick={() => setEvents([])}/>
              </div>
              <div className="log-container">
                {events.length === 0 && <div style={{ textAlign: 'center', color: '#444', marginTop: '20px' }}>No activity...</div>}
                {events.map((e, i) => (
                  <div key={e.id} className={`log-item ${i === 0 ? 'latest' : ''}`}>
                    <div>
                      <div style={{ fontWeight: 'bold', color: e.label === "Scanning..." ? '#ffff00' : '#fff' }}>#{e.id} {e.label.toUpperCase()}</div>
                      <div className="log-nodes">NODES: {e.nodes.join(', ')}</div>
                    </div>
                    <div style={{ textAlign: 'right' }}>
                      <div style={{ color: 'var(--primary)' }}>{(e.x * scaleMult).toFixed(1)} {isCommunityScale?'m':'m'}</div>
                      <div style={{ color: 'var(--primary)' }}>{(e.y * scaleMult).toFixed(1)} {isCommunityScale?'m':'m'}</div>
                    </div>
                  </div>
                ))}
              </div>
            </div>
          </div>

          {/* RADAR MAP */}
          <div 
            className={`map-wrapper ${mode === 'DETECTING' ? 'detecting' : mode === 'CALIBRATING' ? 'calibrating' : ''}`}
            ref={mapContainerRef} 
          >
            <div 
              className="room-physics" 
              ref={roomRef}
              style={{ 
                width: `${roomDims.w}px`, 
                height: `${roomDims.h}px`,
                backgroundImage: isCommunityScale ? "url('/karta.jpg')" : 'none'
              }}
            >
              <div className="grid-lines"></div>
              <div className="cross-center"></div>

              {/* HÖRNEN (Noderna) */}
              {[1,2,3,4].map(n => (
                 <div key={n} style={{
                   position: 'absolute', width: '24px', height: '24px', background: '#222', border: '1px solid #555', color: '#fff', fontWeight:'bold', display: 'flex', justifyContent: 'center', alignItems: 'center', fontSize: '10px', zIndex: 15,
                   top: n > 2 ? '0' : 'auto', bottom: n <= 2 ? '0' : 'auto', left: n === 2 || n === 3 ? '100%' : '0', transform: 'translate(-50%, -50%)' 
                 }}>N{n}</div>
              ))}

              {/* SHOOTING RANGE MARKER */}
              {isCommunityScale && showShootingRange && (
                <div 
                  className={`shooting-range ${isDraggingRange ? 'dragging' : ''}`}
                  onPointerDown={(e) => handlePointerDown(e, 'range')}
                  style={{ 
                    left: `${(shootingRange.x / config.width) * 100}%`, 
                    top: `${(1 - shootingRange.y / config.height) * 100}%`,
                    width: `${(shootingRange.w / config.width) * 100}%`,
                    height: `${(shootingRange.h / config.height) * 100}%`
                  }}
                >
                  <div className="range-label">SHOOTING RANGE</div>
                </div>
              )}

              {/* USER MARKER */}
              {isCommunityScale && showUserPos && (
                <div 
                  className={`user-marker ${isDraggingUser ? 'dragging' : ''}`}
                  onPointerDown={(e) => handlePointerDown(e, 'user')}
                  style={{ 
                    left: `${(userPos.x / config.width) * 100}%`, 
                    top: `${(1 - userPos.y / config.height) * 100}%`
                  }}
                  title="Detta är din position!"
                >
                  <User size={20} color="var(--user-color)" />
                </div>
              )}

              {/* EVENT MARKERS */}
              {events.map((e, index) => (
                <div key={e.id} className={`shot-marker ${index === 0 ? 'latest' : 'old'}`}
                  style={{ 
                    left: `${(e.x / config.width) * 100}%`, 
                    top: `${(1 - e.y / config.height) * 100}%`
                  }}
                >
                  {index === 0 && <div className="ripple"></div>}
                  {index === 0 && (
                    <div className="shot-label" style={{ borderColor: e.label === "Scanning..." ? "yellow" : "var(--primary)" }}>{e.label}</div>
                  )}
                </div>
              ))}
            </div>
            
            {/* Info overlay nedre högra hörnet */}
            <div style={{position:'absolute', bottom: 10, right: 10, color: isCommunityScale ? '#fff' : '#444', textShadow: isCommunityScale ? '0 0 4px #000' : 'none', fontSize:'10px', pointerEvents:'none', textAlign:'right'}}>
               <div>SCALE: {(roomDims.pxPerMeter / scaleMult).toFixed(1)} PX/UNIT</div>
               <div>AREA: {(config.width * scaleMult).toFixed(0)}x{(config.height * scaleMult).toFixed(0)} UNITS</div>
            </div>

          </div>
        </div>
      </div>
    </>
  );
}