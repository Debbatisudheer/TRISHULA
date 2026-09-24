export type Dashboard = {
  api_version?: string;
  dashboard_version?: string;
  status?: string;
  mission_count?: number;
  summary?: {
    vehicle_count?: number;
    active_alert_count?: number;
    total_alert_count?: number;
    command_count?: number;
    completed_commands?: number;
    failed_commands?: number;
    telemetry_accepted?: number;
    events_accepted?: number;
  };
  missions?: Array<{
    mission_id: string;
    health_status?: string;
    readiness?: string;
    freshness_status?: string;
    vehicle_count?: number;
    active_alert_count?: number;
    total_alert_count?: number;
    command_count?: number;
    completed_commands?: number;
    failed_commands?: number;
    vehicles?: Array<{
      source_node: string;
      health_status?: string;
      last_sequence?: number;
      last_kind?: string;
      last_updated_at?: string;
      active_fault_count?: number;
      metric_count?: number;
      subsystem_count?: number;
      has_science?: boolean;
      has_command?: boolean;
      has_file?: boolean;
    }>;
  }>;
};

export type State = {
  mission_count?: number;
  missions?: Array<{
    mission_id: string;
    updated_at?: string;
    health_status?: string;
    readiness?: string;
    freshness_status?: string;
    vehicle_count?: number;
    active_fault_count?: number;
    vehicles?: Array<{
      source_node: string;
      last_sequence?: number;
      last_kind?: string;
      health_status?: string;
      last_updated_at?: string;
      metrics?: Record<string, { value?: string; unit?: string; quality_class?: string; limit_status?: string; sequence?: number }>;
    }>;
  }>;
};

export type TelemetryRecord = {
  envelope?: {
    record_id?: string;
    mission_id?: string;
    source_node?: string;
    sequence_number?: number;
    mission_timestamp_ns?: number;
    correlation_id?: string;
  };
  fields?: Record<string, string>;
};

export type Telemetry = {
  count?: number;
  records?: TelemetryRecord[];
};

export type EventRecord = {
  envelope?: {
    record_id?: string;
    mission_id?: string;
    source_node?: string;
    sequence_number?: number;
    mission_timestamp_ns?: number;
    correlation_id?: string;
  };
  fields?: Record<string, string>;
};

export type Events = {
  count?: number;
  records?: EventRecord[];
};


export type RuntimeSnapshot = {
  result?: string;
  state: "READY" | "STARTING" | "RUNNING" | "PAUSED" | "RESUMING" | "COMPLETE" | "ABORTED" | "FAULT" | string;
  control_sequence: number;
  telemetry_sequence: number;
  mission_time_seconds: number;
  phase_elapsed_seconds: number;
  phase_progress: number;
  position_x_m: number;
  position_y_m: number;
  position_z_m: number;
  velocity_x_m_per_s: number;
  velocity_y_m_per_s: number;
  velocity_z_m_per_s: number;
  altitude_m: number;
  speed_m_per_s: number;
  distance_to_moon_m: number;
  moon_x_m: number;
  moon_y_m: number;
  mission_phase: string;
  battery_soc: number;
  thermal_temperature_c: number;
  thermal_margin: number;
  propellant_fraction: number;
  thrust_fraction: number;
  engine_firing: boolean;
  rcs_firing: boolean;
  power_status: string;
  propulsion_status: string;
  thermal_status: string;
  communication_status: string;
  navigation_status: string;
  health_status: string;
};

export type RuntimeResponse = {
  mission_id: string;
  runtime_version: string;
  runtime: RuntimeSnapshot;
};

export type Alerts = {
  active_count?: number;
  total_count?: number;
  alerts?: Array<{
    alert_id: string;
    severity: string;
    status: string;
    lifecycle: string;
    event_type: string;
    subsystem?: string;
    message?: string;
    updated_at?: string;
  }>;
};

const base = process.env.NEXT_PUBLIC_GROUND_API_URL ?? "/api/ground";
const mission = process.env.NEXT_PUBLIC_MISSION_ID ?? "TRISHULA";
const websocketBase = process.env.NEXT_PUBLIC_GROUND_API_URL ?? "http://localhost:8082";

async function get<T>(path: string): Promise<T> {
  const response = await fetch(`${base}${path}`, { cache: "no-store" });
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
  return response.json() as Promise<T>;
}

export async function loadMissionData() {
  const [dashboard, state, alerts, telemetry, events] = await Promise.all([
    get<Dashboard>("/v1/mission-control/dashboard"),
    get<State>("/v1/mission-control/state"),
    get<Alerts>("/v1/mission-control/alerts"),
    get<Telemetry>(`/v1/missions/${mission}/telemetry?limit=50`),
    get<Events>(`/v1/missions/${mission}/events?limit=50`)
  ]);
  return { dashboard, state, alerts, telemetry, events, mission };
}

export function wsUrl() {
  const url = new URL(`${websocketBase.replace(/\/$/, "")}/v1/mission-control/ws`);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  return url.toString();
}


export async function loadRuntime(): Promise<RuntimeResponse> {
  return get<RuntimeResponse>(`/v1/missions/${mission}/runtime`);
}

export async function controlRuntime(action: "start" | "pause" | "resume" | "abort" | "reset"): Promise<RuntimeResponse> {
  const response = await fetch(`${base}/v1/missions/${mission}/runtime/${action}`, {
    method: "POST",
    cache: "no-store",
  });
  const data = await response.json().catch(() => ({}));
  if (!response.ok || data.accepted === false) {
    const message = typeof data.error === "string" ? data.error : `${response.status} ${response.statusText}`;
    throw new Error(message);
  }
  return data as RuntimeResponse;
}

export type CommandView = {
  command_id: string;
  command?: string;
  target?: string;
  lifecycle: string;
  terminal: boolean;
  latest_record?: TelemetryRecord;
  execution?: { state?: string; terminal?: boolean; message?: string };
  dead_letter?: { command_id?: string; reason?: string };
};

export type CommandsResponse = {
  mission_id: string;
  count: number;
  limit: number;
  commands: CommandView[];
};

export async function loadCommands(): Promise<CommandsResponse> {
  return get<CommandsResponse>(`/v1/missions/${mission}/commands?limit=50`);
}

export async function dispatchCommand(request: {
  command_id: string;
  command: string;
  target: string;
  priority?: string;
  reason?: string;
  parameters?: string;
  sequence_number: number;
  source_node?: string;
  application_id?: number;
}) {
  const response = await fetch(`${base}/v1/missions/${mission}/commands`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    cache: "no-store",
    body: JSON.stringify(request),
  });
  const data = await response.json().catch(() => ({}));
  if (!response.ok || data.accepted === false) {
    throw new Error(typeof data.error === "string" ? data.error : `${response.status} ${response.statusText}`);
  }
  return data as { accepted: boolean; command_id: string; lifecycle?: { lifecycle?: string } };
}
