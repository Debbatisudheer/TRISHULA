# TRISHULA V0.9.59 Ground PostgreSQL Layer

V0.9.59 establishes the PostgreSQL storage contract without changing the live JSONL default.

## Why this layer is separate

The Ground Data Service already has a `RecordStore` interface. JSONL remains the local-first durable backend. PostgreSQL is introduced as a database boundary with a stable schema and migrations so the later distributed service can switch storage without changing packet/record contracts.

## Start PostgreSQL locally

From this directory:

```powershell
docker compose up -d
```

PostgreSQL is exposed on host port `5433`.

Connection details for local development:

```text
host=localhost
port=5433
dbname=trishula_ground
user=trishula
password=trishula_dev
```

The migrations in `db/migrations` are applied automatically on first container initialization.

## Verify

```powershell
docker ps
```

For a shell inside the database container:

```powershell
docker exec -it trishula-ground-postgres psql -U trishula -d trishula_ground
```

Then:

```sql
\dt
\d ground_records
```

## Important

The default V0.9.59 Go service continues to use JSONL. The PostgreSQL adapter is supplied behind an explicit build tag in `services/ground-data-service/postgres_store_postgres.go`; it is not enabled in the default test build because the pgx driver is an external dependency and this environment cannot fetch modules from the public Go proxy.
