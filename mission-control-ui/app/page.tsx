"use client";

import { useEffect, useMemo, useState } from "react";
import { Activity, AlertTriangle, Antenna, Battery, Bot, Database, Gauge, Pause, Play, Radio, RotateCcw, Satellite, ShieldCheck, Square, Wifi, Zap } from "lucide-react";
import { loadMissionData, loadRuntime, controlRuntime, loadCommands, dispatchCommand, wsUrl, type Alerts, type Dashboard, type Events, type RuntimeResponse, type State, type Telemetry, type CommandsResponse } from "../lib/api";
import { MissionVisualization, missionPhases } from "../components/mission-visualization";

const nav = ["Mission", "Spacecraft", "Vikram", "Rover", "Telemetry", "Commands", "Alerts", "Ground Station", "Data", "System"] as const;
const implementedNav = new Set(nav);

const fallbackDashboard: Dashboard = { mission_count: 0, summary: {} };
const fallbackState: State = { mission_count: 0, missions: [] };
const fallbackAlerts: Alerts = { active_count: 0, total_count: 0, alerts: [] };

export default function Home() {
  const [dashboard, setDashboard] = useState<Dashboard>(fallbackDashboard);
  const [state, setState] = useState<State>(fallbackState);
  const [alerts, setAlerts] = useState<Alerts>(fallbackAlerts);
  const [telemetry, setTelemetry] = useState<Telemetry>({ count: 0, records: [] });
  const [events, setEvents] = useState<Events>({ count: 0, records: [] });
  const [live, setLive] = useState(false);
  const [activeNav, setActiveNav] = useState<(typeof nav)[number]>("Spacecraft");
  const [lastUpdate, setLastUpdate] = useState<Date | null>(null);
  const [clock, setClock] = useState("");
  const [runtime, setRuntime] = useState<RuntimeResponse | null>(null);
  const [runtimeBusy, setRuntimeBusy] = useState(false);
  const [runtimeError, setRuntimeError] = useState<string | null>(null);
  const [commands, setCommands] = useState<CommandsResponse>({ mission_id: "TRISHULA", count: 0, limit: 50, commands: [] });
  const [commandError, setCommandError] = useState<string | null>(null);
  const [visualPhase, setVisualPhase] = useState("Surface Operations");

  async function refreshRuntime() {
    try {
      const data = await loadRuntime();
      setRuntime(data);
      setRuntimeError(null);
    } catch (error) {
      setRuntimeError(error instanceof Error ? error.message : "Runtime unavailable");
    }
  }

  async function handleRuntimeAction(action: "start" | "pause" | "resume" | "abort" | "reset") {
    setRuntimeBusy(true);
    setRuntimeError(null);
    try {
      const data = await controlRuntime(action);
      setRuntime(data);
      await refresh();
    } catch (error) {
      setRuntimeError(error instanceof Error ? error.message : `Runtime ${action} failed`);
    } finally {
      setRuntimeBusy(false);
    }
  }

  async function refresh() {
    try {
      const data = await loadMissionData();
      setDashboard(data.dashboard);
      setState(data.state);
      setAlerts(data.alerts);
      setTelemetry(data.telemetry);
      setEvents(data.events);
      try { setCommands(await loadCommands()); } catch { /* command view remains last known state */ }
      setLive(true);
      setLastUpdate(new Date());
    } catch {
      setLive(false);
    }
  }

  useEffect(() => {
    setClock(new Date().toISOString().replace("T", " ").slice(0, 19) + " UTC");
    const clockTimer = window.setInterval(() => setClock(new Date().toISOString().replace("T", " ").slice(0, 19) + " UTC"), 1000);
    refresh();
    refreshRuntime();
    const timer = window.setInterval(refresh, 3000);
    const runtimeTimer = window.setInterval(refreshRuntime, 1000);
    let socket: WebSocket | undefined;
    try {
      socket = new WebSocket(wsUrl());
      socket.onopen = () => setLive(true);
      socket.onmessage = () => refresh();
      socket.onclose = () => setLive(false);
      socket.onerror = () => setLive(false);
    } catch { setLive(false); }
    return () => { window.clearInterval(timer); window.clearInterval(runtimeTimer); window.clearInterval(clockTimer); socket?.close(); };
  }, []);

  const mission = dashboard.missions?.[0];
  const activeAlerts = useMemo(() => (alerts.alerts ?? []).filter(a => a.status === "ACTIVE"), [alerts]);
  const latestTelemetry = telemetry.records?.[0];

  // A physical telemetry tick is represented by multiple GroundRecords: each
  // metric has its own sequence/timestamp, while all records for the same tick
  // share the correlation_id (for example PHYS-V0941-TICK-30). Merge the whole
  // tick instead of selecting only the newest metric record.
  const physicalTicks = useMemo(() => {
    const groups = new Map<string, { maxSequence: number; maxTimestamp: number; fields: Record<string, string> }>();
    for (const record of telemetry.records ?? []) {
      const sequence = Number(record.envelope?.sequence_number ?? 0);
      const timestamp = Number(record.envelope?.mission_timestamp_ns ?? 0);
      const missionSeconds = record.fields?.mission_time_seconds?.trim();
      // Physical telemetry ticks normally share correlation_id. Some ground
      // records may omit it, so mission_time_seconds is the deterministic
      // secondary tick key; only fall back to the record timestamp as a last resort.
      const correlation = record.envelope?.correlation_id?.trim()
        || (missionSeconds ? `mission-time:${missionSeconds}` : `timestamp:${timestamp}`);
      const existing = groups.get(correlation);
      groups.set(correlation, {
        maxSequence: Math.max(existing?.maxSequence ?? 0, sequence),
        maxTimestamp: Math.max(existing?.maxTimestamp ?? 0, timestamp),
        fields: { ...(existing?.fields ?? {}), ...(record.fields ?? {}) }
      });
    }
    return [...groups.values()].sort((a, b) => a.maxSequence - b.maxSequence);
  }, [telemetry.records]);

  const latestPhysicalTick = physicalTicks[physicalTicks.length - 1];
  const tickFields = latestPhysicalTick?.fields ?? {};
  const telemetrySource = latestTelemetry?.envelope?.source_node;
  const currentStateMission = state.missions?.find(m => m.mission_id === "TRISHULA");
  const vehicle = mission?.vehicles?.find(v => v.source_node === telemetrySource) ?? mission?.vehicles?.[0];
  const vehicleState = currentStateMission?.vehicles?.find(v => v.source_node === telemetrySource) ?? currentStateMission?.vehicles?.[0];
  const metricValues = vehicleState?.metrics ?? {};
  const firstMetric = Object.values(metricValues)[0];

  // The ground state engine already assembles all metric records belonging to
  // the current physical tick. Use it as the authoritative completion layer
  // and only use raw telemetry fields where they are available. This prevents
  // a single PHYS-V0941 record (for example phase_progress) from hiding the
  // position/speed/altitude records belonging to the same physical tick.
  const stateFields = Object.fromEntries(
    Object.entries(metricValues).map(([name, metric]) => [name, metric.value ?? ""])
  ) as Record<string, string>;
  const latestFields: Record<string, string> = { ...stateFields, ...tickFields };
  const telemetryPhase = latestFields.mission_phase || latestFields.phase || (latestFields.mode === "SURFACE_READY" ? "Surface Operations" : visualPhase);
  const normalizedTelemetryPhase = telemetryPhase.toLowerCase() === "complete" ? "Complete" : telemetryPhase;
  const resolvedMissionPhase = missionPhases.includes(normalizedTelemetryPhase) ? normalizedTelemetryPhase : visualPhase;
  const missionPhase = resolvedMissionPhase;
  const physicalHistory = physicalTicks.map(tick => tick.fields);
  const battery = runtime?.runtime.battery_soc != null
    ? `${(runtime.runtime.battery_soc * 100).toFixed(2)}%`
    : (latestFields.battery_soc ?? "—");
  const velocity = latestFields.speed_m_per_s ?? latestFields.speed_m_s ?? "—";
  const position = latestFields.position_x_m && latestFields.position_y_m ? `${Number(latestFields.position_x_m).toFixed(1)}, ${Number(latestFields.position_y_m).toFixed(1)} m` : (latestFields.x_m && latestFields.y_m ? `${latestFields.x_m}, ${latestFields.y_m} m` : "—");
  const heading = latestFields.heading_rad ?? "—";
  const eventFeed = useMemo(() => {
    const eventItems = (events.records ?? []).map(r => ({
      time: r.envelope?.mission_timestamp_ns ? new Date(Number(r.envelope.mission_timestamp_ns) / 1_000_000).toLocaleTimeString() : "—",
      kind: "EVENT",
      text: r.fields?.message ?? r.fields?.event_type ?? r.envelope?.record_id ?? "Event received"
    }));
    const alertItems = (alerts.alerts ?? []).map(a => ({
      time: a.updated_at ? new Date(a.updated_at).toLocaleTimeString() : "—",
      kind: "ALERT",
      text: `${a.severity} · ${a.event_type}`
    }));
    return [...eventItems, ...alertItems].slice(0, 8);
  }, [events.records, alerts.alerts]);

  return (
    <main className="shell">
      <header className="topbar">
        <div className="brand"><div className="brand-mark"><Satellite size={28}/></div><div><div className="brand-name">TRISHULA</div><div className="brand-sub">LUNAR EXPLORATION MISSION</div></div></div>
        <nav>{nav.map(item => {
          const enabled = implementedNav.has(item);
          return <button key={item} type="button" className={activeNav === item ? "nav active" : enabled ? "nav" : "nav nav-pending"} disabled={!enabled} title={enabled ? `Open ${item} operations` : `${item} integration is the next Mission Control module`} onClick={() => enabled && setActiveNav(item)}>{item}</button>;
        })}<span className="nav-developer">Sudheer.Dev</span></nav>
        <div className="top-status"><span className={live ? "live-dot" : "live-dot offline"}/>{live ? "LIVE" : "OFFLINE"}</div>
        <div className="clock"><strong>{clock || "—"}</strong><span>Mission Control</span></div>
      </header>

      {activeNav === "Mission" ? (
        <MissionOverview
          mission={mission}
          missionPhase={missionPhase}
          runtime={runtime}
          dashboard={dashboard}
          state={state}
          telemetry={telemetry}
          alerts={alerts}
          events={events}
          live={live}
          lastUpdate={lastUpdate}
          activeAlerts={activeAlerts}
          eventFeed={eventFeed}
        />
      ) : activeNav === "Vikram" ? (
        <VikramOverview state={state} dashboard={dashboard} telemetry={telemetry} alerts={alerts} events={events} live={live} lastUpdate={lastUpdate} />
      ) : activeNav === "Rover" ? (
        <RoverOverview state={state} dashboard={dashboard} telemetry={telemetry} alerts={alerts} events={events} live={live} lastUpdate={lastUpdate} />
      ) : activeNav === "Telemetry" ? (
        <TelemetryOverview telemetry={telemetry} state={state} runtime={runtime} dashboard={dashboard} live={live} lastUpdate={lastUpdate} />
      ) : activeNav === "Commands" ? (
        <CommandsOverview commands={commands} onRefresh={async () => { try { setCommands(await loadCommands()); setCommandError(null); } catch (e) { setCommandError(e instanceof Error ? e.message : "Command view unavailable"); } }} onError={setCommandError} />
      ) : activeNav === "Alerts" ? (
        <AlertsOverview alerts={alerts} events={events} live={live} lastUpdate={lastUpdate} />
      ) : activeNav === "Ground Station" ? (
        <GroundStationOverview dashboard={dashboard} state={state} telemetry={telemetry} events={events} alerts={alerts} runtime={runtime} live={live} lastUpdate={lastUpdate} />
      ) : activeNav === "Data" ? (
        <DataOverview dashboard={dashboard} state={state} telemetry={telemetry} events={events} commands={commands} alerts={alerts} live={live} lastUpdate={lastUpdate} />
      ) : activeNav === "System" ? (
        <SystemOverview dashboard={dashboard} state={state} telemetry={telemetry} events={events} commands={commands} alerts={alerts} runtime={runtime} runtimeError={runtimeError} live={live} lastUpdate={lastUpdate} />
      ) : (
      <section className="workspace">
        <aside className="left-column">
          <Panel title="MISSION STATUS">
            {missionPhases.map((p, i) => {
              const current = i === missionPhases.indexOf(missionPhase);
              const done = i < missionPhases.indexOf(missionPhase);
              const detail = done || (current && p === "Complete") ? "Completed" : current ? "In Progress" : "Pending";
              return <Phase key={p} done={done || (current && p === "Complete")} current={current} label={`${i + 1}. ${p}`} detail={detail} />;
            })}
          </Panel>
          <Panel title="MISSION PARAMETERS">
            <MetricRow label="Mission" value={dashboard.missions?.[0]?.mission_id ?? "TRISHULA"} />
            <MetricRow label="Vehicle" value={vehicle?.source_node ?? "—"} />
            <MetricRow label="Last Sequence" value={String(vehicle?.last_sequence ?? "—")} />
            <MetricRow label="Battery" value={battery === "—" ? "Not supplied" : battery} />
            <MetricRow label="Speed" value={velocity === "—" ? "—" : `${velocity} m/s`} good={velocity !== "—"} />
            <MetricRow label="Position" value={position} good={position !== "—"} />
            <MetricRow label="Altitude" value={latestFields.altitude_m ? `${Number(latestFields.altitude_m).toFixed(1)} m` : "—"} good={Boolean(latestFields.altitude_m)} />
            <MetricRow label="Mission Health" value={mission?.health_status ?? "—"} good={mission?.health_status === "NOMINAL"} />
            <MetricRow label="Readiness" value={mission?.readiness ?? "—"} good={mission?.readiness === "READY"} />
            <MetricRow label="Freshness" value={mission?.freshness_status ?? "—"} />
            <MetricRow label="Physical Phase" value={missionPhase} good={Boolean(latestTelemetry)} />
            <MetricRow label="Telemetry Source" value={latestTelemetry?.envelope?.source_node ?? "—"} />
          </Panel>
          <Panel title="SPACECRAFT STATUS">
            <MetricRow label="Power" value={runtime?.runtime.power_status ?? (battery === "—" ? "Not supplied" : battery)} good={runtime?.runtime.power_status === "SUPPLIED"} />
            <MetricRow label="Propulsion" value={runtime?.runtime.propulsion_status ?? latestFields.propulsion_status ?? "—"} good={runtime?.runtime.propulsion_status === "COASTING" || runtime?.runtime.propulsion_status === "BURNING" || runtime?.runtime.propulsion_status === "RCS"} />
            <MetricRow label="Thermal" value={runtime?.runtime.thermal_temperature_c != null ? `${runtime.runtime.thermal_status} · ${runtime.runtime.thermal_temperature_c.toFixed(1)} °C` : (latestFields.temperature_c ?? latestFields.temperature ?? "—")} good={runtime?.runtime.thermal_status === "NOMINAL"} />
            <MetricRow label="Communication" value={runtime?.runtime.communication_status ?? (live ? "LOCKED" : "NO LINK")} good={runtime?.runtime.communication_status === "LOCKED" || live} />
            <MetricRow label="Navigation" value={runtime?.runtime.navigation_status ?? "—"} good={runtime?.runtime.navigation_status === "NOMINAL"} />
            <MetricRow label="Health" value={runtime?.runtime.health_status ?? vehicle?.health_status ?? "—"} good={runtime?.runtime.health_status === "NOMINAL" || vehicle?.health_status === "NOMINAL"} />
          </Panel>
        </aside>

        <section className="center-column">
          <MissionVisualization phase={missionPhase} live={live} telemetry={latestFields} telemetryHistory={physicalHistory} />
          <div className="timeline">
            {missionPhases.map((p, i) => <button className={i === missionPhases.indexOf(missionPhase) ? "timepoint current" : i < missionPhases.indexOf(missionPhase) ? "timepoint done" : "timepoint"} key={p} onClick={() => setVisualPhase(p)}><span className="node"/><b>{p === "Trans-Lunar Injection" ? "TLI" : p === "Rover Deployment" ? "Rover Deploy" : p}</b><small>{i < missionPhases.indexOf(missionPhase) ? "Complete" : i === missionPhases.indexOf(missionPhase) ? "Current phase" : "Preview"}</small></button>)}
          </div>
          <div className="media-grid">
            <Media title="LAUNCH VIEW" icon={<Zap/>} text="Physical mission telemetry · no synthetic motion" />
            <Media title="SPACECRAFT CAMERA" icon={<Satellite/>} text={latestFields.mode ? `Live state · ${latestFields.mode}` : "Waiting for spacecraft telemetry"} live={live} />
            <Media title="MOON PREVIEW" icon={<Activity/>} text="Lunar target · physical state integration" moon />
            <Media title="ROVER STATUS" icon={<Bot/>} text={`${vehicle?.source_node ?? "ROVER-01"} · ${vehicle?.has_command ? "COMMAND LINK" : "STANDBY"}`} />
          </div>
          <section className="panel physical-runtime">
            <PanelHeader title="PHYSICAL MISSION RUNTIME" right={<RuntimeStateBadge state={runtime?.runtime.state ?? "UNAVAILABLE"} />} />
            <div className="runtime-grid">
              <RuntimeItem label="RUNTIME STATE" value={runtime?.runtime.state ?? "—"} />
              <RuntimeItem label="CONTROL SEQUENCE" value={String(runtime?.runtime.control_sequence ?? "—")} />
              <RuntimeItem label="TELEMETRY SEQUENCE" value={String(runtime?.runtime.telemetry_sequence ?? "—")} />
              <RuntimeItem label="MISSION TIME" value={runtime?.runtime.mission_time_seconds != null ? `${runtime.runtime.mission_time_seconds.toFixed(3)} s` : "—"} />
              <RuntimeItem label="PHYSICAL PHASE" value={runtime?.runtime.mission_phase ?? latestFields.mission_phase ?? "—"} />
              <RuntimeItem label="ALTITUDE" value={runtime?.runtime.altitude_m != null ? `${runtime.runtime.altitude_m.toFixed(1)} m` : "—"} />
              <RuntimeItem label="SPEED" value={runtime?.runtime.speed_m_per_s != null ? `${runtime.runtime.speed_m_per_s.toFixed(2)} m/s` : "—"} />
              <RuntimeItem label="RUNTIME VERSION" value={runtime?.runtime_version ?? "—"} />
            </div>
            <div className="runtime-controls">
              <button className="runtime-btn start" disabled={runtimeBusy || runtime?.runtime.state === "RUNNING" || runtime?.runtime.state === "COMPLETE" || runtime?.runtime.state === "ABORTED"} onClick={() => handleRuntimeAction("start")}><Play size={14}/> START</button>
              <button className="runtime-btn" disabled={runtimeBusy || runtime?.runtime.state !== "RUNNING"} onClick={() => handleRuntimeAction("pause")}><Pause size={14}/> PAUSE</button>
              <button className="runtime-btn" disabled={runtimeBusy || runtime?.runtime.state !== "PAUSED"} onClick={() => handleRuntimeAction("resume")}><Play size={14}/> RESUME</button>
              <button className="runtime-btn abort" disabled={runtimeBusy || (runtime?.runtime.state !== "RUNNING" && runtime?.runtime.state !== "PAUSED")} onClick={() => handleRuntimeAction("abort")}><Square size={13}/> ABORT</button>
              <button className="runtime-btn reset" disabled={runtimeBusy || (runtime?.runtime.state !== "ABORTED" && runtime?.runtime.state !== "COMPLETE")} onClick={() => handleRuntimeAction("reset")}><RotateCcw size={14}/> RESET</button>
            </div>
            {runtimeError && <div className="runtime-error" role="alert">{runtimeError}</div>}
            <div className="runtime-note">Backend-controlled physical runtime. Controls call the Ground runtime API; the UI never advances mission state locally. Pause freezes physical stepping, Resume continues the same physical state, and Abort stops stepping.</div>
          </section>
          <section className="panel telemetry-panel"><PanelHeader title="LIVE TELEMETRY" right={lastUpdate ? `Updated ${lastUpdate.toLocaleTimeString()}` : "Waiting for telemetry"}/><div className="telemetry-grid">
            <TelemetryCard icon={<Battery/>} label="Battery / Power" value={battery} unit={battery === "—" ? "not supplied" : "%"} />
            <TelemetryCard icon={<Gauge/>} label="Speed" value={velocity} unit="m/s" />
            <TelemetryCard icon={<Activity/>} label="Altitude" value={latestFields.altitude_m ? Number(latestFields.altitude_m).toFixed(1) : "—"} unit="m" />
            <TelemetryCard icon={<Activity/>} label="Moon Distance" value={latestFields.distance_to_moon_m ? (Number(latestFields.distance_to_moon_m) / 1e6).toFixed(3) : "—"} unit="million m" />
            <TelemetryCard icon={<Gauge/>} label="Last Sequence" value={String(latestPhysicalTick?.maxSequence ?? vehicle?.last_sequence ?? "—")} unit="" />
            <TelemetryCard icon={<ShieldCheck/>} label="Quality / Limit" value={firstMetric?.quality_class ?? "—"} unit={firstMetric?.limit_status ?? ""} />
          </div></section>
        </section>

        <aside className="right-column">
          <Panel title="LIVE EVENT FEED" right={<span className="tiny-live"><span className="live-dot"/>LIVE</span>}>
            <FeedItem time="LIVE" text={live ? "Ground services connected" : "Waiting for ground service"} />
            <FeedItem time="API" text={`Dashboard ${dashboard.dashboard_version ?? "—"}`} />
            <FeedItem time="STATE" text={`${dashboard.mission_count ?? 0} mission(s) projected`} />
            <FeedItem time="DATA" text={`${dashboard.summary?.telemetry_accepted ?? 0} telemetry accepted`} />
            {eventFeed.slice(0, 3).map((e, i) => <FeedItem key={`${e.kind}-${i}`} time={e.time} text={e.text} warning={e.kind === "ALERT"} />)}
          </Panel>
          <Panel title="ALERT CENTER" right={<span className="alert-count">{alerts.active_count ?? 0} ACTIVE</span>}>
            {activeAlerts.slice(0, 5).map(a => <div className="alert" key={a.alert_id}><AlertTriangle size={15}/><div><b>{a.severity} · {a.event_type}</b><span>{a.message ?? a.subsystem ?? "Mission alert"}</span></div></div>)}
            {activeAlerts.length === 0 && <div className="empty">No active alerts</div>}
          </Panel>
          <Panel title="SYSTEM CONNECTIVITY">
            <Connection icon={<Radio/>} label="Ground Station" value={live ? "ONLINE" : "OFFLINE"} good={live}/>
            <Connection icon={<Wifi/>} label="WebSocket" value={live ? "LIVE" : "DISCONNECTED"} good={live}/>
            <Connection icon={<Database/>} label="Database / Ground API" value="READY" good/>
            <Connection icon={<Antenna/>} label="Kafka Event Stream" value="CONFIGURED" good/>
          </Panel>
          <Panel title="QUICK COMMANDS"><div className="command-grid">
            <button disabled title="Awaiting physical command path">ORIENT TO MOON</button>
            <button disabled title="Awaiting physical command path">TRAJECTORY CORRECTION</button>
            <button disabled title="Awaiting subsystem command path">SYSTEM CHECK</button>
            <button disabled title="Awaiting spacecraft camera command path">CAPTURE IMAGE</button>
            <button disabled title="Awaiting mission-data export command path">DOWNLOAD DATA</button>
            <button className="abort" disabled={runtimeBusy || (runtime?.runtime.state !== "RUNNING" && runtime?.runtime.state !== "PAUSED")} onClick={() => handleRuntimeAction("abort")}>ABORT MISSION</button>
          </div></Panel>
        </aside>
      </section>
      )}
      <footer><span>TRISHULA MISSION CONTROL</span><b>UI v0.4.11 · Runtime API v0.9.113.1</b><span className="footer-owner">Sudheer.Dev</span><span>From Earth to the Moon — Live Mission Operations</span></footer>
    </main>
  );
}

function MissionOverview({
  mission,
  missionPhase,
  runtime,
  dashboard,
  state,
  telemetry,
  alerts,
  events,
  live,
  lastUpdate,
  activeAlerts,
  eventFeed,
}: {
  mission?: NonNullable<Dashboard["missions"]>[number];
  missionPhase: string;
  runtime: RuntimeResponse | null;
  dashboard: Dashboard;
  state: State;
  telemetry: Telemetry;
  alerts: Alerts;
  events: Events;
  live: boolean;
  lastUpdate: Date | null;
  activeAlerts: NonNullable<Alerts["alerts"]>;
  eventFeed: Array<{time:string;kind:string;text:string}>;
}) {
  const missionState = state.missions?.find(m => m.mission_id === "TRISHULA");
  const vehicleCount = dashboard.summary?.vehicle_count ?? dashboard.mission_count ?? 0;
  const telemetryCount = dashboard.summary?.telemetry_accepted ?? telemetry.count ?? 0;
  const eventCount = dashboard.summary?.events_accepted ?? events.count ?? 0;
  const phaseIndex = Math.max(0, missionPhases.indexOf(missionPhase));
  const phaseProgress = runtime?.runtime.phase_progress ?? 0;
  const missionTime = runtime?.runtime.mission_time_seconds;
  const runtimeState = runtime?.runtime.state ?? "UNAVAILABLE";
  const health = mission?.health_status ?? missionState?.health_status ?? "—";
  const readiness = mission?.readiness ?? missionState?.readiness ?? "—";
  const freshness = mission?.freshness_status ?? missionState?.freshness_status ?? "—";
  const latestSequence = runtime?.runtime.telemetry_sequence ?? missionState?.vehicles?.[0]?.last_sequence;

  return <section className="mission-overview">
    <div className="mission-hero panel">
      <div className="mission-hero-main">
        <div className="eyebrow">MISSION OPERATIONS</div>
        <h1>TRISHULA Lunar Mission</h1>
        <p>Authoritative mission view built from Ground mission state, physical runtime state, telemetry, events, and alerts.</p>
        <div className="mission-hero-badges">
          <RuntimeStateBadge state={runtimeState} />
          <span className={live ? "overview-chip good" : "overview-chip bad"}>{live ? "GROUND LINK LIVE" : "GROUND LINK OFFLINE"}</span>
          <span className="overview-chip">PHASE {phaseIndex + 1} / {missionPhases.length}</span>
        </div>
      </div>
      <div className="mission-hero-side">
        <span>MISSION PHASE</span>
        <strong>{missionPhase}</strong>
        <div className="mission-progress"><i style={{width:`${Math.max(0, Math.min(100, phaseProgress * 100))}%`}}/></div>
        <small>{phaseProgress > 0 ? `${(phaseProgress * 100).toFixed(1)}% phase progress` : "Progress supplied by physical runtime"}</small>
      </div>
    </div>

    <div className="overview-grid overview-kpis">
      <OverviewKpi label="MISSION HEALTH" value={health} good={health === "NOMINAL"} />
      <OverviewKpi label="READINESS" value={readiness} good={readiness === "READY"} />
      <OverviewKpi label="FRESHNESS" value={freshness} good={freshness === "FRESH" || freshness === "CURRENT"} />
      <OverviewKpi label="MISSION TIME" value={missionTime != null ? `${missionTime.toFixed(3)} s` : "—"} />
      <OverviewKpi label="TELEMETRY SEQUENCE" value={latestSequence != null ? String(latestSequence) : "—"} />
      <OverviewKpi label="VEHICLES" value={String(vehicleCount)} />
    </div>

    <div className="overview-columns">
      <div className="overview-main-column">
        <Panel title="MISSION PHASE TIMELINE" right={<span>{phaseIndex + 1} / {missionPhases.length}</span>}>
          <div className="mission-overview-timeline">
            {missionPhases.map((phase, index) => {
              const done = index < phaseIndex;
              const current = index === phaseIndex;
              return <div className={current ? "overview-phase current" : done ? "overview-phase done" : "overview-phase"} key={phase}>
                <div className="overview-phase-node">{done ? "✓" : index + 1}</div>
                <div><b>{phase}</b><small>{done ? "Completed" : current ? "Current physical phase" : "Pending"}</small></div>
              </div>;
            })}
          </div>
        </Panel>

        <Panel title="MISSION PHYSICAL STATE" right={<span>{lastUpdate ? `Updated ${lastUpdate.toLocaleTimeString()}` : "Waiting"}</span>}>
          <div className="overview-state-grid">
            <OverviewState label="POSITION X" value={runtime?.runtime.position_x_m != null ? `${runtime.runtime.position_x_m.toFixed(2)} m` : "—"} />
            <OverviewState label="POSITION Y" value={runtime?.runtime.position_y_m != null ? `${runtime.runtime.position_y_m.toFixed(2)} m` : "—"} />
            <OverviewState label="ALTITUDE" value={runtime?.runtime.altitude_m != null ? `${runtime.runtime.altitude_m.toFixed(2)} m` : "—"} />
            <OverviewState label="SPEED" value={runtime?.runtime.speed_m_per_s != null ? `${runtime.runtime.speed_m_per_s.toFixed(2)} m/s` : "—"} />
            <OverviewState label="MOON DISTANCE" value={runtime?.runtime.distance_to_moon_m != null ? `${(runtime.runtime.distance_to_moon_m / 1e6).toFixed(3)} Mm` : "—"} />
            <OverviewState label="CONTROL SEQUENCE" value={runtime?.runtime.control_sequence != null ? String(runtime.runtime.control_sequence) : "—"} />
          </div>
        </Panel>

        <Panel title="MISSION DATA FLOW">
          <div className="overview-flow">
            <FlowNode label="PHYSICAL RUNTIME" value={runtime ? "CONNECTED" : "UNAVAILABLE"} good={Boolean(runtime)} />
            <FlowArrow />
            <FlowNode label="TELEMETRY" value={`${telemetryCount} ACCEPTED`} good={telemetryCount > 0} />
            <FlowArrow />
            <FlowNode label="GROUND EVENTS" value={`${eventCount} ACCEPTED`} good={eventCount > 0} />
            <FlowArrow />
            <FlowNode label="MISSION STATE" value={missionState ? "PROJECTED" : "NOT PROJECTED"} good={Boolean(missionState)} />
          </div>
        </Panel>
      </div>

      <aside className="overview-side-column">
        <Panel title="MISSION ALERTS" right={<span className="alert-count">{alerts.active_count ?? activeAlerts.length} ACTIVE</span>}>
          {activeAlerts.slice(0, 6).map(a => <div className="overview-alert" key={a.alert_id}><AlertTriangle size={14}/><div><b>{a.severity} · {a.event_type}</b><span>{a.message ?? a.subsystem ?? "Mission alert"}</span></div></div>)}
          {activeAlerts.length === 0 && <div className="empty">No active mission alerts</div>}
        </Panel>

        <Panel title="RECENT MISSION EVENTS">
          {eventFeed.slice(0, 6).map((event, index) => <div className="overview-event" key={`${event.kind}-${event.time}-${index}`}><time>{event.time}</time><div><b>{event.kind}</b><span>{event.text}</span></div></div>)}
          {eventFeed.length === 0 && <div className="empty">No mission events available</div>}
        </Panel>

        <Panel title="MISSION RECORD">
          <MetricRow label="Mission ID" value={mission?.mission_id ?? "TRISHULA"} />
          <MetricRow label="Vehicles" value={String(mission?.vehicle_count ?? vehicleCount)} />
          <MetricRow label="Telemetry" value={String(telemetryCount)} />
          <MetricRow label="Events" value={String(eventCount)} />
          <MetricRow label="Commands" value={String(mission?.command_count ?? dashboard.summary?.command_count ?? 0)} />
          <MetricRow label="Completed Cmds" value={String(mission?.completed_commands ?? dashboard.summary?.completed_commands ?? 0)} />
        </Panel>
      </aside>
    </div>
  </section>;
}

function VikramOverview({state, dashboard, telemetry, alerts, events, live, lastUpdate}: {state: State; dashboard: Dashboard; telemetry: Telemetry; alerts: Alerts; events: Events; live: boolean; lastUpdate: Date | null}) {
  const missionState = state.missions?.find(m => m.mission_id === "TRISHULA");
  const vikramState = missionState?.vehicles?.find(v => v.source_node.toUpperCase().includes("VIKRAM"));
  const vikramDashboard = dashboard.missions?.[0]?.vehicles?.find(v => v.source_node.toUpperCase().includes("VIKRAM"));
  const vikramRecords = (telemetry.records ?? []).filter(r => (r.envelope?.source_node ?? "").toUpperCase().includes("VIKRAM"));
  const latestVikram = vikramRecords[0];
  const metrics = vikramState?.metrics ?? {};
  const metric = (...names: string[]) => {
    for (const name of names) {
      const exact = metrics[name]?.value;
      if (exact != null && exact !== "") return exact;
      const key = Object.keys(metrics).find(k => k.toLowerCase() === name.toLowerCase());
      if (key && metrics[key]?.value != null && metrics[key]?.value !== "") return metrics[key].value;
    }
    return undefined;
  };
  const display = (value?: string) => value ?? "—";
  const landerState = metric("lander_state", "surface_state", "mode", "operational_state");
  const deployment = metric("rover_deployment_status", "rover_deployment", "deployment_status");
  const relay = metric("relay_status", "data_relay_status", "science_relay_status");
  const receivedScience = metric("science_products_received", "science_received", "science_product_count");
  const integrity = metric("checksum_status", "integrity_status", "data_integrity");
  const health = vikramState?.health_status ?? vikramDashboard?.health_status;
  const sequence = vikramState?.last_sequence ?? vikramDashboard?.last_sequence;
  const vikramEvents = (events.records ?? []).filter(r => (r.envelope?.source_node ?? "").toUpperCase().includes("VIKRAM")).slice(0, 5);
  const vikramAlerts = (alerts.alerts ?? []).filter(a => (a.subsystem ?? "").toUpperCase().includes("VIKRAM") || (a.message ?? "").toUpperCase().includes("VIKRAM")).slice(0, 5);
  const metricEntries = Object.entries(metrics).slice(0, 12);

  return <section className="rover-overview">
    <div className="rover-hero panel">
      <div>
        <div className="eyebrow">VIKRAM LANDER OPERATIONS</div>
        <h1>{vikramState?.source_node ?? vikramDashboard?.source_node ?? "VIKRAM"}</h1>
        <p>Vikram is the lunar lander and rover relay node. This view exposes only lander state, telemetry, events, alerts, and relay data actually supplied by Ground.</p>
        <div className="rover-badges">
          <span className={live ? "overview-chip good" : "overview-chip bad"}>{live ? "GROUND LINK LIVE" : "GROUND LINK OFFLINE"}</span>
          <span className="overview-chip">LANDER TELEMETRY {vikramRecords.length > 0 ? "AVAILABLE" : "WAITING"}</span>
          {health && <span className={health === "NOMINAL" ? "overview-chip good" : "overview-chip"}>{health}</span>}
        </div>
      </div>
      <div className="rover-hero-state">
        <span>LANDER STATE</span>
        <strong>{display(landerState)}</strong>
        <small>{lastUpdate ? `Updated ${lastUpdate.toLocaleTimeString()}` : "Waiting for Vikram state"}</small>
      </div>
    </div>

    <div className="overview-grid overview-kpis">
      <OverviewKpi label="VIKRAM HEALTH" value={display(health)} good={health === "NOMINAL"} />
      <OverviewKpi label="LAST SEQUENCE" value={sequence != null ? String(sequence) : "—"} />
      <OverviewKpi label="ROVER DEPLOYMENT" value={display(deployment)} good={deployment?.toUpperCase().includes("READY") || deployment?.toUpperCase().includes("DEPLOYED")} />
      <OverviewKpi label="RELAY" value={display(relay)} good={relay?.toUpperCase().includes("READY") || relay?.toUpperCase().includes("ACTIVE")} />
      <OverviewKpi label="SCIENCE RECEIVED" value={display(receivedScience)} />
      <OverviewKpi label="INTEGRITY" value={display(integrity)} good={integrity?.toUpperCase().includes("PASS") || integrity?.toUpperCase() === "VALID" || integrity?.toUpperCase() === "VERIFIED"} />
    </div>

    <div className="rover-columns">
      <div className="overview-main-column">
        <Panel title="VIKRAM OPERATIONAL STATE">
          <div className="overview-state-grid">
            <OverviewState label="LANDER STATE" value={display(landerState)} />
            <OverviewState label="ROVER DEPLOYMENT" value={display(deployment)} />
            <OverviewState label="RELAY" value={display(relay)} />
            <OverviewState label="SCIENCE RECEIVED" value={display(receivedScience)} />
            <OverviewState label="INTEGRITY" value={display(integrity)} />
            <OverviewState label="LAST KIND" value={display(vikramDashboard?.last_kind)} />
            <OverviewState label="SCIENCE PRESENT" value={vikramDashboard?.has_science ? "YES" : "Not supplied"} />
            <OverviewState label="COMMAND LINK" value={vikramDashboard?.has_command ? "AVAILABLE" : "Not supplied"} />
          </div>
        </Panel>

        <Panel title="VIKRAM BACKEND METRICS" right={<span>{metricEntries.length} fields shown</span>}>
          {metricEntries.length > 0 ? metricEntries.map(([name, value]) => <MetricRow key={name} label={name.replaceAll("_", " ").toUpperCase()} value={display(value.value)} />) : <div className="empty">No Vikram metrics are currently projected by Ground.</div>}
        </Panel>

        <Panel title="ROVER → VIKRAM → GROUND">
          <div className="overview-flow">
            <FlowNode label="ROVER DATA" value={vikramDashboard?.has_science ? "RECEIVED" : "—"} good={Boolean(vikramDashboard?.has_science)} />
            <FlowArrow />
            <FlowNode label="VIKRAM ARCHIVE" value={vikramState ? "PROJECTED" : "NOT PROJECTED"} good={Boolean(vikramState)} />
            <FlowArrow />
            <FlowNode label="INTEGRITY" value={display(integrity)} good={Boolean(integrity && /PASS|VALID|VERIFIED/i.test(integrity))} />
            <FlowArrow />
            <FlowNode label="GROUND RELAY" value={display(relay)} good={Boolean(relay && /READY|ACTIVE|PASS|COMPLETE/i.test(relay))} />
          </div>
        </Panel>
      </div>

      <aside className="overview-side-column">
        <Panel title="VIKRAM TELEMETRY">
          <MetricRow label="Source" value={display(latestVikram?.envelope?.source_node)} />
          <MetricRow label="Sequence" value={latestVikram?.envelope?.sequence_number != null ? String(latestVikram.envelope.sequence_number) : "—"} />
          <MetricRow label="Record" value={display(latestVikram?.envelope?.record_id)} />
          <MetricRow label="Correlation" value={display(latestVikram?.envelope?.correlation_id)} />
        </Panel>
        <Panel title="VIKRAM ALERTS">
          {vikramAlerts.map(a => <div className="overview-alert" key={a.alert_id}><AlertTriangle size={14}/><div><b>{a.severity} · {a.event_type}</b><span>{a.message ?? a.subsystem ?? "Vikram alert"}</span></div></div>)}
          {vikramAlerts.length === 0 && <div className="empty">No Vikram-specific alerts available.</div>}
        </Panel>
        <Panel title="VIKRAM EVENTS">
          {vikramEvents.map((event, index) => <div className="overview-event" key={`${event.envelope?.record_id ?? "event"}-${index}`}><time>{event.envelope?.sequence_number != null ? `#${event.envelope.sequence_number}` : "—"}</time><div><b>{event.fields?.event_type ?? "EVENT"}</b><span>{event.fields?.message ?? event.envelope?.record_id ?? "Vikram event"}</span></div></div>)}
          {vikramEvents.length === 0 && <div className="empty">No Vikram events available.</div>}
        </Panel>
      </aside>
    </div>
  </section>;
}


function RoverOverview({state, dashboard, telemetry, alerts, events, live, lastUpdate}: {state: State; dashboard: Dashboard; telemetry: Telemetry; alerts: Alerts; events: Events; live: boolean; lastUpdate: Date | null}) {
  const missionState = state.missions?.find(m => m.mission_id === "TRISHULA");
  const roverState = missionState?.vehicles?.find(v => v.source_node.toUpperCase().includes("ROVER")) ?? missionState?.vehicles?.find(v => v.source_node === "ROVER-01");
  const roverDashboard = dashboard.missions?.[0]?.vehicles?.find(v => v.source_node.toUpperCase().includes("ROVER")) ?? dashboard.missions?.[0]?.vehicles?.find(v => v.source_node === "ROVER-01");
  const roverRecords = (telemetry.records ?? []).filter(r => (r.envelope?.source_node ?? "").toUpperCase().includes("ROVER"));
  const latestRover = roverRecords[0];
  const metrics = roverState?.metrics ?? {};
  const metric = (...names: string[]) => {
    for (const name of names) {
      const exact = metrics[name]?.value;
      if (exact != null && exact !== "") return exact;
      const key = Object.keys(metrics).find(k => k.toLowerCase() === name.toLowerCase());
      if (key && metrics[key]?.value != null && metrics[key]?.value !== "") return metrics[key].value;
    }
    return undefined;
  };
  const display = (value?: string) => value ?? "—";
  const surfaceState = metric("surface_state", "rover_state", "mode", "surface_mode");
  const target = metric("target_id", "current_target", "selected_target", "science_target");
  const localization = metric("localization_status", "localization", "position_status");
  const navigation = metric("navigation_status", "navigation", "nav_status");
  const energy = metric("energy_wh", "remaining_energy_wh", "battery_wh", "rover_energy_wh");
  const science = metric("science_status", "science_state", "last_science_status");
  const relay = metric("relay_status", "data_relay_status", "science_relay_status");
  const health = roverState?.health_status ?? roverDashboard?.health_status;
  const sequence = roverState?.last_sequence ?? roverDashboard?.last_sequence;
  const roverEvents = (events.records ?? []).filter(r => (r.envelope?.source_node ?? "").toUpperCase().includes("ROVER")).slice(0, 5);
  const roverAlerts = (alerts.alerts ?? []).filter(a => (a.subsystem ?? "").toUpperCase().includes("ROVER")).slice(0, 5);
  const metricEntries = Object.entries(metrics).slice(0, 12);

  return <section className="rover-overview">
    <div className="rover-hero panel">
      <div>
        <div className="eyebrow">ROVER OPERATIONS</div>
        <h1>{roverState?.source_node ?? roverDashboard?.source_node ?? "ROVER-01"}</h1>
        <p>Live rover view built from Ground mission state and rover telemetry. Values are displayed only when supplied by the backend.</p>
        <div className="rover-badges">
          <span className={live ? "overview-chip good" : "overview-chip bad"}>{live ? "GROUND LINK LIVE" : "GROUND LINK OFFLINE"}</span>
          <span className="overview-chip">TELEMETRY {roverRecords.length > 0 ? "AVAILABLE" : "WAITING"}</span>
          {health && <span className={health === "NOMINAL" ? "overview-chip good" : "overview-chip"}>{health}</span>}
        </div>
      </div>
      <div className="rover-hero-state">
        <span>SURFACE STATE</span>
        <strong>{display(surfaceState)}</strong>
        <small>{lastUpdate ? `Updated ${lastUpdate.toLocaleTimeString()}` : "Waiting for rover state"}</small>
      </div>
    </div>

    <div className="overview-grid overview-kpis">
      <OverviewKpi label="ROVER HEALTH" value={display(health)} good={health === "NOMINAL"} />
      <OverviewKpi label="LAST SEQUENCE" value={sequence != null ? String(sequence) : "—"} />
      <OverviewKpi label="TARGET" value={display(target)} />
      <OverviewKpi label="LOCALIZATION" value={display(localization)} good={localization?.toUpperCase() === "NOMINAL" || localization?.toUpperCase() === "VALID"} />
      <OverviewKpi label="NAVIGATION" value={display(navigation)} good={navigation?.toUpperCase() === "NOMINAL" || navigation?.toUpperCase() === "READY"} />
      <OverviewKpi label="SCIENCE" value={display(science)} good={science?.toUpperCase().includes("PASS") || science?.toUpperCase() === "READY"} />
    </div>

    <div className="rover-columns">
      <div className="overview-main-column">
        <Panel title="ROVER OPERATIONAL STATE">
          <div className="overview-state-grid">
            <OverviewState label="SURFACE STATE" value={display(surfaceState)} />
            <OverviewState label="TARGET" value={display(target)} />
            <OverviewState label="LOCALIZATION" value={display(localization)} />
            <OverviewState label="NAVIGATION" value={display(navigation)} />
            <OverviewState label="ENERGY" value={energy != null ? `${energy} Wh` : "—"} />
            <OverviewState label="SCIENCE" value={display(science)} />
            <OverviewState label="RELAY" value={display(relay)} />
            <OverviewState label="LAST KIND" value={display(roverDashboard?.last_kind)} />
          </div>
        </Panel>

        <Panel title="ROVER BACKEND METRICS" right={<span>{metricEntries.length} fields shown</span>}>
          {metricEntries.length > 0 ? metricEntries.map(([name, value]) => <MetricRow key={name} label={name.replaceAll("_", " ").toUpperCase()} value={display(value.value)} />) : <div className="empty">No rover metrics are currently projected by Ground.</div>}
        </Panel>

        <Panel title="ROVER DATA FLOW">
          <div className="overview-flow">
            <FlowNode label="ROVER STATE" value={roverState ? "PROJECTED" : "NOT PROJECTED"} good={Boolean(roverState)} />
            <FlowArrow />
            <FlowNode label="ROVER TELEMETRY" value={`${roverRecords.length} RECORDS`} good={roverRecords.length > 0} />
            <FlowArrow />
            <FlowNode label="SCIENCE" value={roverDashboard?.has_science ? "AVAILABLE" : "—"} good={Boolean(roverDashboard?.has_science)} />
            <FlowArrow />
            <FlowNode label="COMMAND" value={roverDashboard?.has_command ? "AVAILABLE" : "—"} good={Boolean(roverDashboard?.has_command)} />
          </div>
        </Panel>
      </div>

      <aside className="overview-side-column">
        <Panel title="ROVER TELEMETRY">
          <MetricRow label="Source" value={display(latestRover?.envelope?.source_node)} />
          <MetricRow label="Sequence" value={latestRover?.envelope?.sequence_number != null ? String(latestRover.envelope.sequence_number) : "—"} />
          <MetricRow label="Record" value={display(latestRover?.envelope?.record_id)} />
          <MetricRow label="Correlation" value={display(latestRover?.envelope?.correlation_id)} />
        </Panel>
        <Panel title="ROVER ALERTS">
          {roverAlerts.map(a => <div className="overview-alert" key={a.alert_id}><AlertTriangle size={14}/><div><b>{a.severity} · {a.event_type}</b><span>{a.message ?? a.subsystem ?? "Rover alert"}</span></div></div>)}
          {roverAlerts.length === 0 && <div className="empty">No rover-specific alerts available.</div>}
        </Panel>
        <Panel title="ROVER EVENTS">
          {roverEvents.map((event, index) => <div className="overview-event" key={`${event.envelope?.record_id ?? "event"}-${index}`}><time>{event.envelope?.sequence_number != null ? `#${event.envelope.sequence_number}` : "—"}</time><div><b>{event.fields?.event_type ?? "EVENT"}</b><span>{event.fields?.message ?? event.envelope?.record_id ?? "Rover event"}</span></div></div>)}
          {roverEvents.length === 0 && <div className="empty">No rover events available.</div>}
        </Panel>
      </aside>
    </div>
  </section>;
}


function TelemetryOverview({telemetry, state, runtime, dashboard, live, lastUpdate}: {telemetry: Telemetry; state: State; runtime: RuntimeResponse | null; dashboard: Dashboard; live: boolean; lastUpdate: Date | null}) {
  const records = telemetry.records ?? [];
  const physicalTicks = useMemo(() => {
    const groups = new Map<string, { maxSequence: number; maxTimestamp: number; fields: Record<string, string>; source?: string; recordIds: string[] }>();
    for (const record of records) {
      const sequence = Number(record.envelope?.sequence_number ?? 0);
      const timestamp = Number(record.envelope?.mission_timestamp_ns ?? 0);
      const missionSeconds = record.fields?.mission_time_seconds?.trim();
      const correlation = record.envelope?.correlation_id?.trim()
        || (missionSeconds ? `mission-time:${missionSeconds}` : `timestamp:${timestamp}`);
      const existing = groups.get(correlation);
      groups.set(correlation, {
        maxSequence: Math.max(existing?.maxSequence ?? 0, sequence),
        maxTimestamp: Math.max(existing?.maxTimestamp ?? 0, timestamp),
        fields: { ...(existing?.fields ?? {}), ...(record.fields ?? {}) },
        source: existing?.source ?? record.envelope?.source_node,
        recordIds: [...(existing?.recordIds ?? []), ...(record.envelope?.record_id ? [record.envelope.record_id] : [])],
      });
    }
    return [...groups.values()].sort((a, b) => a.maxSequence - b.maxSequence);
  }, [records]);

  const currentMission = state.missions?.find(m => m.mission_id === "TRISHULA");
  const vehicle = currentMission?.vehicles?.[0];
  const latestTick = physicalTicks[physicalTicks.length - 1];
  const latestFields = latestTick?.fields ?? {};
  const latestSequence = runtime?.runtime.telemetry_sequence ?? latestTick?.maxSequence ?? vehicle?.last_sequence;
  const missionTime = runtime?.runtime.mission_time_seconds ?? Number(latestFields.mission_time_seconds ?? 0);
  const phase = runtime?.runtime.mission_phase ?? latestFields.mission_phase ?? "—";
  const source = latestTick?.source ?? records[0]?.envelope?.source_node ?? vehicle?.source_node ?? "—";
  const telemetryCount = dashboard.summary?.telemetry_accepted ?? telemetry.count ?? records.length;
  const physicalRows = physicalTicks.slice(-12).reverse();

  const format = (value?: string, digits = 2) => {
    if (value == null || value === "") return "—";
    const n = Number(value);
    return Number.isFinite(n) ? n.toFixed(digits) : value;
  };
  const field = (name: string) => latestFields[name] ?? vehicle?.metrics?.[name]?.value ?? "—";
  const timeLabel = (timestamp: number) => timestamp > 0 ? new Date(timestamp / 1_000_000).toLocaleTimeString() : "—";

  const latestMetricNames = Object.keys(latestFields).sort();

  return <section className="telemetry-overview">
    <div className="telemetry-hero panel">
      <div>
        <div className="eyebrow">TELEMETRY OPERATIONS</div>
        <h1>Physical Telemetry</h1>
        <p>Authoritative telemetry view from Ground ingestion. Physical ticks are grouped by correlation ID so one mission tick is displayed as a complete state instead of a single metric record.</p>
        <div className="telemetry-badges">
          <span className={live ? "overview-chip good" : "overview-chip bad"}>{live ? "GROUND LINK LIVE" : "GROUND LINK OFFLINE"}</span>
          <span className="overview-chip">SOURCE {source}</span>
          <span className="overview-chip">{records.length} RAW RECORDS</span>
          <span className="overview-chip">{physicalTicks.length} PHYSICAL TICKS</span>
        </div>
      </div>
      <div className="telemetry-hero-state">
        <span>LATEST TELEMETRY</span>
        <strong>SEQ {latestSequence != null ? latestSequence : "—"}</strong>
        <small>{lastUpdate ? `Ground refresh ${lastUpdate.toLocaleTimeString()}` : "Waiting for telemetry"}</small>
      </div>
    </div>

    <div className="overview-grid overview-kpis">
      <OverviewKpi label="TELEMETRY ACCEPTED" value={String(telemetryCount)} />
      <OverviewKpi label="LATEST SEQUENCE" value={latestSequence != null ? String(latestSequence) : "—"} />
      <OverviewKpi label="MISSION TIME" value={Number.isFinite(missionTime) ? `${missionTime.toFixed(3)} s` : "—"} />
      <OverviewKpi label="MISSION PHASE" value={phase} />
      <OverviewKpi label="ALTITUDE" value={runtime?.runtime.altitude_m != null ? `${runtime.runtime.altitude_m.toFixed(1)} m` : `${format(field("altitude_m"), 1)} m`} />
      <OverviewKpi label="SPEED" value={runtime?.runtime.speed_m_per_s != null ? `${runtime.runtime.speed_m_per_s.toFixed(2)} m/s` : `${format(field("speed_m_per_s"), 2)} m/s`} />
    </div>

    <div className="telemetry-columns">
      <div className="overview-main-column">
        <Panel title="LATEST PHYSICAL TELEMETRY TICK" right={<span>{latestTick?.recordIds.length ?? 0} records merged</span>}>
          <div className="overview-state-grid telemetry-state-grid">
            <OverviewState label="SEQUENCE" value={latestSequence != null ? String(latestSequence) : "—"} />
            <OverviewState label="MISSION TIME" value={Number.isFinite(missionTime) ? `${missionTime.toFixed(3)} s` : "—"} />
            <OverviewState label="PHASE" value={phase} />
            <OverviewState label="POSITION X" value={`${format(field("position_x_m"), 3)} m`} />
            <OverviewState label="POSITION Y" value={`${format(field("position_y_m"), 3)} m`} />
            <OverviewState label="POSITION Z" value={`${format(field("position_z_m"), 3)} m`} />
            <OverviewState label="VELOCITY X" value={`${format(field("velocity_x_m_per_s"), 3)} m/s`} />
            <OverviewState label="VELOCITY Y" value={`${format(field("velocity_y_m_per_s"), 3)} m/s`} />
            <OverviewState label="VELOCITY Z" value={`${format(field("velocity_z_m_per_s"), 3)} m/s`} />
            <OverviewState label="ALTITUDE" value={`${format(field("altitude_m"), 3)} m`} />
            <OverviewState label="SPEED" value={`${format(field("speed_m_per_s"), 3)} m/s`} />
            <OverviewState label="MOON DISTANCE" value={`${format(field("distance_to_moon_m"), 3)} m`} />
          </div>
        </Panel>

        <Panel title="PHYSICAL TELEMETRY HISTORY" right={<span>latest {physicalRows.length} ticks</span>}>
          {physicalRows.length > 0 ? <div className="telemetry-table-wrap"><table className="telemetry-table"><thead><tr><th>SEQ</th><th>TIME</th><th>PHASE</th><th>ALTITUDE</th><th>SPEED</th><th>MOON DIST.</th></tr></thead><tbody>
            {physicalRows.map((tick, index) => <tr key={`${tick.maxSequence}-${tick.maxTimestamp}-${index}`}>
              <td>{tick.maxSequence || "—"}</td>
              <td>{timeLabel(tick.maxTimestamp)}</td>
              <td>{tick.fields.mission_phase ?? tick.fields.phase ?? "—"}</td>
              <td>{tick.fields.altitude_m ? `${format(tick.fields.altitude_m, 1)} m` : "—"}</td>
              <td>{tick.fields.speed_m_per_s ? `${format(tick.fields.speed_m_per_s, 2)} m/s` : "—"}</td>
              <td>{tick.fields.distance_to_moon_m ? `${format(tick.fields.distance_to_moon_m, 1)} m` : "—"}</td>
            </tr>)}
          </tbody></table></div> : <div className="empty">No physical telemetry ticks are currently available from Ground.</div>}
        </Panel>

        <Panel title="TELEMETRY FIELD INVENTORY" right={<span>{latestMetricNames.length} fields in latest tick</span>}>
          {latestMetricNames.length > 0 ? <div className="telemetry-field-grid">{latestMetricNames.map(name => <div className="telemetry-field" key={name}><span>{name.replaceAll("_", " ").toUpperCase()}</span><b>{latestFields[name]}</b></div>)}</div> : <div className="empty">No telemetry fields available.</div>}
        </Panel>
      </div>

      <aside className="overview-side-column">
        <Panel title="TELEMETRY SOURCE">
          <MetricRow label="Mission" value={latestTick?.fields.mission_id ?? records[0]?.envelope?.mission_id ?? "TRISHULA"} />
          <MetricRow label="Source" value={source} />
          <MetricRow label="Latest sequence" value={latestSequence != null ? String(latestSequence) : "—"} />
          <MetricRow label="Correlation" value={latestTick ? (records.find(r => r.envelope?.sequence_number === latestTick.maxSequence)?.envelope?.correlation_id ?? "—") : "—"} />
          <MetricRow label="Ground records" value={String(telemetryCount)} />
        </Panel>
        <Panel title="PHYSICAL LINK">
          <Connection icon={<Radio/>} label="Ground Service" value={live ? "ONLINE" : "OFFLINE"} good={live}/>
          <Connection icon={<Activity/>} label="Physical ticks" value={physicalTicks.length > 0 ? "RECEIVING" : "WAITING"} good={physicalTicks.length > 0}/>
          <Connection icon={<Database/>} label="State projection" value={vehicle ? "PROJECTED" : "WAITING"} good={Boolean(vehicle)}/>
        </Panel>
        <Panel title="LATEST RECORD IDS">
          {latestTick?.recordIds.slice(-6).reverse().map(id => <div className="telemetry-record-id" key={id}>{id}</div>)}
          {!latestTick && <div className="empty">No telemetry records available.</div>}
        </Panel>
      </aside>
    </div>
  </section>;
}


function CommandsOverview({commands, onRefresh, onError}: {commands: CommandsResponse; onRefresh: () => Promise<void>; onError: (message: string | null) => void}) {
  const [command, setCommand] = useState("SYSTEM_CHECK");
  const [target, setTarget] = useState("SPACECRAFT-01");
  const [priority, setPriority] = useState("normal");
  const [reason, setReason] = useState("Mission Control operator request");
  const [parameters, setParameters] = useState("");
  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState<string | null>(null);

  async function submit() {
    setBusy(true); setNotice(null); onError(null);
    const commandId = `MC-${Date.now()}`;
    try {
      const result = await dispatchCommand({ command_id: commandId, command, target, priority, reason, parameters, sequence_number: Date.now() });
      setNotice(`ACCEPTED · ${result.command_id} · lifecycle ${result.lifecycle?.lifecycle ?? "RECEIVED"}`);
      await onRefresh();
    } catch (e) {
      const message = e instanceof Error ? e.message : "Command dispatch failed";
      onError(message);
    } finally { setBusy(false); }
  }

  return <div className="commands-overview">
    <section className="commands-hero panel">
      <div><span className="eyebrow">MISSION CONTROL · COMMANDS</span><h1>Command Center</h1><p>Operator commands are submitted to the real Ground command API. The UI does not simulate command acceptance or execution.</p><div className="commands-badges"><span className="overview-chip">GROUND API</span><span className="overview-chip">LIFECYCLE TRACKED</span><span className="overview-chip">NO SYNTHETIC EXECUTION</span></div></div>
      <div className="commands-hero-state"><span>MISSION</span><strong>TRISHULA</strong><small>{commands.count} command records</small></div>
    </section>

    <div className="overview-kpis commands-kpis">
      <OverviewKpi label="COMMANDS" value={String(commands.count)} good={commands.count > 0}/>
      <OverviewKpi label="RECEIVED / ACTIVE" value={String(commands.commands.filter(c => !c.terminal).length)} />
      <OverviewKpi label="COMPLETED" value={String(commands.commands.filter(c => c.lifecycle === "COMPLETED").length)} good={commands.commands.some(c => c.lifecycle === "COMPLETED")} />
      <OverviewKpi label="FAILED / REJECTED" value={String(commands.commands.filter(c => ["FAILED","REJECTED","TIMEOUT","CANCELLED"].includes(c.lifecycle)).length)} />
    </div>

    <div className="commands-columns">
      <section className="panel command-dispatch-panel"><PanelHeader title="DISPATCH COMMAND" right="POST /v1/missions/TRISHULA/commands" />
        <div className="command-form-grid">
          <label>COMMAND<select value={command} onChange={e => setCommand(e.target.value)}><option>START_ROVER</option><option>SYSTEM_CHECK</option><option>ORIENT_TO_MOON</option><option>TRAJECTORY_CORRECTION</option><option>CAPTURE_IMAGE</option><option>DOWNLOAD_DATA</option><option>ABORT_MISSION</option></select></label>
          <label>TARGET<input value={target} onChange={e => setTarget(e.target.value)} /></label>
          <label>PRIORITY<select value={priority} onChange={e => setPriority(e.target.value)}><option>low</option><option>normal</option><option>high</option><option>critical</option></select></label>
          <label>REASON<input value={reason} onChange={e => setReason(e.target.value)} /></label>
          <label className="command-form-wide">PARAMETERS<input value={parameters} onChange={e => setParameters(e.target.value)} placeholder='optional command parameters' /></label>
        </div>
        <div className="command-dispatch-actions"><button className="runtime-btn start" disabled={busy || !command.trim() || !target.trim()} onClick={submit}>{busy ? "SUBMITTING…" : "DISPATCH COMMAND"}</button>{notice && <span className="command-notice">{notice}</span>}</div>
        <div className="runtime-note">Dispatch creates a Ground command lifecycle record. It does not claim vehicle execution unless the existing command execution/reconciliation path reports it.</div>
      </section>

      <section className="panel"><PanelHeader title="COMMAND LIFECYCLE" right="Ground-authoritative" />
        <div className="command-list">{commands.commands.length === 0 ? <div className="empty-state">No command records returned by Ground.</div> : commands.commands.map(c => <div className="command-row" key={c.command_id}><div className="command-main"><strong>{c.command || "—"}</strong><span>{c.command_id}</span></div><div><b className={`command-status ${c.lifecycle.toLowerCase()}`}>{c.lifecycle || "UNKNOWN"}</b><small>{c.target || "—"}</small></div></div>)}</div>
      </section>
    </div>
  </div>;
}


function GroundStationOverview({dashboard, state, telemetry, events, alerts, runtime, live, lastUpdate}: {dashboard: Dashboard; state: State; telemetry: Telemetry; events: Events; alerts: Alerts; runtime: RuntimeResponse | null; live: boolean; lastUpdate: Date | null}) {
  const summary = dashboard.summary ?? {};
  const mission = dashboard.missions?.find(m => m.mission_id === "TRISHULA") ?? dashboard.missions?.[0];
  const vehicle = mission?.vehicles?.[0];
  const currentMission = state.missions?.find(m => m.mission_id === "TRISHULA") ?? state.missions?.[0];
  const currentVehicle = currentMission?.vehicles?.[0];
  const latest = telemetry.records?.[0];
  const latestEvent = events.records?.[0];
  const latestAlert = alerts.alerts?.[0];
  const telemetryCount = Number(summary.telemetry_accepted ?? telemetry.count ?? 0);
  const eventCount = Number(summary.events_accepted ?? events.count ?? 0);
  const commandCount = Number(summary.command_count ?? mission?.command_count ?? 0);
  const activeAlertCount = Number(summary.active_alert_count ?? alerts.active_count ?? 0);
  const freshness = mission?.freshness_status ?? currentMission?.freshness_status ?? "—";
  const source = latest?.envelope?.source_node ?? vehicle?.source_node ?? "—";
  const age = latest?.envelope?.mission_timestamp_ns ? Math.max(0, Date.now() - Number(latest.envelope.mission_timestamp_ns) / 1_000_000) : null;
  const ageText = age == null || !Number.isFinite(age) ? "—" : age < 1000 ? `${Math.round(age)} ms` : `${(age / 1000).toFixed(1)} s`;
  const projectionReady = Boolean(currentMission && currentVehicle);
  const runtimeState = runtime?.runtime.state ?? "—";
  return <div className="ground-overview">
    <section className="ground-hero panel">
      <div><span className="eyebrow">MISSION CONTROL · GROUND STATION</span><h1>Ground Station Operations</h1><p>Ground-authoritative view of ingestion, mission projection, live delivery, and operational data flow. Infrastructure states are reported only where the Ground service exposes evidence.</p><div className="ground-badges"><span className="overview-chip">TRISHULA GROUND</span><span className={live ? "overview-chip good" : "overview-chip bad"}>{live ? "API ONLINE" : "GROUND OFFLINE"}</span><span className="overview-chip">SOURCE {source}</span></div></div>
      <div className="ground-hero-state"><span>RUNTIME</span><strong>{runtimeState}</strong><small>{lastUpdate ? `Ground refresh ${lastUpdate.toLocaleTimeString()}` : "Waiting for Ground data"}</small></div>
    </section>
    <div className="overview-kpis ground-kpis">
      <OverviewKpi label="TELEMETRY ACCEPTED" value={String(telemetryCount)} good={telemetryCount > 0}/>
      <OverviewKpi label="EVENTS ACCEPTED" value={String(eventCount)} good={eventCount > 0}/>
      <OverviewKpi label="COMMANDS" value={String(commandCount)} />
      <OverviewKpi label="ACTIVE ALERTS" value={String(activeAlertCount)} good={activeAlertCount === 0}/>
      <OverviewKpi label="PROJECTION" value={projectionReady ? "READY" : "WAITING"} good={projectionReady}/>
      <OverviewKpi label="TELEMETRY AGE" value={ageText} good={age != null && age < 10000}/>
    </div>
    <div className="ground-columns">
      <section className="panel ground-flow-panel"><div className="panel-title"><span>GROUND DATA FLOW</span><span className="panel-sub">LIVE EVIDENCE</span></div><div className="ground-flow">
        <GroundFlowStep title="SPACECRAFT / ROVER" value={source} detail={latest ? `Sequence ${latest.envelope?.sequence_number ?? "—"}` : "No telemetry received"} good={Boolean(latest)}/>
        <GroundFlowArrow/>
        <GroundFlowStep title="GROUND INGEST" value={telemetryCount > 0 ? "RECEIVING" : "WAITING"} detail={`${telemetryCount} telemetry · ${eventCount} events`} good={telemetryCount > 0 || eventCount > 0}/>
        <GroundFlowArrow/>
        <GroundFlowStep title="MISSION PROJECTION" value={projectionReady ? "SYNCHRONIZED" : "WAITING"} detail={`Freshness ${freshness}`} good={projectionReady}/>
        <GroundFlowArrow/>
        <GroundFlowStep title="MISSION CONTROL" value={live ? "AVAILABLE" : "OFFLINE"} detail={`Runtime ${runtimeState}`} good={live}/>
      </div></section>
      <aside className="ground-side">
        <Panel title="GROUND SERVICE"><Connection icon={<Radio/>} label="Ground API" value={live ? "ONLINE" : "OFFLINE"} good={live}/><Connection icon={<Wifi/>} label="WebSocket" value={live ? "AVAILABLE" : "OFFLINE"} good={live}/><Connection icon={<Database/>} label="Mission projection" value={projectionReady ? "READY" : "WAITING"} good={projectionReady}/><Connection icon={<Antenna/>} label="Latest source" value={source} good={Boolean(latest)}/></Panel>
        <Panel title="LATEST GROUND RECORDS"><MetricRow label="Telemetry" value={latest?.envelope?.record_id ?? "—"}/><MetricRow label="Event" value={latestEvent?.envelope?.record_id ?? "—"}/><MetricRow label="Alert" value={latestAlert?.alert_id ?? "—"}/><MetricRow label="Vehicle" value={currentVehicle?.source_node ?? "—"}/></Panel>
      </aside>
    </div>
    <section className="panel ground-history"><div className="panel-title"><span>GROUND OBSERVATION</span><span className="panel-sub">NO SYNTHETIC STATUS</span></div><div className="ground-history-grid"><GroundObservation label="Mission" value={mission?.mission_id ?? "TRISHULA"}/><GroundObservation label="Mission health" value={mission?.health_status ?? currentMission?.health_status ?? "—"}/><GroundObservation label="Readiness" value={mission?.readiness ?? currentMission?.readiness ?? "—"}/><GroundObservation label="Freshness" value={freshness}/><GroundObservation label="Last telemetry" value={latest?.envelope?.sequence_number != null ? String(latest.envelope.sequence_number) : "—"}/><GroundObservation label="Runtime telemetry" value={runtime?.runtime.telemetry_sequence != null ? String(runtime.runtime.telemetry_sequence) : "—"}/></div></section>
  </div>;
}

function GroundFlowStep({title, value, detail, good}: {title: string; value: string; detail: string; good: boolean}) { return <div className={`ground-flow-step ${good ? "good" : ""}`}><span>{title}</span><strong>{value}</strong><small>{detail}</small></div>; }
function GroundFlowArrow() { return <div className="ground-flow-arrow">→</div>; }
function GroundObservation({label, value}: {label: string; value: string}) { return <div className="ground-observation"><span>{label}</span><strong>{value}</strong></div>; }

function AlertsOverview({alerts, events, live, lastUpdate}: {alerts: Alerts; events: Events; live: boolean; lastUpdate: Date | null}) {
  const allAlerts = alerts.alerts ?? [];
  const active = allAlerts.filter(a => a.status === "ACTIVE");
  const resolved = allAlerts.filter(a => a.status !== "ACTIVE");
  const severityCount = (severity: string) => allAlerts.filter(a => a.severity?.toUpperCase() === severity).length;
  const subsystemCount = new Set(allAlerts.map(a => a.subsystem).filter(Boolean)).size;
  const recent = [...allAlerts].sort((a,b) => String(b.updated_at ?? "").localeCompare(String(a.updated_at ?? ""))).slice(0, 20);
  const time = (value?: string) => value ? new Date(value).toLocaleString() : "—";
  return <div className="alerts-overview">
    <section className="alerts-hero panel">
      <div><span className="eyebrow">MISSION CONTROL · ALERTS</span><h1>Alert Operations</h1><p>Ground-authoritative alerts and event-derived fault visibility. The UI displays alert lifecycle data returned by the Ground service; it does not manufacture alert states.</p><div className="alerts-badges"><span className="overview-chip">GROUND AUTHORITATIVE</span><span className="overview-chip">LIFECYCLE TRACKED</span><span className={live ? "overview-chip good" : "overview-chip bad"}>{live ? "LIVE FEED" : "GROUND OFFLINE"}</span></div></div>
      <div className="alerts-hero-state"><span>MISSION</span><strong>TRISHULA</strong><small>{lastUpdate ? `Updated ${lastUpdate.toLocaleTimeString()}` : "Waiting for Ground data"}</small></div>
    </section>
    <div className="overview-kpis alerts-kpis">
      <OverviewKpi label="ACTIVE" value={String(alerts.active_count ?? active.length)} good={active.length === 0}/>
      <OverviewKpi label="TOTAL" value={String(alerts.total_count ?? allAlerts.length)} />
      <OverviewKpi label="CRITICAL" value={String(severityCount("CRITICAL"))} />
      <OverviewKpi label="HIGH" value={String(severityCount("HIGH"))} />
      <OverviewKpi label="RESOLVED / CLOSED" value={String(resolved.length)} good={resolved.length > 0 || allAlerts.length === 0}/>
      <OverviewKpi label="SUBSYSTEMS" value={String(subsystemCount)} />
    </div>
    <div className="alerts-columns">
      <section className="panel"><PanelHeader title="ACTIVE ALERTS" right={`${active.length} Ground-authoritative`} />
        {active.length === 0 ? <div className="empty-state">No active alerts returned by Ground.</div> : <div className="alert-list">{active.map(a => <div className={`alert-row severity-${(a.severity ?? "UNKNOWN").toLowerCase()}`} key={a.alert_id}><div className="alert-icon"><AlertTriangle size={15}/></div><div className="alert-main"><strong>{a.event_type || "ALERT"}</strong><span>{a.message || a.subsystem || "No message supplied"}</span><small>{a.alert_id} · {a.subsystem || "Subsystem unspecified"}</small></div><div className="alert-meta"><b>{a.severity || "UNKNOWN"}</b><span>{a.lifecycle || a.status || "UNKNOWN"}</span><small>{time(a.updated_at)}</small></div></div>)}</div>}
      </section>
      <aside className="alerts-side">
        <Panel title="ALERT LINK"><Connection icon={<Radio/>} label="Ground Service" value={live ? "ONLINE" : "OFFLINE"} good={live}/><Connection icon={<ShieldCheck/>} label="Alert API" value={alerts ? "RECEIVING" : "WAITING"} good={Boolean(alerts)}/><Connection icon={<Database/>} label="Event stream" value={(events.records?.length ?? 0) > 0 ? "RECEIVING" : "WAITING"} good={(events.records?.length ?? 0) > 0}/></Panel>
        <Panel title="SEVERITY BREAKDOWN"><MetricRow label="CRITICAL" value={String(severityCount("CRITICAL"))}/><MetricRow label="HIGH" value={String(severityCount("HIGH"))}/><MetricRow label="MEDIUM" value={String(severityCount("MEDIUM"))}/><MetricRow label="LOW" value={String(severityCount("LOW"))}/></Panel>
      </aside>
    </div>
    <section className="panel"><PanelHeader title="ALERT HISTORY" right={`${recent.length} records`} />
      {recent.length === 0 ? <div className="empty-state">No alert records returned by Ground.</div> : <div className="alert-history">{recent.map(a => <div className="alert-history-row" key={a.alert_id}><time>{time(a.updated_at)}</time><span className={`history-severity severity-text-${(a.severity ?? "unknown").toLowerCase()}`}>{a.severity || "UNKNOWN"}</span><b>{a.event_type || "ALERT"}</b><span>{a.status || "UNKNOWN"}</span><small>{a.message || a.subsystem || "—"}</small></div>)}</div>}
    </section>
  </div>;
}


function DataOverview({dashboard, state, telemetry, events, commands, alerts, live, lastUpdate}: {dashboard: Dashboard; state: State; telemetry: Telemetry; events: Events; commands: CommandsResponse; alerts: Alerts; live: boolean; lastUpdate: Date | null}) {
  const mission = dashboard.missions?.[0];
  const vehicle = mission?.vehicles?.[0];
  const telemetryRecords = telemetry.records ?? [];
  const eventRecords = events.records ?? [];
  const commandRecords = commands.commands ?? [];
  const alertRecords = alerts.alerts ?? [];
  const latestTelemetry = telemetryRecords[0];
  const latestEvent = eventRecords[0];
  const latestCommand = commandRecords[0];
  const telemetryCount = telemetry.count ?? telemetryRecords.length;
  const eventCount = events.count ?? eventRecords.length;
  const commandCount = commands.count ?? commandRecords.length;
  const alertCount = alerts.total_count ?? alertRecords.length;
  const dataSources = [
    ["Telemetry", telemetryCount > 0 ? "Receiving" : "Awaiting data", telemetryCount > 0],
    ["Mission events", eventCount > 0 ? "Available" : "Awaiting records", eventCount > 0],
    ["Command history", commandCount > 0 ? "Available" : "Awaiting records", commandCount > 0],
    ["Alert history", alertCount > 0 ? "Available" : "No records", alertCount > 0],
  ] as const;
  return <section className="ops-page">
    <div className="ops-hero">
      <div className="ops-hero-copy">
        <span className="eyebrow">MISSION DATA OPERATIONS</span>
        <h1>Data Operations</h1>
        <p>Authoritative mission records received and retained by the Ground segment. This workspace presents operational evidence without synthesizing unavailable data.</p>
        <div className="ops-hero-meta"><span>MISSION <b>{mission?.mission_id ?? "TRISHULA"}</b></span><span>VEHICLE <b>{vehicle?.source_node ?? "—"}</b></span><span>STATE <b>{state.mission_count ? "PROJECTED" : "AWAITING"}</b></span></div>
      </div>
      <div className="ops-hero-status"><span className={live ? "status-pulse" : "status-pulse offline"}/><b>{live ? "GROUND DATA LINK" : "DATA LINK OFFLINE"}</b><small>{lastUpdate ? `Last synchronized ${lastUpdate.toLocaleTimeString()}` : "Awaiting Ground synchronization"}</small></div>
    </div>

    <div className="ops-kpis">
      <OpsKpi label="TELEMETRY" value={String(telemetryCount)} detail="records received" good={telemetryCount > 0}/>
      <OpsKpi label="EVENTS" value={String(eventCount)} detail="mission records" good={eventCount > 0}/>
      <OpsKpi label="COMMANDS" value={String(commandCount)} detail="command records" good={commandCount > 0}/>
      <OpsKpi label="ALERTS" value={String(alertCount)} detail="retained records" good={alertCount >= 0}/>
    </div>

    <div className="ops-section-title"><div><span>01</span><div><b>DATA AVAILABILITY</b><small>Current sources exposed by Mission Control</small></div></div></div>
    <div className="ops-grid ops-grid-2">
      <section className="ops-card">
        <div className="ops-card-head"><div><span>RECORD SOURCES</span><h2>Mission Data Inventory</h2></div><Database size={17}/></div>
        <div className="data-inventory">{dataSources.map(([label,value,good]) => <div className="inventory-row" key={label}><span className={good ? "inventory-dot good-dot" : "inventory-dot"}/><div><b>{label}</b><small>{value}</small></div></div>)}</div>
        <div className="ops-footnote"><span>MISSION STATE</span><b>{state.mission_count ? "AVAILABLE" : "NOT SUPPLIED"}</b></div>
      </section>
      <section className="ops-card">
        <div className="ops-card-head"><div><span>RECENT EVIDENCE</span><h2>Latest Ground Records</h2></div><Activity size={17}/></div>
        <div className="evidence-list">
          <EvidenceRow label="Telemetry" value={latestTelemetry?.envelope?.record_id ?? "—"} meta={latestTelemetry?.envelope?.source_node ?? "No source"}/>
          <EvidenceRow label="Event" value={latestEvent?.envelope?.record_id ?? "—"} meta={latestEvent?.fields?.event_type ?? "No event"}/>
          <EvidenceRow label="Command" value={latestCommand?.command_id ?? "—"} meta={latestCommand?.lifecycle ?? "No command"}/>
          <EvidenceRow label="Alerts" value={String(alerts.active_count ?? 0)} meta="active"/>
        </div>
      </section>
    </div>

    <div className="ops-section-title"><div><span>02</span><div><b>MISSION RECORDS</b><small>Most recent records available to operators</small></div></div></div>
    <div className="ops-grid ops-grid-2">
      <section className="ops-card ops-table-card">
        <div className="ops-card-head"><div><span>PHYSICAL TELEMETRY</span><h2>Recent Telemetry</h2></div><Radio size={17}/></div>
        <div className="ops-table"><div className="ops-table-head"><span>RECORD</span><span>SOURCE</span><span>SEQUENCE</span></div>{telemetryRecords.slice(0,8).map((r,i)=><div className="ops-table-row" key={r.envelope?.record_id ?? i}><b>{r.envelope?.record_id ?? "—"}</b><span>{r.envelope?.source_node ?? "—"}</span><span>{r.envelope?.sequence_number ?? "—"}</span></div>)}</div>
        {!telemetryRecords.length && <div className="ops-empty">No telemetry records are currently available.</div>}
      </section>
      <section className="ops-card ops-table-card">
        <div className="ops-card-head"><div><span>MISSION ACTIVITY</span><h2>Recent Events & Commands</h2></div><Zap size={17}/></div>
        <div className="ops-table"><div className="ops-table-head"><span>TYPE</span><span>RECORD</span><span>STATE</span></div>{eventRecords.slice(0,4).map((r,i)=><div className="ops-table-row" key={`event-${r.envelope?.record_id ?? i}`}><b>{r.fields?.event_type ?? "EVENT"}</b><span>{r.envelope?.record_id ?? "—"}</span><span>RECEIVED</span></div>)}{commandRecords.slice(0,4).map((r,i)=><div className="ops-table-row" key={`cmd-${r.command_id ?? i}`}><b>{r.command ?? "COMMAND"}</b><span>{r.command_id ?? "—"}</span><span>{r.lifecycle ?? "—"}</span></div>)}</div>
        {!eventRecords.length && !commandRecords.length && <div className="ops-empty">No mission activity records are currently available.</div>}
      </section>
    </div>

    <div className="ops-note"><ShieldCheck size={16}/><div><b>Data integrity principle</b><span>Mission Control displays only data supplied by the Ground service. Detailed science products are shown when an authoritative endpoint exposes them; this view does not manufacture science records.</span></div></div>
  </section>;
}

function SystemOverview({dashboard, state, telemetry, events, commands, alerts, runtime, runtimeError, live, lastUpdate}: {dashboard: Dashboard; state: State; telemetry: Telemetry; events: Events; commands: CommandsResponse; alerts: Alerts; runtime: RuntimeResponse | null; runtimeError: string | null; live: boolean; lastUpdate: Date | null}) {
  const mission = dashboard.missions?.[0];
  const runtimeReady = Boolean(runtime && !runtimeError);
  const telemetryReady = (telemetry.count ?? telemetry.records?.length ?? 0) > 0;
  const commandReady = (commands.count ?? commands.commands?.length ?? 0) > 0;
  const projectionReady = Boolean(state.mission_count && mission);
  const checks = [
    ["Mission Control API", live ? "Operational" : "Unavailable", live],
    ["Mission projection", projectionReady ? "Available" : "Awaiting state", projectionReady],
    ["Physical runtime", runtimeReady ? "Connected" : (runtimeError ?? "Not configured"), runtimeReady],
    ["Telemetry stream", telemetryReady ? "Receiving" : "Waiting", telemetryReady],
    ["Command feed", commandReady ? "Receiving" : "No records", commandReady],
    ["Alert feed", "Available", true],
    ["Mission Control WebSocket", live ? "Connected" : "Disconnected", live],
  ] as const;
  const healthyChecks = checks.filter(([, , good]) => good).length;
  return <section className="ops-page">
    <div className="ops-hero">
      <div className="ops-hero-copy">
        <span className="eyebrow">MISSION CONTROL OPERATIONS</span>
        <h1>System Operations</h1>
        <p>Operational readiness of the Mission Control interfaces and data paths. Status is derived only from authoritative APIs currently exposed by the Ground segment.</p>
        <div className="ops-hero-meta"><span>MISSION <b>{mission?.mission_id ?? "TRISHULA"}</b></span><span>CHECKS <b>{healthyChecks}/{checks.length} AVAILABLE</b></span><span>RUNTIME <b>{runtime?.runtime.state ?? "—"}</b></span></div>
      </div>
      <div className="ops-hero-status"><span className={live ? "status-pulse" : "status-pulse offline"}/><b>{live ? "CONTROL SYSTEM ONLINE" : "CONTROL SYSTEM OFFLINE"}</b><small>{lastUpdate ? `Last synchronized ${lastUpdate.toLocaleTimeString()}` : "Awaiting Mission Control data"}</small></div>
    </div>

    <div className="ops-kpis">
      <OpsKpi label="CONTROL LINK" value={live ? "ONLINE" : "OFFLINE"} detail="Mission Control API" good={live}/>
      <OpsKpi label="RUNTIME" value={runtime?.runtime.state ?? "—"} detail={runtime?.runtime_version ?? "physical runtime"} good={runtimeReady}/>
      <OpsKpi label="TELEMETRY" value={String(telemetry.count ?? telemetry.records?.length ?? 0)} detail="records available" good={telemetryReady}/>
      <OpsKpi label="ACTIVE ALERTS" value={String(alerts.active_count ?? 0)} detail="current mission alerts" good={(alerts.active_count ?? 0) === 0}/>
    </div>

    <div className="ops-section-title"><div><span>01</span><div><b>SERVICE READINESS</b><small>Interfaces and data paths visible to Mission Control</small></div></div></div>
    <section className="ops-card readiness-card">
      <div className="ops-card-head"><div><span>CONTROL PLANE</span><h2>Operational Readiness</h2></div><Gauge size={17}/></div>
      <div className="readiness-list">{checks.map(([label,value,good])=><div className="readiness-row" key={label}><div className="readiness-name"><span className={good ? "status-pulse" : "status-pulse offline"}/><b>{label}</b></div><strong className={good ? "good" : ""}>{value}</strong></div>)}</div>
    </section>

    <div className="ops-section-title"><div><span>02</span><div><b>MISSION STATE</b><small>Current operational state and physical runtime evidence</small></div></div></div>
    <div className="ops-grid ops-grid-2">
      <section className="ops-card">
        <div className="ops-card-head"><div><span>MISSION CONTROL</span><h2>Operational State</h2></div><Satellite size={17}/></div>
        <div className="system-metrics">
          <SystemMetric label="Mission" value={mission?.mission_id ?? "TRISHULA"}/>
          <SystemMetric label="Mission health" value={mission?.health_status ?? "—"}/>
          <SystemMetric label="Readiness" value={mission?.readiness ?? "—"}/>
          <SystemMetric label="Data freshness" value={mission?.freshness_status ?? "—"}/>
          <SystemMetric label="Vehicles" value={String(mission?.vehicle_count ?? 0)}/>
          <SystemMetric label="Active faults" value={String(mission?.active_alert_count ?? 0)}/>
        </div>
      </section>
      <section className="ops-card">
        <div className="ops-card-head"><div><span>PHYSICAL RUNTIME</span><h2>Runtime Evidence</h2></div><Activity size={17}/></div>
        <div className="system-metrics">
          <SystemMetric label="Runtime version" value={runtime?.runtime_version ?? "—"}/>
          <SystemMetric label="Runtime state" value={runtime?.runtime.state ?? "—"}/>
          <SystemMetric label="Control sequence" value={runtime ? String(runtime.runtime.control_sequence) : "—"}/>
          <SystemMetric label="Telemetry sequence" value={runtime ? String(runtime.runtime.telemetry_sequence) : "—"}/>
          <SystemMetric label="Mission phase" value={runtime?.runtime.mission_phase ?? "—"}/>
          <SystemMetric label="Mission time" value={runtime ? `${runtime.runtime.mission_time_seconds.toFixed(3)} s` : "—"}/>
        </div>
      </section>
    </div>

    <div className="ops-section-title"><div><span>03</span><div><b>GROUND DATA FLOW</b><small>Accepted records currently visible through Mission Control</small></div></div></div>
    <section className="ops-card">
      <div className="flow-strip professional-flow">
        <FlowNode label="TELEMETRY" value={String(dashboard.summary?.telemetry_accepted ?? telemetry.count ?? 0)} good={telemetryReady}/><FlowArrow/>
        <FlowNode label="EVENTS" value={String(dashboard.summary?.events_accepted ?? events.count ?? 0)} good={(events.count ?? 0) > 0}/><FlowArrow/>
        <FlowNode label="COMMANDS" value={String(dashboard.summary?.command_count ?? commands.count ?? 0)} good={(commands.count ?? 0) > 0}/><FlowArrow/>
        <FlowNode label="COMPLETED" value={String(dashboard.summary?.completed_commands ?? 0)} good/><FlowArrow/>
        <FlowNode label="ACTIVE ALERTS" value={String(alerts.active_count ?? 0)} good={(alerts.active_count ?? 0) === 0}/>
      </div>
    </section>

    <div className="ops-note"><ShieldCheck size={16}/><div><b>Infrastructure boundary</b><span>Kafka, PostgreSQL, Redis and host-level health are not labeled here because the current Mission Control API does not expose authoritative per-component health endpoints. They should be added when the Ground service provides those contracts.</span></div></div>
  </section>;
}

function OpsKpi({label,value,detail,good}:{label:string;value:string;detail:string;good?:boolean}) { return <div className="ops-kpi panel"><span>{label}</span><strong className={good ? "good" : ""}>{value}</strong><small>{detail}</small></div>; }
function EvidenceRow({label,value,meta}:{label:string;value:string;meta:string}) { return <div className="evidence-row"><div><span>{label}</span><b>{value}</b></div><small>{meta}</small></div>; }
function SystemMetric({label,value}:{label:string;value:string}) { return <div className="system-metric"><span>{label}</span><b>{value}</b></div>; }

function OverviewKpi({label,value,good}:{label:string;value:string;good?:boolean}) { return <div className="overview-kpi panel"><span>{label}</span><strong className={good ? "good" : ""}>{value}</strong></div>; }
function OverviewState({label,value}:{label:string;value:string}) { return <div className="overview-state"><span>{label}</span><b>{value}</b></div>; }
function FlowNode({label,value,good}:{label:string;value:string;good?:boolean}) { return <div className="flow-node"><span>{label}</span><b className={good ? "good" : ""}>{value}</b></div>; }
function FlowArrow() { return <span className="flow-arrow">→</span>; }

function RuntimeItem({label,value}:{label:string;value:string}) { return <div className="runtime-item"><span>{label}</span><b>{value}</b></div>; }
function RuntimeStateBadge({state}:{state:string}) { const normalized = state.toUpperCase(); return <span className={`runtime-badge ${normalized.toLowerCase()}`}>{normalized}</span>; }

function Panel({title, right, children}: {title:string; right?:React.ReactNode; children:React.ReactNode}) { return <section className="panel"><PanelHeader title={title} right={right}/>{children}</section>; }
function PanelHeader({title,right}:{title:string;right?:React.ReactNode}) { return <div className="panel-head"><b>{title}</b>{right && <span>{right}</span>}</div>; }
function Phase({done,current,label,detail}:{done?:boolean;current?:boolean;label:string;detail:string}) { return <div className={current ? "phase current" : "phase"}><span className={done || current ? "phase-icon done" : "phase-icon"}>{done ? "✓" : current ? "●" : "○"}</span><span>{label}</span><small>{detail}</small></div>; }
function MetricRow({label,value,good}:{label:string;value:string;good?:boolean}) { return <div className="metric-row"><span>{label}</span><strong className={good ? "good" : ""}>{value}</strong></div>; }
function FeedItem({time,text,warning}:{time:string;text:string;warning?:boolean}) { return <div className="feed"><time>{time}</time><span className={warning ? "warning" : ""}>{text}</span></div>; }
function Media({title,text,live,moon,icon}:{title:string;text:string;live?:boolean;moon?:boolean;icon:React.ReactNode}) {
  const visual = moon ? <div className="moon-orb"/> : title === "LAUNCH VIEW" ? <div className="launch-visual"><div className="launch-smoke"/><div className="mini-rocket">{icon}</div></div> : title === "SPACECRAFT CAMERA" ? <div className="spacecraft-visual"><div className="mini-earth"/><div className="mini-spacecraft">{icon}</div></div> : title === "ROVER STATUS" ? <div className="rover-visual"><div className="mini-rover"><span/><i/><i/><i/></div></div> : <div className="media-icon">{icon}</div>;
  return <div className={moon ? "media moon" : "media"}><div className="media-bg">{visual}</div><div className="media-label"><b>{title}</b>{live && <span>● LIVE</span>}<small>{text}</small></div></div>;
}
function TelemetryCard({icon,label,value,unit}:{icon:React.ReactNode;label:string;value:string;unit:string}) { return <div className="telemetry-card"><div className="telemetry-label">{icon}{label}</div><strong>{value} <small>{unit}</small></strong><div className="spark"><i/><i/><i/><i/><i/><i/><i/></div></div>; }
function Connection({icon,label,value,good}:{icon:React.ReactNode;label:string;value:string;good?:boolean}) { return <div className="connection"><span>{icon}</span><div><b>{label}</b><small className={good ? "good" : ""}>{value}</small></div></div>; }
