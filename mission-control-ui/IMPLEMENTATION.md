# TRISHULA Mission Control UI V0.3

## Scope
UI-only mission visualization upgrade built from the working V0.2 UI. The V0.9.105 backend is unchanged.

## Changes
- Added phase-aware SVG mission scenes for all nine mission phases.
- Added launch vehicle, spacecraft, lander, and rover visual models.
- Added transfer trajectory, orbit, descent and landing-site visualization.
- Added phase preview controls on the mission timeline.
- Added play/pause control for the active visualization.
- Preserved existing API proxy, live telemetry, alerts, state and dashboard integration.

## Verification
Run `npm install`, `npm run dev`, then verify the Mission page in the browser.


## V0.3.1 Reference Alignment Patch
Built from the exact supplied `trishula-ui-v0.3-mission-visualization` source.

Changed:
- `app/page.tsx`
- `lib/api.ts`
- `components/mission-visualization.tsx`
- `app/globals.css`
- `README.md`
- `IMPLEMENTATION.md`

Preserved:
- Next.js same-origin HTTP ground-service proxy
- live dashboard/state/alerts integration
- existing V0.3 phase visualization and controls
- V0.9.105 backend unchanged

Live-data improvements:
- dashboard/state/alerts/telemetry/events are loaded from the real ground service
- event feed uses returned event/alert records
- telemetry cards use returned telemetry fields where present
- WebSocket connects directly to the configured ground WebSocket endpoint instead of incorrectly treating the HTTP proxy route as a WebSocket upgrade

Reference-alignment improvements:
- six telemetry panels
- richer launch/spacecraft/Moon/rover media visuals
- expanded Earth and Moon visual detailing
- reference-style mission parameter and spacecraft status density

Verification note:
The package was statically prepared and inspected. `npm install` could not be completed in the build environment because package installation timed out; therefore no frontend build/test result is claimed here. Verify with `npm install` and `npm run dev` on Windows.

## V0.4.3 Physical State Authority Fix

The physical mission telemetry is emitted as multiple GroundRecords per simulation tick. The UI now treats the Mission Control state endpoint as the authoritative merged current-tick metric source and overlays the latest raw telemetry tick on top. This prevents a single metric record such as `phase_progress` from causing position/speed/altitude to appear missing.

Tick grouping also uses `mission_time_seconds` when `correlation_id` is absent, with record timestamp only as a last-resort key.

The Mission timeline now marks `Complete` as completed rather than showing it as in progress.
