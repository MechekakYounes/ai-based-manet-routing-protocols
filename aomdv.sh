set -uo pipefail   


PROTO="AOMDV"
NNODES=${NNODES:-200}
SIMTIME=${SIMTIME:-200}
TXRANGE=${TXRANGE:-250}
MAXPATHS=${MAXPATHS:-3}
SEEDS=${SEEDS:-"1 2 3 4 5"}
TIMEOUT=${TIMEOUT:-600}

for arg in "$@"; do
    case "$arg" in
        --nNodes=*)   NNODES=${arg#*=} ;;
        --simTime=*)  SIMTIME=${arg#*=} ;;
        --txRange=*)  TXRANGE=${arg#*=} ;;
        --maxPaths=*) MAXPATHS=${arg#*=} ;;
        --seeds=*)    SEEDS=${arg#*=} ;;
        --timeout=*)  TIMEOUT=${arg#*=} ;;
        *) echo "Unknown arg: $arg"; exit 1 ;;
    esac
done

# out dir
RESULTS_DIR="results/aomdv_results_n${NNODES}_t${SIMTIME}_r${TXRANGE}_p${MAXPATHS}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/aomdv_results.csv"

# CSV header (one row per seed)
echo "protocol,seed,nNodes,simTime,txRange,maxPaths,pdr_pct,delay_ms,throughput_kbps,status" > "$CSV"

echo "Sweep config: protocol=$PROTO nNodes=$NNODES simTime=$SIMTIME"
echo "              txRange=$TXRANGE maxPaths=$MAXPATHS seeds=[$SEEDS]"
echo "Results CSV : $CSV"
echo "Raw logs    : $RESULTS_DIR/seed_*.log"
echo

# ---- Run each seed ----
for seed in $SEEDS; do
    echo "[$(date +%H:%M:%S)] Running seed=$seed ..."
    LOG="$RESULTS_DIR/seed_${seed}.log"

    timeout "$TIMEOUT" ./ns3 run --no-build \
        "scratch/aomdv.cc\
            --nNodes=$NNODES \
            --simTime=$SIMTIME \
            --txRange=$TXRANGE \
            --maxPaths=$MAXPATHS \
            --enableLog=false \
            --seed=$seed \
            --run=1" \
        > "$LOG" 2>&1
    EXIT=$?

    if [ $EXIT -ne 0 ]; then
        echo "  [WARN] seed=$seed exited $EXIT — logging NA (see $LOG)"
        echo "$PROTO,$seed,$NNODES,$SIMTIME,$TXRANGE,$MAXPATHS,NA,NA,NA,exit_$EXIT" >> "$CSV"
        continue
    fi

    # Parse the "Averages across N flows" block:
    PDR=$(awk   '/Averages across/{f=1;next} f&&/^PDR/{gsub(/.*: */,"");gsub(/ *%.*/,"");print;exit}'        "$LOG")
    DELAY=$(awk '/Averages across/{f=1;next} f&&/^Delay/{gsub(/.*: */,"");gsub(/ *ms.*/,"");print;exit}'     "$LOG")
    TPUT=$(awk  '/Averages across/{f=1;next} f&&/^Throughput/{gsub(/.*: */,"");gsub(/ *kbps.*/,"");print;exit}' "$LOG")

    # Fall back to NA if the block is missing or empty
    : "${PDR:=NA}"; : "${DELAY:=NA}"; : "${TPUT:=NA}"

    echo "$PROTO,$seed,$NNODES,$SIMTIME,$TXRANGE,$MAXPATHS,$PDR,$DELAY,$TPUT,ok" >> "$CSV"
    echo "  seed=$seed  PDR=${PDR}%  delay=${DELAY}ms  thr=${TPUT}kbps"
done


echo
echo "===== Summary ====="

awk -F, 'NR>1 && $10=="ok" {
    n++; pdr+=$7; dly+=$8; tpt+=$9;
    pdr_sq+=$7*$7; dly_sq+=$8*$8; tpt_sq+=$9*$9;
}
END {
    if (n == 0) { print "No successful runs."; exit }
    pdr_m = pdr/n; dly_m = dly/n; tpt_m = tpt/n;
    pdr_s = (n>1) ? sqrt((pdr_sq/n - pdr_m^2)) : 0;
    dly_s = (n>1) ? sqrt((dly_sq/n - dly_m^2)) : 0;
    tpt_s = (n>1) ? sqrt((tpt_sq/n - tpt_m^2)) : 0;
    printf "Successful runs : %d\n", n;
    printf "PDR (%%)        : mean=%.2f  std=%.2f\n", pdr_m, pdr_s;
    printf "Delay (ms)     : mean=%.2f  std=%.2f\n", dly_m, dly_s;
    printf "Throughput(kbps): mean=%.2f  std=%.2f\n", tpt_m, tpt_s;
}' "$CSV"

echo
echo "Per-seed CSV:"
if command -v column >/dev/null 2>&1; then
    column -t -s, "$CSV"
else
    cat "$CSV"
fi