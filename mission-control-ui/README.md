# TRISHULA UI V0.9.114.1 — Mission Navigation

Additive continuation of V0.9.113.1.

- UI: V0.4.7
- Backend API baseline: V0.9.113.1
- First fully integrated navigation module: Mission
- Spacecraft/runtime view preserved unchanged in behavior.
- No fake telemetry or UI-only mission state.


## V0.9.115.1 — Rover Navigation
- Enabled the Rover Mission Control navigation module.
- Rover page reads existing Ground mission-state and telemetry data for rover vehicles only.
- Missing rover fields remain `—`; no UI-generated telemetry values are introduced.
- Added `Sudheer .Dev` identity to the top navigation and footer.
- UI package version updated to 0.4.8.



$env:TRISHULA_GROUND_DB_URL="postgres://trishula:trishula_dev@localhost:5433/trishula_ground?sslmode=disable"
$env:TRISHULA_GROUND_REDIS_ADDR="localhost:6379"
$env:TRISHULA_GROUND_KAFKA_BROKERS="localhost:9092"
$env:TRISHULA_GROUND_KAFKA_TOPIC="trishula.ground.records"
$env:TRISHULA_GROUND_KAFKA_CONSUMER_GROUP="trishula-ground-consumers"


go run -tags postgres .

./build/trishula_physical_live_uplink.exe --api http://127.0.0.1:8082/v1/ingest --ticks 10000 --interval-ms 100