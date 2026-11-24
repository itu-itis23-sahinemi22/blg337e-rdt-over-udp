#!/usr/bin/env bash
#
# end-to-end smoke test (issue #7): the real sender and receiver over loopback.
#
# the sender sends straight to the receiver's port, without the emulator. this works because
# the receiver answers to the address each datagram came from (docs/interfaces.md section 1).
# for each protocol it checks that a file arrives intact and that both programs exit with 0,
# and that a sender with no receiver gives up with 1. loss and the other impairments need the
# emulator; they come with run_one.sh (#10).
#
# run from the repository root after building: make smoke
# exit status 0 when every check passes, 1 otherwise.

set -u

SENDER=bin/sender
RECEIVER=bin/receiver
PROTOCOLS=(sw gbn sr)
PORT=9201                   # the receiver's port in the transfers
IDLE_PORTS=(9211 9212 9213) # nothing listens on these: the give-up checks
LINGER_MS=500               # a short -q, so the receiver exits soon after each transfer
LIMIT=30                    # seconds; a hung program fails its check instead of the whole job

failures=0
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

# check <1 or 0> <what>: prints the result of one check and counts it if it failed
check() {
    if [ "$1" -eq 1 ]; then
        printf '  ok    %s\n' "$2"
    else
        printf '  FAIL  %s\n' "$2"
        failures=$((failures + 1))
    fi
}

sha256() {
    sha256sum < "$1" | cut -d' ' -f1
}

# show_logs <name>: the end of both logs of a failed transfer, for the CI output
show_logs() {
    for log in "$dir/$1".*.log; do
        echo "  --- $log"
        tail -n 5 "$log" | sed 's/^/  /'
    done
}

# transfer <protocol> <input name>: one transfer; checks both exit statuses and the hash
transfer() {
    local proto=$1 name=$2
    local in="$dir/$name.in" out="$dir/$proto-$name.out"
    local before=$failures

    timeout "$LIMIT" "$RECEIVER" -p "$proto" -P "$PORT" -q "$LINGER_MS" -f "$out" \
        -L "$dir/$proto-$name.receiver.log" &
    local receiver_pid=$!
    # the receiver should bind first; if it is late, the first packet is lost and resent after
    # the initial RTO of 1 s, so the check still passes
    sleep 0.2
    timeout "$LIMIT" "$SENDER" -p "$proto" -P "$PORT" -f "$in" -L "$dir/$proto-$name.sender.log"
    local sender_status=$?
    wait "$receiver_pid"
    local receiver_status=$?

    check $((sender_status == 0)) "$proto, $name file: sender exits with 0 (got $sender_status)"
    check $((receiver_status == 0)) "$proto, $name file: receiver exits with 0 (got $receiver_status)"
    check "$([ "$(sha256 "$in")" = "$(sha256 "$out")" ] && echo 1 || echo 0)" \
        "$proto, $name file: SHA-256 of the received file matches"
    if [ "$failures" -gt "$before" ]; then
        show_logs "$proto-$name"
    fi
}

# the three files cover the termination cases of docs/interfaces.md 4.3
head -c $((256 * 1024 + 333)) /dev/urandom > "$dir/full.in" # last packet shorter than 1024 bytes
head -c $((4 * 1024)) /dev/urandom > "$dir/exact.in"        # a multiple of the payload: no extra empty packet
: > "$dir/empty.in"                                         # one packet with len 0 and LAST

echo "smoke: transfers"
for proto in "${PROTOCOLS[@]}"; do
    for name in full exact empty; do
        transfer "$proto" "$name"
    done
done

# with -x 1 and no receiver, each sender gives up after about 3 s: the initial RTO of 1 s, then
# one retransmission with the RTO doubled to 2 s. the three run at once to keep the test short.
echo "smoke: give up without a receiver"
pids=()
for i in "${!PROTOCOLS[@]}"; do
    proto=${PROTOCOLS[$i]}
    timeout "$LIMIT" "$SENDER" -p "$proto" -P "${IDLE_PORTS[$i]}" -x 1 -f "$dir/exact.in" \
        -L "$dir/$proto-giveup.sender.log" 2> /dev/null &
    pids+=($!)
done
for i in "${!PROTOCOLS[@]}"; do
    proto=${PROTOCOLS[$i]}
    wait "${pids[$i]}"
    status=$?
    check $((status == 1)) "$proto: sender with no receiver exits with 1 (got $status)"
    check "$(grep -q ',give_up,.*,retx=1$' "$dir/$proto-giveup.sender.log" && echo 1 || echo 0)" \
        "$proto: the give-up is logged after 1 retransmission"
done

if [ "$failures" -gt 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
echo "all checks passed"
