package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

func main() {
	port := os.Getenv("TRISHULA_GROUND_DATA_PORT")
	if port == "" {
		port = "8082"
	}
	maxRecords := 10000
	if raw := os.Getenv("TRISHULA_GROUND_DATA_MAX_RECORDS"); raw != "" {
		if parsed, err := strconv.Atoi(raw); err == nil && parsed > 0 {
			maxRecords = parsed
		}
	}
	redisAddr := os.Getenv("TRISHULA_GROUND_REDIS_ADDR")
	storagePath := os.Getenv("TRISHULA_GROUND_DATA_FILE")
	if storagePath == "" {
		storagePath = filepath.Join("data", "ground", "records.jsonl")
	}

	store, storageDescription, err := openGroundStore(context.Background(), storagePath)
	if err != nil {
		log.Fatalf("initialize ground storage: %v", err)
	}
	var cache StateCache
	cacheDescription := "disabled"
	if strings.TrimSpace(redisAddr) != "" {
		cache = NewRedisStateCache(redisAddr)
		cacheDescription = redisAddr
	}
	var publisher RecordPublisher
	publisherDescription := "disabled"
	var consumer *KafkaConsumer
	consumerDescription := "disabled"
	kafkaBrokers := os.Getenv("TRISHULA_GROUND_KAFKA_BROKERS")
	kafkaTopic := os.Getenv("TRISHULA_GROUND_KAFKA_TOPIC")
	if strings.TrimSpace(kafkaBrokers) != "" || strings.TrimSpace(kafkaTopic) != "" {
		if strings.TrimSpace(kafkaBrokers) == "" || strings.TrimSpace(kafkaTopic) == "" {
			log.Fatalf("kafka configuration requires both TRISHULA_GROUND_KAFKA_BROKERS and TRISHULA_GROUND_KAFKA_TOPIC")
		}
		kafkaPublisher, publisherErr := NewKafkaRecordPublisher(kafkaBrokers, kafkaTopic)
		if publisherErr != nil {
			log.Fatalf("initialize kafka publisher: %v", publisherErr)
		}
		publisher = kafkaPublisher
		publisherDescription = kafkaBrokers + " topic=" + kafkaTopic
		if groupID := strings.TrimSpace(os.Getenv("TRISHULA_GROUND_KAFKA_CONSUMER_GROUP")); groupID != "" {
			consumer, err = NewKafkaRecordConsumer(kafkaBrokers, kafkaTopic, groupID)
			if err != nil {
				log.Fatalf("initialize kafka consumer: %v", err)
			}
			consumerDescription = "group=" + groupID + " topic=" + kafkaTopic
		}
	}
	service := newService(maxRecords, store, cache, publisher)
	var physicalMissionRuntime *physicalMissionRuntimeBridge
	runtimeCtx, runtimeCancel := context.WithCancel(context.Background())
	if runtimePath := physicalMissionRuntimePath(); strings.TrimSpace(runtimePath) != "" {
		if _, statErr := os.Stat(runtimePath); statErr == nil {
			physicalMissionRuntime, err = NewPhysicalMissionRuntimeBridge(runtimePath)
			if err != nil {
				log.Printf("initialize physical mission runtime bridge: %v", err)
			} else {
				log.Printf("physical mission runtime bridge enabled: %s", runtimePath)
				startPhysicalMissionRuntimeTicker(runtimeCtx, physicalMissionRuntime, service, "TRISHULA", 1*time.Second)
			}
		}
	}
	if consumer != nil {
		if repository, ok := store.(RecordRepository); ok {
			if err := consumer.AlertManager().Restore(context.Background(), repository, ""); err != nil {
				log.Printf("restore mission control alerts: %v", err)
			}
		}
	}
	deadLetterQueue := NewCommandDeadLetterQueue(service.Ingest, service.LoadRecords)
	var orchestrator *CommandExecutionOrchestrator
	var scheduler *CommandScheduler
	vehicleBridgePath := strings.TrimSpace(os.Getenv("TRISHULA_VEHICLE_BRIDGE_PATH"))
	if vehicleBridgePath == "" {
		vehicleBridgePath = filepath.Join("..", "..", "build", "trishula_ground_command_bridge.exe")
	}
	if _, statErr := os.Stat(vehicleBridgePath); statErr == nil {
		bridge, bridgeErr := NewCPPVehicleCommandBridge(vehicleBridgePath)
		if bridgeErr != nil {
			log.Printf("initialize vehicle command bridge: %v", bridgeErr)
		} else {
			policy := CommandReliabilityPolicyFromEnv()
			orchestrator, err = NewCommandExecutionOrchestratorWithPolicy(context.Background(), bridge, service.Ingest, 1, 64, policy)
			if err != nil {
				_ = bridge.Close()
				log.Printf("initialize command execution orchestrator: %v", err)
			}
		}
	} else {
		log.Printf("vehicle command bridge disabled: %s does not exist", vehicleBridgePath)
	}
	if orchestrator != nil {
		orchestrator.SetDeadLetterQueue(deadLetterQueue)
		deadLetterQueue.SetReplay(orchestrator.Replay)
	}
	if consumer != nil && orchestrator != nil {
		consumer.AttachCommandOrchestrator(orchestrator)
	}
	defer func() {
		runtimeCancel()
		if physicalMissionRuntime != nil {
			if err := physicalMissionRuntime.Close(); err != nil {
				log.Printf("close physical mission runtime bridge: %v", err)
			}
		}
		if scheduler != nil {
			if err := scheduler.Close(); err != nil {
				log.Printf("close command scheduler: %v", err)
			}
		}
		if orchestrator != nil {
			if err := orchestrator.Close(); err != nil {
				log.Printf("close command orchestrator: %v", err)
			}
		}
		if consumer != nil {
			if err := consumer.Close(); err != nil {
				log.Printf("close kafka consumer: %v", err)
			}
		}
		if publisher != nil {
			if err := publisher.Close(); err != nil {
				log.Printf("close kafka publisher: %v", err)
			}
		}
		if err := service.Close(); err != nil {
			log.Printf("close ground storage: %v", err)
		}
	}()

	mux := http.NewServeMux()
	mux.HandleFunc("GET /v1/missions/{missionID}/runtime", func(w http.ResponseWriter, r *http.Request) {
		missionID := strings.TrimSpace(r.PathValue("missionID"))
		if missionID == "" {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
			return
		}
		if physicalMissionRuntime == nil {
			writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "physical mission runtime is not configured"})
			return
		}
		snap := physicalMissionRuntime.Snapshot()
		writeJSON(w, http.StatusOK, map[string]any{"mission_id": missionID, "runtime_version": currentPhysicalMissionRuntimeVersion, "runtime": snap})
	})
	mux.HandleFunc("POST /v1/missions/{missionID}/runtime/{action}", func(w http.ResponseWriter, r *http.Request) {
		missionID := strings.TrimSpace(r.PathValue("missionID"))
		action := strings.ToUpper(strings.TrimSpace(r.PathValue("action")))
		if missionID == "" {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
			return
		}
		if physicalMissionRuntime == nil {
			writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "physical mission runtime is not configured"})
			return
		}
		allowed := map[string]bool{"START": true, "PAUSE": true, "RESUME": true, "ABORT": true, "RESET": true}
		if !allowed[action] {
			writeJSON(w, http.StatusNotFound, map[string]string{"error": "unsupported runtime action"})
			return
		}
		snap, err := physicalMissionRuntime.Command(action)
		if err != nil {
			writeJSON(w, http.StatusConflict, map[string]any{"accepted": false, "mission_id": missionID, "runtime_version": currentPhysicalMissionRuntimeVersion, "runtime": snap, "error": err.Error()})
			return
		}
		writeJSON(w, http.StatusAccepted, map[string]any{"accepted": true, "mission_id": missionID, "action": action, "runtime_version": currentPhysicalMissionRuntimeVersion, "runtime": snap})
	})
	webSocketGateway := NewMissionControlWebSocketGateway()
	if consumer != nil {
		consumer.AttachWebSocketGateway(webSocketGateway)
	}

	mux.HandleFunc("GET /health", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, map[string]string{"status": "ok", "service": "trishula-ground-data-service"})
	})

	mux.HandleFunc("GET /v1/state", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, service.Snapshot())
	})

	if consumer != nil {
		mux.HandleFunc("GET /v1/stream/status", func(w http.ResponseWriter, _ *http.Request) {
			writeJSON(w, http.StatusOK, consumer.Snapshot())
		})
		mux.HandleFunc("GET /v1/mission/state", func(w http.ResponseWriter, _ *http.Request) {
			writeJSON(w, http.StatusOK, consumer.MissionState())
		})
		mux.HandleFunc("GET /v1/mission/state/{missionID}/vehicles", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			if missionID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
				return
			}
			vehicles := consumer.VehicleSummaries(missionID)
			if len(vehicles) == 0 {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "vehicle state not found"})
				return
			}
			writeJSON(w, http.StatusOK, map[string]any{"mission_id": missionID, "vehicles": vehicles, "count": len(vehicles)})
		})
		mux.HandleFunc("GET /v1/mission/state/{missionID}/vehicles/{sourceNode}", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			sourceNode := strings.TrimSpace(r.PathValue("sourceNode"))
			if missionID == "" || sourceNode == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID and sourceNode are required"})
				return
			}
			vehicle, found := consumer.Vehicle(missionID, sourceNode)
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "vehicle state not found"})
				return
			}
			writeJSON(w, http.StatusOK, vehicle)
		})
		mux.HandleFunc("GET /v1/mission/state/{missionID}/vehicles/{sourceNode}/subsystems", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			sourceNode := strings.TrimSpace(r.PathValue("sourceNode"))
			if missionID == "" || sourceNode == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID and sourceNode are required"})
				return
			}
			subsystems := consumer.Subsystems(missionID, sourceNode)
			if len(subsystems) == 0 {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "subsystem state not found"})
				return
			}
			writeJSON(w, http.StatusOK, map[string]any{"mission_id": missionID, "source_node": sourceNode, "subsystems": subsystems, "count": len(subsystems)})
		})
		mux.HandleFunc("GET /v1/mission/state/{missionID}/vehicles/{sourceNode}/subsystems/{subsystem}", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			sourceNode := strings.TrimSpace(r.PathValue("sourceNode"))
			subsystem := strings.TrimSpace(r.PathValue("subsystem"))
			if missionID == "" || sourceNode == "" || subsystem == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID, sourceNode, and subsystem are required"})
				return
			}
			state, found := consumer.Subsystem(missionID, sourceNode, subsystem)
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "subsystem state not found"})
				return
			}
			writeJSON(w, http.StatusOK, state)
		})
		mux.HandleFunc("GET /v1/mission/state/{missionID}/freshness", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			if missionID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
				return
			}
			warnAfter := 30 * time.Second
			criticalAfter := 2 * time.Minute
			if raw := strings.TrimSpace(r.URL.Query().Get("warn_after_ms")); raw != "" {
				parsed, err := strconv.ParseInt(raw, 10, 64)
				if err != nil || parsed <= 0 {
					writeJSON(w, http.StatusBadRequest, map[string]string{"error": "warn_after_ms must be a positive integer"})
					return
				}
				warnAfter = time.Duration(parsed) * time.Millisecond
			}
			if raw := strings.TrimSpace(r.URL.Query().Get("critical_after_ms")); raw != "" {
				parsed, err := strconv.ParseInt(raw, 10, 64)
				if err != nil || parsed <= 0 {
					writeJSON(w, http.StatusBadRequest, map[string]string{"error": "critical_after_ms must be a positive integer"})
					return
				}
				criticalAfter = time.Duration(parsed) * time.Millisecond
			}
			freshness, found, err := consumer.MissionFreshness(missionID, time.Now().UTC(), warnAfter, criticalAfter)
			if err != nil {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "mission freshness not found"})
				return
			}
			writeJSON(w, http.StatusOK, freshness)
		})
		mux.HandleFunc("GET /v1/mission/state/{missionID}/health", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			if missionID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
				return
			}
			health, found := consumer.MissionHealth(missionID)
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "mission health not found"})
				return
			}
			writeJSON(w, http.StatusOK, health)
		})
		mux.HandleFunc("GET /v1/mission/state/{missionID}/consistency", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			if missionID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
				return
			}
			repository, ok := store.(RecordRepository)
			if !ok {
				writeJSON(w, http.StatusNotImplemented, map[string]string{"error": "durable repository does not support consistency queries"})
				return
			}
			consistency, found, err := consumer.MissionStateConsistency(r.Context(), repository, missionID, time.Now().UTC())
			if err != nil {
				writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "mission state not available"})
				return
			}
			writeJSON(w, http.StatusOK, consistency)
		})
		mux.HandleFunc("POST /v1/mission/state/{missionID}/synchronize", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			if missionID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
				return
			}
			repository, ok := store.(RecordRepository)
			if !ok {
				writeJSON(w, http.StatusNotImplemented, map[string]string{"error": "durable repository does not support state synchronization"})
				return
			}
			syncResult, found, err := consumer.SynchronizeMissionState(r.Context(), repository, missionID, time.Now().UTC())
			if err != nil {
				writeJSON(w, http.StatusInternalServerError, map[string]any{"error": err.Error(), "synchronization": syncResult})
				return
			}
			if !found {
				writeJSON(w, http.StatusNotFound, syncResult)
				return
			}
			writeJSON(w, http.StatusOK, syncResult)
		})
		mux.HandleFunc("GET /v1/mission/state/{missionID}/health-state", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			if missionID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
				return
			}
			warnAfter := defaultHealthWarnAfter
			criticalAfter := defaultHealthCriticalAfter
			if raw := strings.TrimSpace(r.URL.Query().Get("warn_after_ms")); raw != "" {
				parsed, err := strconv.ParseInt(raw, 10, 64)
				if err != nil || parsed <= 0 {
					writeJSON(w, http.StatusBadRequest, map[string]string{"error": "warn_after_ms must be a positive integer"})
					return
				}
				warnAfter = time.Duration(parsed) * time.Millisecond
			}
			if raw := strings.TrimSpace(r.URL.Query().Get("critical_after_ms")); raw != "" {
				parsed, err := strconv.ParseInt(raw, 10, 64)
				if err != nil || parsed <= 0 {
					writeJSON(w, http.StatusBadRequest, map[string]string{"error": "critical_after_ms must be a positive integer"})
					return
				}
				criticalAfter = time.Duration(parsed) * time.Millisecond
			}
			healthState, found, err := consumer.MissionHealthState(missionID, time.Now().UTC(), warnAfter, criticalAfter)
			if err != nil {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "mission health state not found"})
				return
			}
			writeJSON(w, http.StatusOK, healthState)
		})
		mux.HandleFunc("GET /v1/mission/state/{missionID}", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			if missionID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
				return
			}
			state, found := consumer.Mission(missionID)
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "mission state not found"})
				return
			}
			writeJSON(w, http.StatusOK, state)
		})
	}
	if consumer != nil {
		mux.HandleFunc("GET /v1/stream/routes", func(w http.ResponseWriter, _ *http.Request) {
			writeJSON(w, http.StatusOK, consumer.Routes())
		})
		mux.HandleFunc("GET /v1/processing/results", func(w http.ResponseWriter, _ *http.Request) {
			writeJSON(w, http.StatusOK, consumer.Results())
		})
		mux.HandleFunc("GET /v1/commands/lifecycle", func(w http.ResponseWriter, _ *http.Request) {
			writeJSON(w, http.StatusOK, consumer.CommandLifecycle())
		})
		mux.HandleFunc("GET /v1/commands/execution", func(w http.ResponseWriter, _ *http.Request) {
			writeJSON(w, http.StatusOK, consumer.CommandExecution())
		})
		if orchestrator != nil {
			mux.HandleFunc("GET /v1/commands/orchestrator", func(w http.ResponseWriter, _ *http.Request) {
				writeJSON(w, http.StatusOK, orchestrator.Snapshot())
			})
		}
		mux.HandleFunc("GET /v1/commands/dlq", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.URL.Query().Get("mission_id"))
			writeJSON(w, http.StatusOK, map[string]any{"entries": deadLetterQueue.List(missionID), "dlq": deadLetterQueue.Snapshot()})
		})
		mux.HandleFunc("GET /v1/commands/dlq/{deadLetterID}", func(w http.ResponseWriter, r *http.Request) {
			entry, found := deadLetterQueue.Get(r.PathValue("deadLetterID"))
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "dead-letter entry not found"})
				return
			}
			writeJSON(w, http.StatusOK, entry)
		})
		mux.HandleFunc("POST /v1/commands/dlq/{deadLetterID}/replay", func(w http.ResponseWriter, r *http.Request) {
			entry, err := deadLetterQueue.Replay(r.Context(), r.PathValue("deadLetterID"))
			if err != nil {
				status := http.StatusConflict
				if strings.Contains(err.Error(), "not found") {
					status = http.StatusNotFound
				}
				writeJSON(w, status, map[string]string{"error": err.Error()})
				return
			}
			writeJSON(w, http.StatusAccepted, entry)
		})
		mux.HandleFunc("POST /v1/commands/dlq/{deadLetterID}/resolve", func(w http.ResponseWriter, r *http.Request) {
			entry, err := deadLetterQueue.Resolve(r.Context(), r.PathValue("deadLetterID"))
			if err != nil {
				status := http.StatusConflict
				if strings.Contains(err.Error(), "not found") {
					status = http.StatusNotFound
				}
				writeJSON(w, status, map[string]string{"error": err.Error()})
				return
			}
			writeJSON(w, http.StatusOK, entry)
		})
		mux.HandleFunc("GET /v1/missions/{missionID}/commands/{commandID}/detail", func(w http.ResponseWriter, r *http.Request) {
			handleMissionCommandDetailView(w, r, store, consumer, deadLetterQueue)
		})

		mux.HandleFunc("GET /v1/missions/{missionID}/commands/{commandID}", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			commandID := strings.TrimSpace(r.PathValue("commandID"))
			if missionID == "" || commandID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID and commandID are required"})
				return
			}

			repository, ok := store.(RecordRepository)
			if !ok {
				writeJSON(w, http.StatusNotImplemented, map[string]string{"error": "durable repository does not support command queries"})
				return
			}
			records, err := repository.Query(r.Context(), RecordQuery{
				MissionID:     missionID,
				CorrelationID: commandID,
				Limit:         200,
			})
			if err != nil {
				writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			status := CommandIntegrationStatus{
				APIEnvelope: APIEnvelope{RequestID: requestID(r), Version: currentGroundAPIIntegrationVersion},
				MissionID:   missionID,
				CommandID:   commandID,
				RecordCount: len(records),
			}
			for _, lifecycle := range consumer.router.CommandLifecycle() {
				if lifecycle.CommandID == commandID {
					copy := lifecycle
					status.Lifecycle = &copy
					break
				}
			}
			if execution, ok := consumer.router.CommandExecutionByID(commandID); ok {
				status.Execution = &execution
			}
			for _, entry := range deadLetterQueue.List(missionID) {
				if entry.CommandID == commandID {
					copy := entry
					status.DeadLetter = &copy
					break
				}
			}
			if status.RecordCount == 0 && status.Lifecycle == nil && status.Execution == nil && status.DeadLetter == nil {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "command not found"})
				return
			}
			w.Header().Set("X-TRISHULA-API-VERSION", currentGroundAPIIntegrationVersion)
			writeJSON(w, http.StatusOK, status)
		})
		mux.HandleFunc("GET /v1/api", func(w http.ResponseWriter, r *http.Request) {
			writeJSON(w, http.StatusOK, GroundAPIInfo{
				APIEnvelope: APIEnvelope{RequestID: requestID(r), Version: currentGroundAPIIntegrationVersion},
				Service:     "trishula-ground-data-service",
				Contract:    "ground-api-v1",
				Resources: []string{
					"/v1/missions/{missionID}/commands",
					"/v1/missions/{missionID}/telemetry",
					"/v1/missions/{missionID}/events",
					"/v1/missions/{missionID}/commands/{commandID}",
					"/v1/missions/{missionID}/commands/{commandID}/detail",
					"/v1/missions/{missionID}/commands/schedule",
					"/v1/commands/execution/{commandID}",
					"/v1/commands/dlq",
					"/v1/missions/{missionID}/history",
					"/v1/missions/{missionID}/commands/{commandID}/history",
					"/v1/commands/search",
					"/v1/mission-control",
					"/v1/mission-control/state",
					"/v1/mission-control/dashboard",
					"/v1/mission-control/ws",
					"/v1/mission-control/alerts",
					"/v1/mission-control/alerts/{alertID}",
					"/v1/mission-control/alerts/{alertID}/detail",
					"/v1/mission-control/alerts/{alertID}/history",
					"/v1/mission-control/alerts/{alertID}/acknowledge",
					"/v1/mission-control/alerts/{alertID}/clear",
				},
			})
		})
		mux.HandleFunc("GET /v1/api/contract", handleGroundAPIContract)
		mux.HandleFunc("GET /v1/api/integration", func(w http.ResponseWriter, r *http.Request) {
			handleGroundAPIIntegrationStatus(w, r, true, cache != nil, publisher != nil, consumer != nil, orchestrator != nil, scheduler != nil, deadLetterQueue != nil)
		})
		mux.HandleFunc("GET /v1/mission-control/dashboard", func(w http.ResponseWriter, r *http.Request) {
			handleMissionControlDashboard(w, r, consumer)
		})
		mux.HandleFunc("GET /v1/mission-control/state", func(w http.ResponseWriter, r *http.Request) {
			if consumer == nil {
				handleMissionControlState(w, r, nil)
				return
			}
			handleMissionControlState(w, r, consumer.router)
		})
		mux.Handle("GET /v1/mission-control/ws", webSocketGateway)
		mux.HandleFunc("GET /v1/mission-control/alerts/{alertID}", func(w http.ResponseWriter, r *http.Request) {
			if consumer == nil {
				writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "kafka consumer unavailable"})
				return
			}
			alertID := strings.TrimSpace(r.PathValue("alertID"))
			missionID := strings.TrimSpace(r.URL.Query().Get("mission_id"))
			if alertID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "alertID is required"})
				return
			}
			alert, ok := consumer.Alert(alertID, missionID)
			if !ok {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "alert not found"})
				return
			}
			w.Header().Set("X-TRISHULA-API-VERSION", currentMissionControlAlertVersion)
			writeJSON(w, http.StatusOK, alert)
		})
		mux.HandleFunc("GET /v1/mission-control/alerts/{alertID}/detail", func(w http.ResponseWriter, r *http.Request) {
			repository, ok := store.(RecordRepository)
			if !ok {
				writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "ground record repository unavailable"})
				return
			}
			handleMissionControlAlertDetail(w, r, consumer, repository)
		})
		mux.HandleFunc("GET /v1/mission-control/alerts/{alertID}/history", func(w http.ResponseWriter, r *http.Request) {
			repository, ok := store.(RecordRepository)
			if !ok {
				writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "ground record repository unavailable"})
				return
			}
			handleMissionControlAlertHistory(w, r, consumer, repository)
		})
		mux.HandleFunc("POST /v1/mission-control/alerts/{alertID}/acknowledge", func(w http.ResponseWriter, r *http.Request) {
			if consumer == nil {
				writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "kafka consumer unavailable"})
				return
			}
			alertID := strings.TrimSpace(r.PathValue("alertID"))
			if alertID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "alertID is required"})
				return
			}
			var action MissionControlAlertLifecycleAction
			if err := json.NewDecoder(r.Body).Decode(&action); err != nil {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid request body"})
				return
			}
			if strings.TrimSpace(action.OperatorID) == "" {
				action.OperatorID = "GROUND-OPS-01"
			}
			missionID := strings.TrimSpace(r.URL.Query().Get("mission_id"))
			alert, ok := consumer.Alert(alertID, missionID)
			if !ok {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "alert not found"})
				return
			}
			if alert.Status == "CLEARED" {
				writeJSON(w, http.StatusConflict, map[string]string{"error": "cleared alert cannot be acknowledged"})
				return
			}
			if alert.Status != "ACKNOWLEDGED" {
				actionAlert := alert
				actionAlert.Status = "ACKNOWLEDGED"
				actionAlert.Lifecycle = string(EventAcknowledged)
				record := consumer.AlertManager().LifecycleRecord(actionAlert, "ACKNOWLEDGE", action.OperatorID, action.Reason, uint64(time.Now().UTC().UnixNano()))
				result := service.Ingest(record)
				if !result.Accepted {
					writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "failed to persist alert acknowledgement: " + result.Reason})
					return
				}
				var err error
				alert, err = consumer.AlertManager().Acknowledge(alertID, missionID, action.OperatorID, action.Reason)
				if err != nil {
					writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
					return
				}
			}
			w.Header().Set("X-TRISHULA-API-VERSION", currentMissionControlAlertVersion)
			writeJSON(w, http.StatusOK, alert)
		})
		mux.HandleFunc("POST /v1/mission-control/alerts/{alertID}/clear", func(w http.ResponseWriter, r *http.Request) {
			if consumer == nil {
				writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "kafka consumer unavailable"})
				return
			}
			alertID := strings.TrimSpace(r.PathValue("alertID"))
			if alertID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "alertID is required"})
				return
			}
			var action MissionControlAlertLifecycleAction
			if err := json.NewDecoder(r.Body).Decode(&action); err != nil {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid request body"})
				return
			}
			if strings.TrimSpace(action.OperatorID) == "" {
				action.OperatorID = "GROUND-OPS-01"
			}
			missionID := strings.TrimSpace(r.URL.Query().Get("mission_id"))
			alert, ok := consumer.Alert(alertID, missionID)
			if !ok {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "alert not found"})
				return
			}
			if alert.Status != "CLEARED" {
				actionAlert := alert
				actionAlert.Status = "CLEARED"
				actionAlert.Lifecycle = string(EventCleared)
				record := consumer.AlertManager().LifecycleRecord(actionAlert, "CLEAR", action.OperatorID, action.Reason, uint64(time.Now().UTC().UnixNano()))
				result := service.Ingest(record)
				if !result.Accepted {
					writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "failed to persist alert clear: " + result.Reason})
					return
				}
				var err error
				alert, err = consumer.AlertManager().Clear(alertID, missionID, action.OperatorID, action.Reason)
				if err != nil {
					writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
					return
				}
			}
			w.Header().Set("X-TRISHULA-API-VERSION", currentMissionControlAlertVersion)
			writeJSON(w, http.StatusOK, alert)
		})
		mux.HandleFunc("GET /v1/mission-control/alerts", func(w http.ResponseWriter, r *http.Request) {
			if consumer == nil {
				writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "kafka consumer unavailable"})
				return
			}
			missionID := strings.TrimSpace(r.URL.Query().Get("mission_id"))
			w.Header().Set("X-TRISHULA-API-VERSION", currentMissionControlAlertVersion)
			writeJSON(w, http.StatusOK, consumer.Alerts(missionID))
		})
		mux.HandleFunc("GET /v1/mission-control", func(w http.ResponseWriter, r *http.Request) {
			var missionService *MissionService
			if consumer != nil {
				repository, ok := store.(RecordRepository)
				if ok {
					missionService = NewMissionService(consumer.router, repository)
				}
			}
			handleMissionControlSnapshot(w, r, missionService, consumer != nil)
		})
		mux.HandleFunc("GET /v1/commands/execution/{commandID}", func(w http.ResponseWriter, r *http.Request) {
			commandID := strings.TrimSpace(r.PathValue("commandID"))
			if commandID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "commandID is required"})
				return
			}
			state, found := consumer.CommandExecutionByID(commandID)
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "command execution state not found"})
				return
			}
			writeJSON(w, http.StatusOK, state)
		})
	}

	if cache != nil {
		mux.HandleFunc("GET /v1/state/cache", func(w http.ResponseWriter, r *http.Request) {
			state, found, err := cache.GetSnapshot(r.Context())
			if err != nil {
				writeJSON(w, http.StatusBadGateway, map[string]string{"error": err.Error()})
				return
			}
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "redis state not found"})
				return
			}
			writeJSON(w, http.StatusOK, state)
		})
	}

	repository, repositoryOK := store.(RecordRepository)
	mux.HandleFunc("GET /v1/missions/{missionID}/history", func(w http.ResponseWriter, r *http.Request) {
		missionID := strings.TrimSpace(r.PathValue("missionID"))
		if missionID == "" {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
			return
		}
		if !repositoryOK {
			writeJSON(w, http.StatusNotImplemented, map[string]string{"error": "durable repository does not support operational history"})
			return
		}
		query, err := parseOperationalHistoryQuery(r, missionID)
		if err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		records, err := repository.Query(r.Context(), query)
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
			return
		}
		view := buildOperationalHistoryView(missionID, records, query.Limit)
		view.RequestID = requestID(r)
		writeJSON(w, http.StatusOK, view)
	})
	mux.HandleFunc("GET /v1/missions/{missionID}/commands/{commandID}/history", func(w http.ResponseWriter, r *http.Request) {
		missionID := strings.TrimSpace(r.PathValue("missionID"))
		commandID := strings.TrimSpace(r.PathValue("commandID"))
		if missionID == "" || commandID == "" {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID and commandID are required"})
			return
		}
		if !repositoryOK {
			writeJSON(w, http.StatusNotImplemented, map[string]string{"error": "durable repository does not support command history"})
			return
		}
		query, err := parseOperationalHistoryQuery(r, missionID)
		if err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		query.CorrelationID = commandID
		records, err := repository.Query(r.Context(), query)
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
			return
		}
		view := buildOperationalHistoryView(missionID, records, query.Limit)
		view.CommandID = commandID
		view.RequestID = requestID(r)
		writeJSON(w, http.StatusOK, view)
	})

	if repositoryOK {
		mux.HandleFunc("GET /v1/commands/search", func(w http.ResponseWriter, r *http.Request) {
			query, pageSize, err := parseCommandSearchQuery(r)
			if err != nil {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			records, err := repository.Query(r.Context(), query)
			if err != nil {
				writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			view := buildCommandSearchView(r, records, pageSize)
			w.Header().Set("X-TRISHULA-API-VERSION", currentCommandSearchVersion)
			writeJSON(w, http.StatusOK, view)
		})

		mux.HandleFunc("GET /v1/records", func(w http.ResponseWriter, r *http.Request) {
			query, err := parseRecordQuery(r)
			if err != nil {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			records, err := repository.Query(r.Context(), query)
			if err != nil {
				writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			writeJSON(w, http.StatusOK, map[string]any{"records": records, "count": len(records)})
		})

		mux.HandleFunc("GET /v1/records/{recordID}", func(w http.ResponseWriter, r *http.Request) {
			recordID := strings.TrimSpace(r.PathValue("recordID"))
			if recordID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "recordID is required"})
				return
			}
			record, found, err := repository.GetByID(r.Context(), recordID)
			if err != nil {
				writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "record not found"})
				return
			}
			writeJSON(w, http.StatusOK, record)
		})
	}

	if consumer != nil {
		missionService := NewMissionService(consumer.router, repository)
		mux.HandleFunc("GET /v1/missions", func(w http.ResponseWriter, r *http.Request) {
			missions := missionService.List(r.Context(), time.Now().UTC())
			writeJSON(w, http.StatusOK, map[string]any{"missions": missions, "count": len(missions)})
		})
		mux.HandleFunc("GET /v1/missions/{missionID}", func(w http.ResponseWriter, r *http.Request) {
			detail, found, err := missionService.Get(r.Context(), r.PathValue("missionID"), time.Now().UTC())
			if err != nil {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "mission not found"})
				return
			}
			writeJSON(w, http.StatusOK, detail)
		})
		mux.HandleFunc("POST /v1/missions/{missionID}/synchronize", func(w http.ResponseWriter, r *http.Request) {
			result, found, err := missionService.Synchronize(r.Context(), r.PathValue("missionID"), time.Now().UTC())
			if err != nil {
				writeJSON(w, http.StatusInternalServerError, map[string]any{"error": err.Error(), "synchronization": result})
				return
			}
			if !found {
				writeJSON(w, http.StatusNotFound, result)
				return
			}
			writeJSON(w, http.StatusOK, result)
		})
		operationsService := NewMissionOperationsService(consumer.router, repository)
		commandSourceNode := strings.TrimSpace(os.Getenv("TRISHULA_GROUND_COMMAND_SOURCE_NODE"))
		commandDispatcher := NewMissionCommandDispatcher(service, commandSourceNode)

		schedulerPoll := time.Second
		if raw := strings.TrimSpace(os.Getenv("TRISHULA_COMMAND_SCHEDULER_POLL")); raw != "" {
			if d, parseErr := time.ParseDuration(raw); parseErr == nil && d >= 100*time.Millisecond {
				schedulerPoll = d
			}
		}
		scheduler, err = NewCommandScheduler(context.Background(), service, commandDispatcher, schedulerPoll)
		if err != nil {
			log.Printf("initialize command scheduler: %v", err)
		}
		mux.HandleFunc("GET /v1/missions/{missionID}/commands", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			if missionID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
				return
			}
			if !repositoryOK {
				writeJSON(w, http.StatusNotImplemented, map[string]string{"error": "durable repository does not support mission command views"})
				return
			}
			limit, err := parseMissionCommandViewLimit(r)
			if err != nil {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			records, err := repository.Query(r.Context(), RecordQuery{MissionID: missionID, Kind: KindCommand, Limit: 10000})
			if err != nil {
				writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			views := buildMissionCommandViews(missionID, records, consumer.router.CommandExecutionForMission(missionID), deadLetterQueue.List(missionID), limit)
			views.RequestID = requestID(r)
			views.Limit = limit
			w.Header().Set("X-TRISHULA-API-VERSION", currentMissionCommandViewsVersion)
			writeJSON(w, http.StatusOK, views)
		})
		mux.HandleFunc("GET /v1/missions/{missionID}/telemetry", func(w http.ResponseWriter, r *http.Request) {
			handleMissionDataView(w, r, store, KindTelemetry)
		})
		mux.HandleFunc("GET /v1/missions/{missionID}/events", func(w http.ResponseWriter, r *http.Request) {
			handleMissionDataView(w, r, store, KindEvent)
		})
		mux.HandleFunc("POST /v1/missions/{missionID}/commands", func(w http.ResponseWriter, r *http.Request) {
			missionID := strings.TrimSpace(r.PathValue("missionID"))
			if missionID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
				return
			}
			defer r.Body.Close()
			var request CommandDispatchRequest
			decoder := json.NewDecoder(r.Body)
			decoder.DisallowUnknownFields()
			if err := decoder.Decode(&request); err != nil {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid JSON: " + err.Error()})
				return
			}
			result, err := commandDispatcher.Submit(missionID, request)
			if err != nil {
				writeJSON(w, http.StatusUnprocessableEntity, map[string]string{"error": err.Error()})
				return
			}
			if !result.Accepted {
				status := http.StatusConflict
				if !result.Ingest.Duplicate {
					status = http.StatusInternalServerError
				}
				writeJSON(w, status, result)
				return
			}
			writeJSON(w, http.StatusAccepted, result)
		})
		if scheduler != nil {
			mux.HandleFunc("POST /v1/missions/{missionID}/commands/schedule", func(w http.ResponseWriter, r *http.Request) {
				defer r.Body.Close()
				var req CommandScheduleRequest
				decoder := json.NewDecoder(r.Body)
				decoder.DisallowUnknownFields()
				if err := decoder.Decode(&req); err != nil {
					writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid JSON: " + err.Error()})
					return
				}
				scheduled, err := scheduler.Create(r.PathValue("missionID"), req)
				if err != nil {
					writeJSON(w, http.StatusUnprocessableEntity, map[string]string{"error": err.Error()})
					return
				}
				writeJSON(w, http.StatusAccepted, map[string]any{"accepted": true, "schedule": scheduled, "scheduler_version": currentCommandSchedulerVersion})
			})
			mux.HandleFunc("GET /v1/commands/scheduled", func(w http.ResponseWriter, r *http.Request) {
				missionID := strings.TrimSpace(r.URL.Query().Get("mission_id"))
				writeJSON(w, http.StatusOK, map[string]any{"schedules": scheduler.List(missionID), "scheduler": scheduler.Snapshot()})
			})
			mux.HandleFunc("GET /v1/commands/scheduled/{scheduleID}", func(w http.ResponseWriter, r *http.Request) {
				scheduled, found := scheduler.Get(r.PathValue("scheduleID"))
				if !found {
					writeJSON(w, http.StatusNotFound, map[string]string{"error": "schedule not found"})
					return
				}
				writeJSON(w, http.StatusOK, scheduled)
			})
			mux.HandleFunc("POST /v1/commands/scheduled/{scheduleID}/cancel", func(w http.ResponseWriter, r *http.Request) {
				scheduled, err := scheduler.Cancel(r.PathValue("scheduleID"))
				if err != nil {
					status := http.StatusConflict
					if strings.Contains(err.Error(), "not found") {
						status = http.StatusNotFound
					}
					writeJSON(w, status, map[string]string{"error": err.Error()})
					return
				}
				writeJSON(w, http.StatusOK, scheduled)
			})
			mux.HandleFunc("GET /v1/commands/scheduler", func(w http.ResponseWriter, _ *http.Request) {
				writeJSON(w, http.StatusOK, scheduler.Snapshot())
			})
		}
		mux.HandleFunc("GET /v1/missions/{missionID}/operations", func(w http.ResponseWriter, r *http.Request) {
			limit := 50
			if raw := strings.TrimSpace(r.URL.Query().Get("timeline_limit")); raw != "" {
				parsed, err := strconv.Atoi(raw)
				if err != nil || parsed <= 0 {
					writeJSON(w, http.StatusBadRequest, map[string]string{"error": "timeline_limit must be a positive integer"})
					return
				}
				limit = parsed
			}
			view, found, err := operationsService.Operations(r.Context(), r.PathValue("missionID"), time.Now().UTC(), limit)
			if err != nil {
				writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			if !found {
				writeJSON(w, http.StatusNotFound, map[string]string{"error": "mission not found"})
				return
			}
			writeJSON(w, http.StatusOK, view)
		})
		mux.HandleFunc("GET /v1/missions/{missionID}/timeline", func(w http.ResponseWriter, r *http.Request) {
			limit := 50
			if raw := strings.TrimSpace(r.URL.Query().Get("limit")); raw != "" {
				parsed, err := strconv.Atoi(raw)
				if err != nil || parsed <= 0 {
					writeJSON(w, http.StatusBadRequest, map[string]string{"error": "limit must be a positive integer"})
					return
				}
				limit = parsed
			}
			timeline, err := operationsService.Timeline(r.Context(), r.PathValue("missionID"), limit)
			if err != nil {
				writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			writeJSON(w, http.StatusOK, map[string]any{"mission_id": r.PathValue("missionID"), "timeline": timeline, "count": len(timeline), "mission_operations_version": currentMissionOperationsVersion})
		})
	}

	mux.HandleFunc("POST /v1/ingest", func(w http.ResponseWriter, r *http.Request) {
		defer r.Body.Close()
		var record GroundRecord
		decoder := json.NewDecoder(r.Body)
		decoder.DisallowUnknownFields()
		if err := decoder.Decode(&record); err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid JSON: " + err.Error()})
			return
		}
		if err := validateRecord(record); err != nil {
			writeJSON(w, http.StatusUnprocessableEntity, map[string]string{"error": err.Error()})
			return
		}
		result := service.Ingest(record)
		status := http.StatusAccepted
		if result.Duplicate {
			status = http.StatusConflict
		} else if !result.Accepted {
			status = http.StatusInternalServerError
		}
		writeJSON(w, status, result)
	})

	log.Printf("TRISHULA ground data service listening on :%s", port)
	log.Printf("TRISHULA ground storage backend: %s", storageDescription)
	log.Printf("TRISHULA ground current-state cache: %s", cacheDescription)
	log.Printf("TRISHULA ground event stream: %s", publisherDescription)
	log.Printf("TRISHULA ground Kafka consumer: %s", consumerDescription)
	if orchestrator != nil {
		log.Printf("TRISHULA command execution orchestrator: %s", currentCommandOrchestratorVersion)
		log.Printf("TRISHULA command dead-letter queue: %s", currentCommandDeadLetterVersion)
	} else {
		log.Printf("TRISHULA command execution orchestrator: disabled")
	}
	log.Printf("TRISHULA command execution reconciliation: %s", currentCommandReconciliationVersion)
	if scheduler != nil {
		log.Printf("TRISHULA command scheduler: %s", currentCommandSchedulerVersion)
	}
	log.Fatal(http.ListenAndServe(":"+port, withRequestID(mux)))
}

func parseRecordQuery(r *http.Request) (RecordQuery, error) {
	values := r.URL.Query()
	query := RecordQuery{
		MissionID:     values.Get("mission_id"),
		SourceNode:    values.Get("source_node"),
		CorrelationID: values.Get("correlation_id"),
	}
	if kind := values.Get("kind"); kind != "" {
		query.Kind = DataKind(kind)
		if query.Kind != KindTelemetry && query.Kind != KindScience && query.Kind != KindEvent && query.Kind != KindCommand && query.Kind != KindFile {
			return RecordQuery{}, fmt.Errorf("unsupported kind %q", kind)
		}
	}
	var err error
	if raw := values.Get("min_mission_time_ns"); raw != "" {
		value, parseErr := strconv.ParseUint(raw, 10, 64)
		if parseErr != nil {
			return RecordQuery{}, fmt.Errorf("invalid min_mission_time_ns: %w", parseErr)
		}
		query.MinMissionTimeNS = &value
	}
	if raw := values.Get("max_mission_time_ns"); raw != "" {
		value, parseErr := strconv.ParseUint(raw, 10, 64)
		if parseErr != nil {
			return RecordQuery{}, fmt.Errorf("invalid max_mission_time_ns: %w", parseErr)
		}
		query.MaxMissionTimeNS = &value
	}
	if raw := values.Get("min_sequence"); raw != "" {
		value, parseErr := strconv.ParseUint(raw, 10, 64)
		if parseErr != nil {
			return RecordQuery{}, fmt.Errorf("invalid min_sequence: %w", parseErr)
		}
		query.MinSequence = &value
	}
	if raw := values.Get("max_sequence"); raw != "" {
		value, parseErr := strconv.ParseUint(raw, 10, 64)
		if parseErr != nil {
			return RecordQuery{}, fmt.Errorf("invalid max_sequence: %w", parseErr)
		}
		query.MaxSequence = &value
	}
	if raw := values.Get("limit"); raw != "" {
		query.Limit, err = strconv.Atoi(raw)
		if err != nil || query.Limit < 0 {
			return RecordQuery{}, fmt.Errorf("invalid limit")
		}
		if query.Limit > 1000 {
			query.Limit = 1000
		}
	}
	if err := validateQuery(query); err != nil {
		return RecordQuery{}, err
	}
	return query, nil
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}
