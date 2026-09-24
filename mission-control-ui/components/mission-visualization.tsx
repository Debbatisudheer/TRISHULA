"use client";

import { useMemo } from "react";

export const missionPhases = [
  "Launch", "Earth Orbit", "Trans-Lunar Injection", "Cruise to Moon",
  "Lunar Orbit", "Descent", "Landing", "Rover Deployment", "Surface Operations", "Complete"
];

type Props = {
  phase: string;
  live: boolean;
  telemetry?: Record<string, string>;
  telemetryHistory?: Record<string, string>[];
};

type Scene = {
  badge: string;
  trajectory?: string;
  completedPath?: string;
  realPath?: string;
  currentPath?: string;
  trajectoryDots?: { x: number; y: number; kind: "completed" | "current" }[];
  orbit?: { cx: number; cy: number; rx: number; ry: number };
  landingSite?: boolean;
  surface?: boolean;
  rocket?: { x: number; y: number; scale: number };
  spacecraft?: { x: number; y: number; rotate: number };
  lander?: { x: number; y: number };
  rover?: { x: number; y: number };
};


export function MissionVisualization({ phase, live, telemetry = {}, telemetryHistory = [] }: Props) {
  const t = 0.45;
  const phaseIndex = Math.max(0, missionPhases.findIndex(p => p.toLowerCase() === phase.toLowerCase()));


  const scene = useMemo(() => sceneForPhase(phase, phaseIndex, t, telemetry, telemetryHistory), [phase, phaseIndex, t, telemetry, telemetryHistory]);
  const linkText = live ? "LIVE" : "OFFLINE";
  const telemetryText = telemetry.mode ?? telemetry.mission_phase ?? "NO LIVE MODE";
  const spacecraftX = Number(telemetry.position_x_m ?? telemetry.x_m ?? "NaN");
  const spacecraftY = Number(telemetry.position_y_m ?? telemetry.y_m ?? "NaN");
  const spacecraftPosition = Number.isFinite(spacecraftX) && Number.isFinite(spacecraftY) ? `${spacecraftX.toFixed(1)}, ${spacecraftY.toFixed(1)} m` : "POSITION N/A";

  return (
    <div className="visual panel">
      <svg viewBox="0 0 1100 520" role="img" aria-label={`TRISHULA ${phase} mission visualization`} preserveAspectRatio="xMidYMid slice">
        <defs>
          <radialGradient id="spaceV3" cx="54%" cy="46%"><stop offset="0" stopColor="#0b2236"/><stop offset=".52" stopColor="#020a13"/><stop offset="1" stopColor="#010408"/></radialGradient>
          <radialGradient id="earthV3" cx="38%" cy="30%"><stop offset="0" stopColor="#8bd7ff"/><stop offset=".25" stopColor="#327fb8"/><stop offset=".62" stopColor="#0b3557"/><stop offset="1" stopColor="#020b14"/></radialGradient>
          <radialGradient id="moonV3" cx="30%" cy="26%"><stop offset="0" stopColor="#f6f7f8"/><stop offset=".4" stopColor="#b8bdc2"/><stop offset=".78" stopColor="#696f75"/><stop offset="1" stopColor="#292d31"/></radialGradient>
          <linearGradient id="panelV3" x1="0" x2="1"><stop stopColor="#092b47"/><stop offset=".5" stopColor="#2c82ad"/><stop offset="1" stopColor="#06243c"/></linearGradient>
          <linearGradient id="transferV3" x1="0" x2="1"><stop stopColor="#35e39a"/><stop offset=".42" stopColor="#58cfff"/><stop offset="1" stopColor="#8cddff"/></linearGradient>
          <filter id="glowV3"><feGaussianBlur stdDeviation="7" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
          <filter id="shipV3"><feGaussianBlur stdDeviation="2.5" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
          <pattern id="starsV3" width="120" height="90" patternUnits="userSpaceOnUse">
            <circle cx="10" cy="18" r=".7" fill="#fff"/><circle cx="42" cy="63" r=".45" fill="#fff"/><circle cx="77" cy="24" r=".55" fill="#b7e7ff"/><circle cx="105" cy="48" r=".7" fill="#fff"/><circle cx="92" cy="82" r=".35" fill="#91cfff"/>
          </pattern>
          <clipPath id="earthClipV3"><circle cx="145" cy="350" r="210"/></clipPath>
          <clipPath id="moonClipV3"><circle cx="915" cy="172" r="108"/></clipPath>
        </defs>
        <rect width="1100" height="520" fill="url(#spaceV3)"/>
        <rect width="1100" height="520" fill="url(#starsV3)" opacity=".9"/>

        <g className="earth-v3">
          <circle cx="145" cy="350" r="210" fill="url(#earthV3)" stroke="#6ccfff" strokeOpacity=".62" strokeWidth="2"/>
          <g clipPath="url(#earthClipV3)" opacity=".8">
            <path d="M-80 235 C10 205 30 245 85 218 C135 192 160 226 205 202 C250 180 300 205 365 172 L400 560 L-80 560Z" fill="#16445f"/>
            <path d="M-60 300 C10 265 54 310 101 278 C142 250 185 296 226 269 C265 243 315 278 370 250 L390 560 L-60 560Z" fill="#2d6b83" opacity=".75"/>
            <path d="M-30 382 C35 342 80 385 128 352 C177 318 224 364 282 326 C320 301 354 320 392 306 L392 560 L-30 560Z" fill="#3e788e" opacity=".46"/>
            <path d="M-20 270 C35 307 74 321 120 302 C162 284 190 244 232 264 C273 283 292 318 356 308" fill="none" stroke="#d9f4ff" strokeOpacity=".3" strokeWidth="5"/>
            <ellipse cx="90" cy="352" rx="200" ry="208" fill="#02070c" opacity=".45"/>
            <path d="M-40 285 C35 250 105 255 178 286 C235 309 286 304 352 280" fill="none" stroke="#d9f6ff" strokeOpacity=".16" strokeWidth="7"/>
            <path d="M-20 420 C45 392 120 398 188 421 C246 440 295 434 350 413" fill="none" stroke="#d9f6ff" strokeOpacity=".12" strokeWidth="6"/>
          </g>
        </g>
        <text x="70" y="92" className="svg-label">EARTH</text><text x="70" y="111" className="svg-sub-label">MISSION ORIGIN</text>

        <g className="moon-v3">
          <circle cx="915" cy="172" r="108" fill="url(#moonV3)" stroke="#f1f4f6" strokeOpacity=".42"/>
          <g clipPath="url(#moonClipV3)" opacity=".52">
            <circle cx="862" cy="126" r="16" fill="#656a70"/><circle cx="936" cy="120" r="23" fill="#686d72"/><circle cx="965" cy="178" r="14" fill="#5b6066"/><circle cx="887" cy="212" r="10" fill="#5b6067"/><circle cx="841" cy="188" r="12" fill="#73787d"/><circle cx="928" cy="165" r="8" fill="#555a60"/><circle cx="980" cy="142" r="7" fill="#777c81"/><circle cx="900" cy="151" r="6" fill="#8a8f94"/><circle cx="954" cy="218" r="6" fill="#51565c"/>
          </g>
        <path d="M830 116 C880 84 944 82 990 111" fill="none" stroke="#fff" strokeOpacity=".12" strokeWidth="4"/>
          <path d="M835 226 C895 250 954 244 1000 210" fill="none" stroke="#fff" strokeOpacity=".08" strokeWidth="5"/>
        </g>
        <text x="897" y="35" className="svg-label">MOON</text><text x="875" y="54" className="svg-sub-label">LUNAR TARGET</text>

        {scene.realPath && <path d={scene.realPath} fill="none" stroke="#35e39a" strokeOpacity=".9" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"/>}
        {scene.currentPath && <path d={scene.currentPath} fill="none" stroke="#67d8ff" strokeWidth="3" strokeDasharray="8 7" strokeLinecap="round"/>}
        {scene.trajectoryDots?.map((point, index) => (
          <circle key={`trajectory-dot-${index}`} cx={point.x} cy={point.y} r="4.5" fill={point.kind === "current" ? "#67d8ff" : "#35e39a"} stroke="#06131e" strokeWidth="1.2"/>
        ))}
        <g className="trajectory-legend">
          <line x1="28" y1="490" x2="58" y2="490" stroke="#35e39a" strokeWidth="3" strokeLinecap="round"/>
          <text x="66" y="493">COMPLETED PHYSICAL TRAJECTORY</text>
          <line x1="270" y1="490" x2="300" y2="490" stroke="#67d8ff" strokeWidth="3" strokeDasharray="8 7"/>
          <text x="308" y="493">CURRENT SEGMENT</text>
        </g>

        {scene.orbit && <ellipse cx={scene.orbit.cx} cy={scene.orbit.cy} rx={scene.orbit.rx} ry={scene.orbit.ry} fill="none" stroke="#65c8f7" strokeOpacity=".52" strokeWidth="2" strokeDasharray="5 8"/>}
        {scene.landingSite && <g><circle cx="915" cy="294" r="6" fill="#f3bf4d" filter="url(#glowV3)"/><circle cx="915" cy="294" r="22" fill="none" stroke="#f3bf4d" strokeOpacity=".35"/><text x="942" y="299" className="svg-marker">LANDING SITE</text></g>}
        {scene.surface && <path d="M760 350 C825 334 895 342 970 329 C1030 319 1070 334 1120 320 L1120 520 L760 520Z" fill="#252a2e" opacity=".85"/>}

        {scene.rocket && <Rocket x={scene.rocket.x} y={scene.rocket.y} scale={scene.rocket.scale}/>} 
        {scene.spacecraft && <Spacecraft x={scene.spacecraft.x} y={scene.spacecraft.y} rotate={scene.spacecraft.rotate}/>} 
        {scene.lander && <Lander x={scene.lander.x} y={scene.lander.y}/>} 
        {scene.rover && <Rover x={scene.rover.x} y={scene.rover.y}/>} 

        <g className="scene-badge"><rect x="26" y="30" width="175" height="29" rx="5"/><text x="39" y="49">{scene.badge}</text></g>
      </svg>
      <div className="mission-status-overlay" aria-label="Current mission status">
        <div className="mission-status-title">TRISHULA-1</div>
        <div className="mission-status-cell"><span>PHASE</span><b>{phase}</b></div>
        <div className="mission-status-cell"><span>LINK</span><b className={live ? "status-good" : "status-bad"}>{linkText}</b></div>
        <div className="mission-status-cell"><span>MODE</span><b>{telemetryText}</b></div>
        <div className="mission-status-cell position"><span>POSITION</span><b>{spacecraftPosition}</b></div>
      </div>
      <div className="telemetry-status-chip">
        <span className={live ? "status-pulse" : "status-pulse offline"}></span>
        <span>PHYSICAL TELEMETRY</span>
        <b>{live ? "CONNECTED" : "DISCONNECTED"}</b>
        <em>·</em>
        <b>{phase.toUpperCase()}</b>
      </div>
    </div>
  );
}

function sceneForPhase(phase: string, i: number, t: number, telemetry: Record<string, string>, history: Record<string, string>[]): Scene {
  const physicalX = Number(telemetry.position_x_m ?? telemetry.x_m ?? "NaN");
  const physicalY = Number(telemetry.position_y_m ?? telemetry.y_m ?? "NaN");
  const moonX = Number(telemetry.moon_x_m ?? "NaN");
  const moonY = Number(telemetry.moon_y_m ?? "NaN");
  const hasPhysicalPosition = Number.isFinite(physicalX) && Number.isFinite(physicalY);
  const hasMoonPosition = Number.isFinite(moonX) && Number.isFinite(moonY);

  // The physical coordinates remain authoritative. The mission view is a
  // readability projection: Earth orbit is expanded so real orbital motion is
  // visible, while Earth-to-Moon transfer distance is compressed into the
  // available viewport. No future path is generated.
  const EARTH = { x: 145, y: 350 };
  const MOON = { x: 915, y: 172 };
  const EARTH_RADIUS_M = 6.371e6;
  const ORBIT_VIEW_MAX_M = 25e6;
  const moonDistance = hasMoonPosition ? Math.max(1, Math.hypot(moonX, moonY)) : 384.4e6;
  const moonAngle = hasMoonPosition ? Math.atan2(moonY, moonX) : Math.atan2(MOON.y - EARTH.y, MOON.x - EARTH.x);
  const transferAngle = Math.atan2(MOON.y - EARTH.y, MOON.x - EARTH.x);

  const mapPhysical = (x: number, y: number) => {
    const r = Math.hypot(x, y);
    const theta = Math.atan2(y, x);

    if (r <= ORBIT_VIEW_MAX_M) {
      const normalized = Math.max(0, Math.min(1, (r - EARTH_RADIUS_M) / (ORBIT_VIEW_MAX_M - EARTH_RADIUS_M)));
      const viewRadius = 175 + normalized * 80;
      return {
        x: EARTH.x + Math.cos(theta) * viewRadius,
        y: EARTH.y - Math.sin(theta) * viewRadius,
      };
    }

    const fraction = Math.max(0, Math.min(1, (r - ORBIT_VIEW_MAX_M) / Math.max(1, moonDistance - ORBIT_VIEW_MAX_M)));
    const start = { x: 330, y: EARTH.y };
    const end = { x: MOON.x - 115, y: MOON.y };
    const baseX = start.x + (end.x - start.x) * fraction;
    const baseY = start.y + (end.y - start.y) * fraction;
    let delta = theta - moonAngle;
    while (delta > Math.PI) delta -= 2 * Math.PI;
    while (delta < -Math.PI) delta += 2 * Math.PI;
    const crossTrack = Math.max(-55, Math.min(55, delta * 45));
    return {
      x: baseX - Math.sin(transferAngle) * crossTrack,
      y: baseY - Math.cos(transferAngle) * crossTrack,
    };
  };

  const physicalPoints = history
    .map(f => ({ x: Number(f.position_x_m ?? f.x_m ?? "NaN"), y: Number(f.position_y_m ?? f.y_m ?? "NaN") }))
    .filter(p => Number.isFinite(p.x) && Number.isFinite(p.y));
  if (hasPhysicalPosition) {
    const last = physicalPoints[physicalPoints.length - 1];
    if (!last || Math.abs(last.x - physicalX) > 0.01 || Math.abs(last.y - physicalY) > 0.01) physicalPoints.push({ x: physicalX, y: physicalY });
  }

  if (hasPhysicalPosition) {
    const screenPoints = physicalPoints.map(p => mapPhysical(p.x, p.y));
    const spacecraft = mapPhysical(physicalX, physicalY);
    const currentCount = Math.min(8, Math.max(2, Math.ceil(screenPoints.length * 0.2)));
    const split = Math.max(1, screenPoints.length - currentCount);
    const completedPoints = screenPoints.slice(0, split + 1);
    const currentPoints = screenPoints.slice(Math.max(0, split));
    const makePath = (points: {x:number;y:number}[]) => points.length > 1
      ? points.map((point, n) => `${n ? "L" : "M"}${point.x.toFixed(1)} ${point.y.toFixed(1)}`).join(" ")
      : undefined;
    const dots: {x:number;y:number;kind:"completed"|"current"}[] = [];
    const completedStep = Math.max(1, Math.floor(completedPoints.length / 24));
    for (let n = 0; n < completedPoints.length; n += completedStep) dots.push({ ...completedPoints[n], kind: "completed" });
    const currentStep = Math.max(1, Math.floor(currentPoints.length / 6));
    for (let n = 0; n < currentPoints.length; n += currentStep) dots.push({ ...currentPoints[n], kind: "current" });
    const last = screenPoints[screenPoints.length - 1];
    if (last) dots.push({ ...last, kind: "current" });
    const moonPoint = hasMoonPosition ? mapPhysical(moonX, moonY) : MOON;
    return {
      badge: `PHYSICAL ${phase.toUpperCase()} · REAL TELEMETRY`,
      realPath: makePath(completedPoints),
      currentPath: makePath(currentPoints),
      trajectoryDots: dots,
      orbit: Math.hypot(physicalX, physicalY) <= ORBIT_VIEW_MAX_M ? { cx: EARTH.x, cy: EARTH.y, rx: 255, ry: 255 } : undefined,
      spacecraft: { x: spacecraft.x, y: spacecraft.y, rotate: 0 },
      landingSite: phase === "Landing" || phase === "Rover Deployment" || phase === "Surface Operations",
      lander: phase === "Landing" || phase === "Rover Deployment" || phase === "Surface Operations" ? { x: moonPoint.x, y: moonPoint.y + 105 } : undefined,
      surface: phase === "Surface Operations" || phase === "Complete"
    };
  }

  // No physical position means no spacecraft trajectory is drawn.
  return {
    badge: `${phase.toUpperCase()} · PHYSICAL TELEMETRY REQUIRED`,
    orbit: i === 1 ? { cx: 145, cy: 350, rx: 270, ry: 78 } : undefined
  };
}

function Spacecraft({x,y,rotate}:{x:number;y:number;rotate:number}) { return <g transform={`translate(${x-30} ${y-20}) rotate(${rotate} 30 20)`} filter="url(#shipV3)"><rect x="19" y="13" width="26" height="13" rx="3" fill="#dbe3e8" stroke="#f4fbff"/><rect x="2" y="9" width="15" height="21" fill="url(#panelV3)" stroke="#70d0ff"/><rect x="47" y="9" width="15" height="21" fill="url(#panelV3)" stroke="#70d0ff"/><path d="M32 26 l-8 14 h16z" fill="#d6a24a"/><circle cx="32" cy="16" r="3.5" fill="#7dd7ff"/><path d="M61 19 l11 -5 M61 21 l11 5" stroke="#8ddcff"/></g>; }
function Rocket({x,y,scale}:{x:number;y:number;scale:number}) { return <g transform={`translate(${x-18} ${y-55}) scale(${scale})`} filter="url(#shipV3)"><path d="M18 0 C31 15 36 38 31 58 L5 58 C0 38 5 15 18 0Z" fill="#d9e1e7" stroke="#f5fbff"/><path d="M5 34 L-8 51 L6 48 M31 34 L44 51 L30 48" fill="#7b8991"/><circle cx="18" cy="21" r="7" fill="#77b6d6" stroke="#eef9ff"/><path d="M10 58 L18 92 L26 58Z" fill="#f4b33e"/><path d="M14 59 L18 82 L22 59Z" fill="#fff0a6"/></g>; }
function Lander({x,y}:{x:number;y:number}) { return <g transform={`translate(${x-30} ${y-28})`} filter="url(#shipV3)"><rect x="10" y="8" width="40" height="28" rx="5" fill="#c7cdd0" stroke="#f2f6f8"/><rect x="17" y="14" width="26" height="10" fill="#243b4c"/><path d="M10 31 L0 52 M50 31 L60 52 M16 36 L8 55 M44 36 L52 55" stroke="#aeb8bd" strokeWidth="4"/><circle cx="30" cy="20" r="4" fill="#5ac5f5"/></g>; }
function Rover({x,y}:{x:number;y:number}) { return <g transform={`translate(${x-42} ${y-22})`} filter="url(#shipV3)"><rect x="12" y="5" width="58" height="28" rx="5" fill="#c8ced1" stroke="#eef5f8"/><rect x="27" y="-18" width="7" height="23" fill="#aab4ba"/><rect x="30" y="-24" width="30" height="6" fill="#173f58" stroke="#75cfff"/><circle cx="18" cy="38" r="10" fill="#242b30" stroke="#9da7ac"/><circle cx="62" cy="38" r="10" fill="#242b30" stroke="#9da7ac"/><circle cx="40" cy="38" r="10" fill="#242b30" stroke="#9da7ac"/></g>; }
