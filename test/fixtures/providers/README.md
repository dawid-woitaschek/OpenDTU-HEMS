# Native provider fixtures

Derived from official specifications and bounded responses supplied on
2026-10-19. Tests use local files and require no provider network access.

- `forecast-east.json` / `forecast-west.json`: public sparse `time=utc` responses,
  including irregular daylight boundaries and an overnight gap.
- `forecast-east-full.json`: exact existing `time=utc&full=1` query, 47 boundaries
  with continuous hourly zero-production night periods (46 intervals).

Forecast.Solar coordinates, place, distance, request identity and quota-zone
identity are synthetic. Raw curl/header captures are deliberately excluded.
The Swagger v7.41.0.2515 documents the public estimate endpoint and `time=utc`,
but does not document `full=1`. Its dense output is observed behavior, not a
guaranteed Swagger contract. Sparse gaps remain rejected atomically; tests never
silently fill missing periods with zero.

West was deliberately requested twice. The captured remaining counters
11 → 9 → 8 are fully explained by those requests and the later full East request.
