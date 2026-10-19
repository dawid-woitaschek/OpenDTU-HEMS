# Native provider fixtures

Derived from official specifications and bounded responses supplied on
2026-10-19. Tests use local files and require no provider network access.

- `energy-charts-v2-price.json`: official `/v2/price` DE-LU envelope, 192
  quarter-hour records over today and tomorrow, including license metadata.
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

Official artifact SHA-256 values (raw specifications are not firmware assets):

- `energy-charts-openapi.json`: `523d3ec0fd82a7f148a94c02ffc08e8251c231d74e74b6019978af1e64b610d4`
- `energy-charts-llms-full.txt`: `a1c88a5cd2cdf0cd16df7cf7c77e88d19c0189db7b2fe3e414186ff686939fef`
- `forecast-solar-swagger.yaml`: `a4749942367c042e24e65a1a7e7fb75d65cb6becec9ba505e40d09a9d9fc59c2`
