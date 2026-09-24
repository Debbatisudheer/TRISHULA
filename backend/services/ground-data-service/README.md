# TRISHULA Ground Data Service — V0.9.105

V0.9.81 command lifecycle execution reconciliation remains intact. V0.9.82 adds the operator-facing command dispatch boundary on top of that read model.

## Command lifecycle

Primary operational path:

`VALIDATED -> QUEUED -> SENT -> ACKNOWLEDGED -> EXECUTING -> COMPLETED`

Failure/terminal paths include:

`FAILED`, `TIMEOUT`, `REJECTED`, `CANCELLED`

The reconciler correlates command records with vehicle-side event/telemetry feedback using `command_id`, then records acknowledgement/execution/completion sequences, timestamps, result/error data, and the latest vehicle response.

Existing `CommandEngine` validation remains intact. The reconciliation layer is a separate read-model/state-machine boundary so strict command validation is not replaced.

## APIs

- `GET /v1/commands/lifecycle` — existing command validation lifecycle
- `GET /v1/commands/execution` — reconciled command execution states
- `GET /v1/commands/execution/{commandID}` — one reconciled command

## Feedback examples

A command record can carry `lifecycle=VALIDATED` or `execution_state=QUEUED/SENT/...`.
Vehicle feedback can be represented as an event or telemetry record with `command_id` plus `execution_state`, `command_status`, or an `event_type` such as `COMMAND_ACK`, `COMMAND_EXECUTING`, `COMMAND_COMPLETED`, `COMMAND_FAILED`, `COMMAND_TIMEOUT`, or `COMMAND_CANCELLED`.

## V0.9.82 command dispatch boundary

V0.9.82 adds an operator-facing command submission boundary on top of the V0.9.81 execution reconciliation read model. `POST /v1/missions/{missionID}/commands` creates a validated command record with an explicit command ID and sequence number, persists/publishes it through the existing ground ingestion path, and leaves vehicle execution behind the existing command/vehicle boundaries. Command lifecycle feedback remains the source of execution reconciliation.

## V0.9.82 command submission API

`POST /v1/missions/{missionID}/commands` accepts an operator command request with an explicit `command_id`, `target`, `command`, and positive `sequence_number`. The dispatcher constructs a `command.v1` GroundRecord with lifecycle `RECEIVED` and sends it through the existing ingestion boundary, preserving PostgreSQL durability, Redis projection, Kafka publication, and downstream command lifecycle/execution reconciliation.

The dispatch boundary does not execute a vehicle command itself. Vehicle execution remains behind the existing command/vehicle adapter and feedback path.

Recommended environment variable:

`TRISHULA_GROUND_COMMAND_SOURCE_NODE` — defaults to `GROUND-OPS-01`.


## V0.9.84 command reliability

V0.9.86 makes command reliability a first-class execution-control layer. The orchestrator executes vehicle commands with a bounded reliability policy. Each attempt has a deadline; transient execution failures are retried with exponential backoff up to the configured attempt limit. Explicit rejection/invalid/unsupported statuses are not retried. Exhausted timeouts reconcile as `TIMEOUT`, while other exhausted failures reconcile as `FAILED`.

## TRISHULA V0.9.85 — Command Scheduling & Execution Control

V0.9.85 adds a persistent, restart-recoverable scheduling layer above the verified V0.9.84 execution orchestrator. Scheduled commands are stored as append-only `COMMAND_SCHEDULE` event records, reconstructed on startup, dispatched when due, and support cancellation before dispatch.

Endpoints:
- `POST /v1/missions/{missionID}/commands/schedule`
- `GET /v1/commands/scheduled?mission_id=TRISHULA`
- `GET /v1/commands/scheduled/{scheduleID}`
- `POST /v1/commands/scheduled/{scheduleID}/cancel`
- `GET /v1/commands/scheduler`

The existing V0.9.84 execution path is reused unchanged for actual vehicle execution.


## V0.9.86 Reliability Controls

V0.9.86 adds explicit retry/failure-recovery observability and runtime policy controls on top of the existing V0.9.84 execution path. Transient failures and attempt timeouts are retried up to `MaxAttempts` with bounded exponential backoff. Explicit rejection/invalid/unsupported statuses are not retried. Each retry emits a `COMMAND_RETRY` event, and the orchestrator snapshot exposes the active retry attempt and policy.

Optional environment variables:

- `TRISHULA_COMMAND_MAX_ATTEMPTS` (default `3`)
- `TRISHULA_COMMAND_ATTEMPT_TIMEOUT` (default `10s`)
- `TRISHULA_COMMAND_INITIAL_BACKOFF` (default `250ms`)
- `TRISHULA_COMMAND_MAX_BACKOFF` (default `2s`)

Existing V0.9.81 reconciliation and V0.9.85 scheduling contracts remain unchanged.

## TRISHULA V0.9.87 — Command Timeout & Dead-Letter Recovery

V0.9.87 adds a durable dead-letter recovery boundary after V0.9.86 bounded retry/failure recovery. When a command reaches an exhausted retryable failure or timeout, the original command record and terminal execution state are captured in the command dead-letter queue. Dead-letter actions are persisted through the existing ground-record durability path, so pending entries can be reconstructed after service restart.

APIs:
- `GET /v1/commands/dlq?mission_id=TRISHULA` — list dead-letter entries and DLQ snapshot
- `GET /v1/commands/dlq/{deadLetterID}` — inspect one dead-letter entry
- `POST /v1/commands/dlq/{deadLetterID}/replay` — replay a pending command through the existing V0.9.86 orchestrator
- `POST /v1/commands/dlq/{deadLetterID}/resolve` — mark a dead-letter entry resolved without replay

Replay is idempotency-protected by the existing orchestrator command terminal/in-flight maps. A replayed command is returned to the normal `RECEIVED` boundary and then follows the existing scheduling/execution/reconciliation path.

The V0.9.85 scheduler and V0.9.84 vehicle bridge remain separate boundaries. V0.9.87 does not replace their contracts.


## TRISHULA V0.9.89 — Mission / Command Query & Operational History

V0.9.89 adds read-only operational history APIs on top of the existing durable `RecordRepository`. The APIs reuse the existing record query implementation, preserve the command execution boundary, and return records in the repository's deterministic newest-first ordering.

Endpoints:
- `GET /v1/missions/{missionID}/history`
- `GET /v1/missions/{missionID}/commands/{commandID}/history`

Supported filters are the existing record-query filters: `kind`, `source_node`, `correlation_id`, `min_mission_time_ns`, `max_mission_time_ns`, `min_sequence`, `max_sequence`, and `limit` (up to 500 for history).


## V0.9.91 — Mission Command Views

Adds the operator-facing GET `/v1/missions/{missionID}/commands` view. It groups durable command records by command correlation ID and overlays the latest execution reconciliation and dead-letter state without changing command execution or scheduling paths.

## TRISHULA V0.9.93 — Mission Telemetry & Event Views

V0.9.93 adds read-only mission-scoped telemetry and event views on top of the existing durable `RecordRepository`. The views reuse the deterministic repository ordering and do not alter ingestion, Kafka, processing, command execution, scheduling, reconciliation, or dead-letter paths.

Endpoints:
- `GET /v1/missions/{missionID}/telemetry`
- `GET /v1/missions/{missionID}/events`

Supported filters:
- `source_node`
- `correlation_id`
- `limit` (1–500; default 50)

Each response includes the API version, mission ID, record kind, count/limit, generation time, and durable records. Telemetry and event records remain separate views so consumers do not need to filter mixed mission history themselves.


## V0.9.94 — API Contract Hardening & Integration

Adds a read-only API contract descriptor at `GET /v1/api/contract`. It publishes the stable Ground API contract name/version, JSON content type, request-ID header convention, resource method/path inventory, and the standard error response shape. Existing operational paths remain unchanged.


## TRISHULA V0.9.95 — Ground API & Integration Completion

V0.9.95 completes the ground API/integration layer with a read-only integration status endpoint. The endpoint exposes the stable `ground-api-v1` contract version and the configured/disabled status of the ground service integration components without changing their execution boundaries.

Endpoint:
- `GET /v1/api/integration`

The response reports the API contract version, overall integration status (`ready` when all integration components are configured, otherwise `degraded`), and component states for storage, Redis cache, Kafka publisher/consumer, command orchestrator, scheduler, and dead-letter queue.

## TRISHULA V0.9.100 — Live Command / Execution Updates

V0.9.100 extends the Mission Control WebSocket gateway with real-time command lifecycle and execution projections. Accepted command lifecycle records and command-class execution events are consumed from Kafka and projected as `mission_control.command_execution` messages. V0.9.99 telemetry streaming remains unchanged and continues to use its own telemetry stream version.

## TRISHULA V0.9.104 — Mission Control Dashboard Data

V0.9.104 adds a read-only dashboard aggregation boundary for Mission Control. It composes existing mission-state, vehicle-state, health, freshness, alert, command-execution, telemetry-pipeline, and event-pipeline projections without introducing a second state owner.

Endpoint:
- `GET /v1/mission-control/dashboard`

Dashboard data includes:
- mission count and aggregate vehicle/alert/command summaries
- per-mission health, readiness, and freshness
- per-mission active/total alerts and command execution outcomes
- per-vehicle health, latest sequence/kind, metrics/subsystems, faults, and presence flags
- accepted telemetry/event pipeline counters

The endpoint is read-only and uses existing `RecordRouter` and `KafkaConsumer` projections. The dashboard version is `v0.9.104`; the WebSocket alert/telemetry/command stream versions remain unchanged for compatibility.


## TRISHULA V0.9.105 — Mission Control Integration & Hardening

V0.9.105 hardens the existing Mission Control surface without introducing a second state owner or a new transport protocol. It adds regression coverage for cross-component dashboard aggregation, deterministic mission/vehicle ordering, Mission Control API resource completeness, and stable API contract inventory.

The milestone preserves the existing Mission Control boundaries introduced in V0.9.96–V0.9.104:
- mission and vehicle state remain owned by `MissionStateEngine`
- dashboard remains a read-only aggregation over existing `KafkaConsumer` projections
- alerts remain owned by `MissionControlAlertManager`
- real-time telemetry, command-execution, and alert streams retain their existing WebSocket versions

The Ground API contract and integration-status surfaces are advanced to `v0.9.105` so the service metadata does not report stale pre-hardening versions.

No new database, Kafka topic, WebSocket protocol, or Mission Control state owner is introduced by V0.9.105.

### V0.9.109.1 — bounded mission-history Top-K query selection

V0.9.109.1 optimizes bounded operational history queries with a fixed-size min-heap. When a query requests a small positive `limit` against a larger input set, the service retains only the best `k` matching records and sorts those retained records for the final newest-first response. This avoids sorting the complete matching history when only a small page is required. Full-result queries and established ordering/filter semantics remain unchanged.
