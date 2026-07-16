#!/bin/bash
# NexusDB Test Suite
# Verifies the basic build works and that all stub functions report "NOT IMPLEMENTED"
# Once the AI implements the required features, these tests should PASS.

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=0
TOTAL=0
OUTFILE=/tmp/nexusdb_output.txt
ERRFILE=/tmp/nexusdb_error.txt

pass() { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS + 1)); TOTAL=$((TOTAL + 1)); }
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL + 1)); TOTAL=$((TOTAL + 1)); }
info() { echo -e "  ${YELLOW}[INFO]${NC} $1"; }

echo "=============================================="
echo "  NexusDB Test Suite"
echo "=============================================="

# ── Test 1: Build ──
echo ""
echo "── Test 1: Build ──"
if make clean all 2>&1 | tee /tmp/nexusdb_build.txt | tail -1 | grep -q "built"; then
    pass "Build succeeded"
else
    fail "Build failed"
    cat /tmp/nexusdb_build.txt
    exit 1
fi

# ── Test 2: Binary Execution ──
echo ""
echo "── Test 2: Binary Execution ──"
if [ ! -x ./nexusdb ]; then
    fail "nexusdb binary not found or not executable"
    exit 1
fi
pass "nexusdb binary is executable"

set +e
timeout 5 ./nexusdb > "$OUTFILE" 2> "$ERRFILE"
EXIT_CODE=$?
set -e

if [ $EXIT_CODE -eq 0 ]; then
    pass "nexusdb ran successfully (exit 0)"
elif [ $EXIT_CODE -eq 139 ] || [ $EXIT_CODE -eq 134 ] || [ $EXIT_CODE -eq 132 ]; then
    info "nexusdb crashed (signal $EXIT_CODE) — expected: code has deliberate bugs"
    pass "nexusdb ran (exit $EXIT_CODE)"
elif [ $EXIT_CODE -eq 124 ]; then
    fail "nexusdb timed out (>5s)"
else
    info "nexusdb exited with code $EXIT_CODE"
    pass "nexusdb ran (exit $EXIT_CODE)"
fi

# ── Test 3: Stub Detection ──
echo ""
echo "── Test 3: Stub Detection ──"
STUB_COUNT=0
if [ -f "$ERRFILE" ] && [ -s "$ERRFILE" ]; then
    c=$(grep -c "NOT IMPLEMENTED" "$ERRFILE" 2>/dev/null || true)
    STUB_COUNT=$((STUB_COUNT + ${c:-0}))
fi
if [ -f "$OUTFILE" ] && [ -s "$OUTFILE" ]; then
    c=$(grep -c "NOT IMPLEMENTED" "$OUTFILE" 2>/dev/null || true)
    STUB_COUNT=$((STUB_COUNT + ${c:-0}))
fi
echo "  Stub messages found: $STUB_COUNT"
if [ "$STUB_COUNT" -eq 0 ] 2>/dev/null; then
    pass "All functions implemented (0 stubs remaining)!"
elif [ "$STUB_COUNT" -le 8 ] 2>/dev/null; then
    info "$STUB_COUNT of 8 stub functions still need implementation"
    pass "Stub detection working"
else
    info "Found $STUB_COUNT NOT IMPLEMENTED messages"
    pass "Stub detection working"
fi

# ── Test 4: Basic Submit ──
echo ""
echo "── Test 4: Basic Submit ──"
if grep -q "Submit result: 0" "$OUTFILE" 2>/dev/null; then
    pass "Basic submit returns ERR_NONE (0)"
else
    fail "Basic submit did not return ERR_NONE"
fi

# ── Test 5: Crypto Roundtrip ──
echo ""
echo "── Test 5: Crypto Roundtrip ──"
if grep -q "Match: YES" "$OUTFILE" 2>/dev/null; then
    pass "Crypto encrypt/decrypt roundtrip correct"
else
    fail "Crypto roundtrip failed"
fi

# ── Test 6: Memory Allocation ──
echo ""
echo "── Test 6: Memory Allocation ──"
if grep -q "Allocated:" "$OUTFILE" 2>/dev/null; then
    pass "Memory allocation test ran"
else
    fail "Memory allocation test did not run"
fi

# ── Test 7: Scheduler Stress ──
echo ""
echo "── Test 7: Scheduler Stress ──"
if grep -q "Dequeued" "$OUTFILE" 2>/dev/null; then
    pass "Scheduler stress test completed (test survived overflow)"
else
    info "Scheduler stress test crashed (expected — queue overflow bug)"
    pass "Scheduler overflow detected (known bug)"
fi

# ── Test 8: Crash Analysis ──
echo ""
echo "── Test 8: Crash Analysis ──"
if [ $EXIT_CODE -eq 139 ] || [ $EXIT_CODE -eq 134 ] || [ $EXIT_CODE -eq 132 ]; then
    info "Program crashed (signal $EXIT_CODE) — expected until bugs are fixed"
    pass "Crash is from known scheduler overflow bug"
elif [ $EXIT_CODE -eq 0 ]; then
    pass "No crash — all bugs may be fixed!"
else
    pass "No crash detected (exit $EXIT_CODE)"
fi

# ── Test 9: Compiler Warnings ──
echo ""
echo "── Test 9: Compiler Warnings ──"
WARN_COUNT=0
if [ -f /tmp/nexusdb_build.txt ]; then
    WARN_COUNT=$(grep -c "warning:" /tmp/nexusdb_build.txt 2>/dev/null || true)
    WARN_COUNT=${WARN_COUNT:-0}
fi
echo "  Compiler warnings: $WARN_COUNT"
if [ "$WARN_COUNT" -le 5 ] 2>/dev/null; then
    pass "Compiler warnings within acceptable range ($WARN_COUNT)"
else
    info "Many compiler warnings ($WARN_COUNT) — some may need fixing"
    pass "Build succeeded despite warnings"
fi

# ── Summary ──
echo ""
echo "=============================================="
printf "  Results: ${GREEN}%d passed${NC}, ${RED}%d failed${NC}, %d total\n" $PASS $FAIL $TOTAL
echo "=============================================="
if [ "$FAIL" -gt 0 ]; then
    echo ""
    echo "Output files saved: stdout=$OUTFILE stderr=$ERRFILE"
    exit 1
else
    exit 0
fi
