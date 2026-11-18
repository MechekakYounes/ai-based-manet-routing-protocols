#!/usr/bin/env bash
# run_analysis.sh — unified sweep: AOMDV via scratch/aomdv.cc,
#                    AODV/OLSR/DSDV/DSR via manet-routing-compare.
# Usage:
#   ./run_analysis.sh
#   ./run_analysis.sh --seeds="1 2" --timeout=300
#   ./run_analysis.sh --protocols="AOMDV AODV" --seeds="1 2 3"
set -uo pipefail

# ---- Defaults ----
NNODES=${NNODES:-50}
SIMTIME=${SIMTIME:-200}
TXRANGE=${TXRANGE:-250}
MAXPATHS=${MAXPATHS:-3}
PROP=${PROP:-"Range"}
SEEDS=${SEEDS:-"1 2 3 4 5"}
PROTOCOLS=${PROTOCOLS:-"AOMDV AODV OLSR DSDV DSR"}
TIMEOUT=${TIMEOUT:-900}
RESULTS_DIR=${RESULTS_DIR:-results}
CSV="$RESULTS_DIR/unified_results.csv"

# ---- Parse --key=value ----
for arg in "$@"; do
    case "$arg" in
        --nNodes=*)    NNODES=${arg#*=} ;;
        --simTime=*)   SIMTIME=${arg#*=} ;;
        --txRange=*)   TXRANGE=${arg#*=} ;;
        --maxPaths=*)  MAXPATHS=${arg#*=} ;;
        --seeds=*)     SEEDS=${arg#*=} ;;
        --protocols=*) PROTOCOLS=${arg#*=} ;;
        --prop=*)      PROP=${arg#*=} ;;
        --timeout=*)   TIMEOUT=${arg#*=} ;;
        --results=*)   RESULTS_DIR=${arg#*=} ;;
        -h|--help)     sed -n '2,8p' "$0"; exit 0 ;;
        *) echo "Unknown arg: $arg"; exit 1 ;;
    esac
done

mkdir -p "$RESULTS_DIR"
echo "protocol,seed,nNodes,simTime,txRange,maxPaths,pdr_pct,delay_ms,throughput_kbps,status" > "$CSV"

# Print effective config BEFORE running anything
echo "Sweep config:"
echo "  protocols = [$PROTOCOLS]"
echo "  seeds     = [$SEEDS]"
echo "  nNodes    = $NNODES"
echo "  simTime   = $SIMTIME"
echo "  txRange   = $TXRANGE"
echo "  maxPaths  = $MAXPATHS"
echo "  timeout   = $TIMEOUT"
echo "  CSV       = $CSV"
echo

# Detect AOMDV seed support
AOMDV_HAS_SEED=0
grep -q 'cmd.AddValue.*"seed"' scratch/aomdv.cc 2>/dev/null && AOMDV_HAS_SEED=1
echo "AOMDV --seed support: $AOMDV_HAS_SEED (0 = all seeds will produce identical rows)"
echo

# ---- Runners ----
run_aomdv() {
    local seed=$1
    local outdir="$RESULTS_DIR/AOMDV_${seed}"
    mkdir -p "$outdir"
    local log="$outdir/stdout.log"
    local seed_args=""
    [ "$AOMDV_HAS_SEED" -eq 1 ] && seed_args="--seed=$seed --run=1"

    echo "  -> timeout ${TIMEOUT}s ./ns3 run scratch/aomdv.cc --nNodes=$NNODES --simTime=$SIMTIME --txRange=$TXRANGE --maxPaths=$MAXPATHS $seed_args"
    timeout "$TIMEOUT" ./ns3 run --no-build \
        "scratch/aomdv.cc --nNodes=$NNODES --simTime=$SIMTIME --txRange=$TXRANGE --maxPaths=$MAXPATHS --enableLog=false $seed_args" \
        > "$log" 2>&1
    local code=$?

    if [ $code -ne 0 ]; then
        echo "AOMDV,$seed,$NNODES,$SIMTIME,$TXRANGE,$MAXPATHS,NA,NA,NA,exit_$code" >> "$CSV"
        echo "  [WARN] AOMDV seed=$seed exited $code (see $log)"
        return
    fi

    local pdr delay tput
    pdr=$(awk   '/Averages across/{f=1;next} f&&/^PDR/{gsub(/.*: */,"");gsub(/ *%.*/,"");print;exit}'        "$log")
    delay=$(awk '/Averages across/{f=1;next} f&&/^Delay/{gsub(/.*: */,"");gsub(/ *ms.*/,"");print;exit}'     "$log")
    tput=$(awk  '/Averages across/{f=1;next} f&&/^Throughput/{gsub(/.*: */,"");gsub(/ *kbps.*/,"");print;exit}' "$log")
    : "${pdr:=NA}"; : "${delay:=NA}"; : "${tput:=NA}"
    echo "AOMDV,$seed,$NNODES,$SIMTIME,$TXRANGE,$MAXPATHS,$pdr,$delay,$tput,ok" >> "$CSV"
    echo "  PDR=${pdr}%  delay=${delay}ms  thr=${tput}kbps"
}

run_manet() {
    local proto=$1
    local seed=$2
    local outdir="$RESULTS_DIR/${proto}_${seed}"
    mkdir -p "$outdir"
    local fm=true
    [ "$proto" = "DSR" ] && fm=false

    echo "  -> timeout ${TIMEOUT}s ./ns3 run manet-routing-compare --protocol=$proto --seed=$seed --flowMonitor=$fm"
    timeout "$TIMEOUT" ./ns3 run --no-build \
        "manet-routing-compare --protocol=$proto --seed=$seed --flowMonitor=$fm --traceMobility=false --CSVfileName=$outdir/out.csv" \
        > "$outdir/stdout.log" 2>&1
    local code=$?
    [ "$fm" = "true" ] && [ -f manet-routing-compare.flowmon ] && mv manet-routing-compare.flowmon "$outdir/manet-routing-compare.flowmon"

    if [ $code -ne 0 ]; then
        echo "$proto,$seed,$NNODES,$SIMTIME,$TXRANGE,$MAXPATHS,NA,NA,NA,exit_$code" >> "$CSV"
        echo "  [WARN] $proto seed=$seed exited $code (see $outdir/stdout.log)"
        return
    fi

    local metrics
    metrics=$(python3 - "$outdir" <<'PYEOF'
import sys, os, xml.etree.ElementTree as ET, csv
d = sys.argv[1]
pdr = delay = tput = 'NA'
try:
    with open(os.path.join(d, 'out.csv')) as f:
        rows = list(csv.DictReader(f))
    rates = [float(r['ReceiveRate']) for r in rows if r.get('SimulationSecond') and float(r['SimulationSecond']) >= 101]
    if rates: tput = f'{sum(rates)/len(rates):.2f}'
except Exception: pass
fm = os.path.join(d, 'manet-routing-compare.flowmon')
if os.path.exists(fm):
    try:
        root = ET.parse(fm).getroot()
        tx = rx = 0; ds = 0.0
        for f in root.find('FlowStats'):
            tx += int(f.get('txPackets')); rx += int(f.get('rxPackets'))
            ds += float(f.get('delaySum','+0ns').strip('+').replace('ns',''))/1e9
        if tx > 0: pdr = f'{100.0*rx/tx:.2f}'
        if rx > 0: delay = f'{ds*1000.0/rx:.2f}'
    except Exception: pass
print(f'{pdr},{delay},{tput}')
PYEOF
    )
    echo "$proto,$seed,$NNODES,$SIMTIME,$TXRANGE,$MAXPATHS,${metrics},ok" >> "$CSV"
    echo "  PDR/delay/thr = $metrics"
}

# ---- Main loop ----
for seed in $SEEDS; do
    for proto in $PROTOCOLS; do
        echo "[$(date +%H:%M:%S)] $proto seed=$seed"
        case "$proto" in
            AOMDV) run_aomdv "$seed" ;;
            *)     run_manet "$proto" "$seed" ;;
        esac
    done
done

# ---- Summary ----
echo
echo "===== Summary (per protocol) ====="
awk -F, 'NR>1 && $10=="ok" {
    p=$1; runs[p]++;
    if($7!="NA"){np[p]++;sp[p]+=$7}
    if($8!="NA"){nd[p]++;sd[p]+=$8}
    if($9!="NA"){nt[p]++;st[p]+=$9}
}
END{
    printf "%-8s %5s %14s %14s %14s\n","proto","runs","PDR(%)","Delay(ms)","Thr(kbps)"
    n=split("AOMDV AODV OLSR DSDV DSR",o," ")
    for(i=1;i<=n;i++){p=o[i]; if(runs[p]==0)continue;
        printf "%-8s %5d %14.2f %14.2f %14.2f\n",p,runs[p],
            np[p]?sp[p]/np[p]:0, nd[p]?sd[p]/nd[p]:0, nt[p]?st[p]/nt[p]:0
    }
}' "$CSV"

echo
echo "Unified CSV: $CSV"