#!/usr/bin/env bash
# run_all.sh — run all 5 protocols × 3 seeds with matched parameters.
# Usage: ./run_all.sh   (from ns-3 root)

set -e

RESULTS=results
SEEDS="1 2 3 4 5"
PROTOCOLS="AODV OLSR DSDV DSR AOMDV"

mkdir -p "$RESULTS"

for seed in $SEEDS; do
    for proto in $PROTOCOLS; do
        outdir="$RESULTS/${proto}_${seed}"
        mkdir -p "$outdir"

        # DSR cannot use FlowMonitor
        if [ "$proto" = "DSR" ]; then
            FM="false"
        else
            FM="true"
        fi

        echo "[$(date +%H:%M:%S)] Running $proto seed=$seed ..."
        ./ns3 run --no-build \
            "manet-routing-compare \
                --protocol=$proto \
                --seed=$seed \
                --flowMonitor=$FM \
                --traceMobility=false \
                --CSVfileName=$outdir/out.csv" \
            > "$outdir/stdout.log" 2>&1

        # Move the flowmon file produced by this run (name is hardcoded in C++)
        if [ "$FM" = "true" ] && [ -f manet-routing-compare.flowmon ]; then
            mv manet-routing-compare.flowmon "$outdir/manet-routing-compare.flowmon"
        fi
    done
done

echo
echo "======================================================="
echo "All runs complete. Analyzing..."
echo "======================================================="
python3 scratch/runs_analysis.py "$RESULTS" analysis_out/
echo
echo "Results:"
echo "  CSV   : analysis_out/summary.csv"
echo "  JSON  : analysis_out/summary.json"