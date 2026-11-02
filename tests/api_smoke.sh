#!/usr/bin/env bash
# End-to-end API smoke test: starts owr_server, drives it over HTTP, checks the answers.
# Usage: tests/api_smoke.sh [path-to-owr_server]   (run from the repository root)
set -euo pipefail
BIN=${1:-build/owr_server}; PORT=${PORT:-8197}; URL=http://127.0.0.1:$PORT
"$BIN" --port "$PORT" --static dashboard/frontend/dist --scenarios scenarios >/dev/null 2>&1 &
PID=$!; trap 'kill $PID 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do curl -sf "$URL/api/health" >/dev/null 2>&1 && break; sleep 0.1; done
pass=0; fail=0
check() { if [ "$2" = "$3" ]; then pass=$((pass+1)); echo "  ok   $1"; else fail=$((fail+1)); echo "  FAIL $1 (got '$2', want '$3')"; fi; }
j() { python3 -c "import sys,json; d=json.load(sys.stdin); print($1)"; }
post() { curl -sf -X POST "$URL$1" -d "$2"; }

check "health"               "$(curl -s $URL/api/health | j "d['ok']")" "True"
check "dashboard served"     "$(curl -s -o /dev/null -w '%{http_code}' $URL/)" "200"
check "scenario list"        "$(curl -s $URL/api/scenarios | j "len(d['scenarios'])>=8")" "True"
post /api/scenario '{"name":"deadlock"}' >/dev/null
check "step 66 ticks"        "$(post /api/control '{"action":"step","steps":66}' | j "d['state']['tick']")" "66"
check "deadlock detected"    "$(curl -s $URL/api/state | j "d['state']['status']")" "CRITICAL"
check "recover endpoint"     "$(post /api/deadlock/recover '{}' | j "d['ok']")" "True"
check "process create"       "$(post /api/process '{"cpuBurst":5}' | j "d['pid']>0")" "True"
check "bad program rejected" "$(curl -s -X POST $URL/api/process -d '{"script":["BOGUS 1"]}' | j "'error' in d")" "True"
check "failure injection"    "$(post /api/failure '{"action":"io_flood","count":3}' | j "d['ok']")" "True"
check "replacement lab"      "$(post /api/replacement-lab '{"refs":[7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1],"frames":3}' | j "[r['faults'] for r in d['results']]")" "[15, 12, 14, 9]"
check "banker safe"          "$(post /api/banker '{"available":[3,3,2],"max":[[7,5,3],[3,2,2],[9,0,2],[2,2,2],[4,3,3]],"allocation":[[0,1,0],[2,0,0],[3,0,2],[2,1,1],[0,0,2]]}' | j "d['sequence']")" "[1, 3, 4, 0, 2]"
check "detective diagnose"   "$(post /api/detective/2/diagnose '{"answer":"DEADLOCK"}' | j "d['correct']")" "True"
check "experiment"           "$(post /api/experiment '{"workload":{"cpu":3,"io":3,"ui":1},"schedulers":["FCFS","RR"],"replacements":[]}' | j "len(d['schedulers'])")" "2"
check "reset"                "$(post /api/control '{"action":"reset"}' | j "d['state']['tick']")" "0"
echo "$pass passed, $fail failed"; [ "$fail" -eq 0 ]
