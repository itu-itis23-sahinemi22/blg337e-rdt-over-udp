#!/usr/bin/env bash
#
# runs every run of Experiments 1-5 (issue #17), then the analysis scripts (#18).
#
#   scripts/run_all.sh [--dry-run] [exp1 exp2 exp3 exp4 exp5 robust ...]
#
# with no experiment named it runs exp1 to exp5; the robustness runs of #16 only run when named.
# each run goes through scripts/run_one.sh (#10) into runs/<experiment>/<run id>/, and each
# experiment starts from an empty runs/<experiment>/. --dry-run prints the run_one.sh commands
# and runs nothing. the parameters, seeds and scenario files of every run are fixed here.
#
# run from the repository root. exit status 0 when every run ends as expected and the analysis
# succeeds, 1 otherwise, 2 for a usage or set-up error.

set -u

RUN_ONE=${RUN_ONE:-scripts/run_one.sh} # can be replaced, to test this script without the emulator
ANALYZE=analysis/analyze.py
TEST_FILE=data/test.bin

# the seeds of the runs with random impairments: digits of pi, e and the square roots of 2, 3, 5.
# experiment 2 uses all five, experiments 3 and 4 the first three.
SEEDS=(3141592653 2718281828 1414213562 1732050807 2236067977)
# runs without random impairments: the seed has no effect, but run_one.sh still needs one
FIXED_SEED=337

# experiment 5 only: 16 retransmissions of one packet happen at k = 4 (report section 9.3)
EXP5_OPTS=(-S "-x 20" -R "-q 45000")

usage() {
    echo "usage: scripts/run_all.sh [--dry-run] [exp1 exp2 exp3 exp4 exp5 robust ...]" >&2
    exit 2
}

dry_run=0
experiments=()
for arg in "$@"; do
    case $arg in
        --dry-run) dry_run=1 ;;
        exp1 | exp2 | exp3 | exp4 | exp5 | robust) experiments+=("$arg") ;;
        *) usage ;;
    esac
done
if [ ${#experiments[@]} -eq 0 ]; then
    experiments=(exp1 exp2 exp3 exp4 exp5)
fi

total=0
unexpected=() # run folders whose outcome differs from the expected one

# show_args <arg>...: the arguments as a shell would take them back, for --dry-run
show_args() {
    local a
    for a in "$@"; do
        case $a in
            *' '*) printf ' "%s"' "$a" ;;
            *) printf ' %s' "$a" ;;
        esac
    done
}

# field <key> <file>: the value of key=value in a result.txt
field() {
    sed -n "s/^$1=//p" "$2"
}

# outcome <run dir>: what the run produced, from its result.txt (#10):
# intact, corrupt (the hashes differ), or the reason it did not finish properly
outcome() {
    local result=$1/result.txt
    if [ ! -f "$result" ]; then
        echo "no result.txt"
    elif [ "$(field timeout "$result")" = yes ]; then
        echo "timeout"
    elif [ "$(field sender_status "$result")" != 0 ] || [ "$(field receiver_status "$result")" != 0 ] ||
        [ "$(field emulator_status "$result")" != 0 ]; then
        echo "exit status sender $(field sender_status "$result")," \
            "receiver $(field receiver_status "$result"), emulator $(field emulator_status "$result")"
    elif [ "$(field hash_match "$result")" = yes ]; then
        echo "intact"
    else
        echo "corrupt"
    fi
}

# run <experiment> <run id> <expected outcome> <run_one.sh options>...
run() {
    local exp=$1 id=$2 expect=$3
    shift 3
    local dir=runs/$exp/$id
    total=$((total + 1))
    if [ "$dry_run" -eq 1 ]; then
        printf '%s -o %s' "$RUN_ONE" "$dir"
        show_args "$@"
        echo
        return
    fi

    printf '[%3d] %-40s ' "$total" "$dir"
    local start=$SECONDS output got
    output=$(bash "$RUN_ONE" -o "$dir" "$@" 2>&1) # bash: works without the executable bit
    got=$(outcome "$dir")
    if [ "$got" = "$expect" ]; then
        printf '%-8s %4d s\n' "$got" $((SECONDS - start))
    else
        printf 'UNEXPECTED: %s, expected %s\n' "$got" "$expect"
        if [ -n "$output" ]; then
            printf '%s\n' "$output" | tail -n 10 | sed 's/^/      /'
        fi
        unexpected+=("$dir")
    fi
}

# Experiment 1: ideal network, SW and window sizes of GBN and SR
exp1() {
    local s=scenarios/baseline.conf w
    run exp1 sw intact -c "$s" -s "$FIXED_SEED" -p sw
    for w in 1 2 4 8 16 32; do
        run exp1 "gbn-w$w" intact -c "$s" -s "$FIXED_SEED" -p gbn -w "$w"
        run exp1 "sr-w$w" intact -c "$s" -s "$FIXED_SEED" -p sr -w "$w"
    done
}

# Experiment 2: DATA loss 0, 5, 10 %. 0 % has nothing random, so it runs once.
exp2() {
    local proto loss seed
    for proto in sw gbn sr; do
        run exp2 "$proto-loss0" intact -c scenarios/baseline.conf -s "$FIXED_SEED" -p "$proto"
        for loss in 5 10; do
            for seed in "${SEEDS[@]}"; do
                run exp2 "$proto-loss$loss-s$seed" intact -c "scenarios/exp2-loss$loss.conf" \
                    -s "$seed" -p "$proto"
            done
        done
    done
}

# Experiment 3: propagation delay with jitter 10 % of it; SW, and SR with W = 16 and 64
exp3() {
    local delay seed s
    for delay in 20 50 100 200; do
        s=scenarios/exp3-delay$delay.conf
        for seed in "${SEEDS[@]:0:3}"; do
            run exp3 "sw-d$delay-s$seed" intact -c "$s" -s "$seed" -p sw
            run exp3 "sr-w16-d$delay-s$seed" intact -c "$s" -s "$seed" -p sr -w 16
            run exp3 "sr-w64-d$delay-s$seed" intact -c "$s" -s "$seed" -p sr -w 64
        done
    done
}

# Experiment 4: each impairment at 5 % with all three protocols
exp4() {
    local imp proto seed
    for imp in corruption reordering duplication ackloss; do
        for proto in sw gbn sr; do
            for seed in "${SEEDS[@]:0:3}"; do
                run exp4 "$proto-$imp-s$seed" intact -c "scenarios/exp4-$imp.conf" -s "$seed" -p "$proto"
            done
        done
    done
}

# exp5_window <protocol> <k> <W> <safe limit> <scenario>: one window of Experiment 5. Up to the
# limit the file must arrive intact; above it the file must be corrupted, and the run is repeated
# three times to show that the failure is the same every time.
exp5_window() {
    local proto=$1 k=$2 w=$3 limit=$4 s=$5 rep
    if [ "$w" -le "$limit" ]; then
        run exp5 "$proto-k$k-w$w" intact -c "$s" -s "$FIXED_SEED" -p "$proto" -w "$w" -k "$k" \
            "${EXP5_OPTS[@]}"
    else
        for rep in 1 2 3; do
            run exp5 "$proto-k$k-w$w-rep$rep" corrupt -c "$s" -s "$FIXED_SEED" -p "$proto" -w "$w" \
                -k "$k" "${EXP5_OPTS[@]}"
        done
    fi
}

# Experiment 5: sequence-number limits for k = 3 and 4, every W up to the limit and one above
exp5() {
    local k w limit
    for k in 3 4; do
        limit=$(((1 << k) - 1)) # Go-Back-N: 2^k - 1
        for w in $(seq 1 $((limit + 1))); do
            exp5_window gbn "$k" "$w" "$limit" "scenarios/exp5-gbn-k$k.conf"
        done
        limit=$((1 << (k - 1))) # Selective Repeat: 2^(k-1)
        for w in $(seq 1 $((limit + 1))); do
            exp5_window sr "$k" "$w" "$limit" scenarios/exp5-sr.conf
        done
    done
}

# Robustness (#16): every impairment, and all of them at once, with all three protocols
robust() {
    local imp proto seed
    for imp in dataloss ackloss corruption reordering duplication all; do
        for proto in sw gbn sr; do
            for seed in "${SEEDS[@]:0:3}"; do
                run robust "$proto-$imp-s$seed" intact -c "scenarios/robust-$imp.conf" -s "$seed" -p "$proto"
            done
        done
    done
}

if [ "$dry_run" -eq 0 ]; then
    make -s || exit 2
    for f in "$RUN_ONE" "$TEST_FILE"; do
        if [ ! -f "$f" ]; then
            echo "run_all: $f is missing" >&2
            exit 2
        fi
    done
fi

analysis_failed=0
for exp in "${experiments[@]}"; do
    echo "== $exp"
    if [ "$dry_run" -eq 0 ]; then
        rm -rf "runs/$exp"
        mkdir -p "runs/$exp"
    fi
    "$exp"
done

# the analysis covers the experiments; the robustness runs only need their hashes
if [ "$dry_run" -eq 0 ]; then
    for exp in "${experiments[@]}"; do
        [ "$exp" = robust ] && continue
        if [ ! -f "$ANALYZE" ]; then
            echo "run_all: $ANALYZE is missing, no metrics or plots"
            analysis_failed=1
            break
        fi
        echo "== analysis of $exp"
        python3 "$ANALYZE" "runs/$exp" || analysis_failed=1
    done
fi

echo "$total runs"
if [ "$dry_run" -eq 1 ]; then
    exit 0
fi
if [ ${#unexpected[@]} -gt 0 ]; then
    echo "${#unexpected[@]} run(s) with an unexpected outcome:"
    printf '  %s\n' "${unexpected[@]}"
fi
if [ ${#unexpected[@]} -gt 0 ] || [ "$analysis_failed" -eq 1 ]; then
    exit 1
fi
echo "every run ended as expected"
