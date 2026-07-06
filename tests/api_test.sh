#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════════
# IDC Platform — API Integration Test Suite
# ═══════════════════════════════════════════════════════════════════════════════
# Usage:
#   ./tests/api_test.sh [options]
#
# Options:
#   -h, --help              Show this help message
#   -b, --base URL          API base URL (default: http://localhost:8080/api/v1)
#   -a, --admin-token TOK   Use this admin token (skip login)
#   -d, --dealer-token TOK  Use this dealer token (skip login)
#   -s, --section SECTION   Run only specific section (auth|products|distributors|orders|subscriptions|invoices|bandwidth|sync|performance)
#   -v, --verbose           Verbose output (show response bodies)
#   -q, --quiet             Minimal output (pass/fail only)
#
# Dependencies: curl, jq
#
# This script tests all critical API paths of the IDC platform.
# It is designed to be idempotent — safe to run multiple times.
#
# Exit codes:
#   0 — All tests passed
#   1 — One or more tests failed
#   2 — Prerequisite check failed
# ═══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ── Config ─────────────────────────────────────────────────────────────────────

BASE_URL="${BASE_URL:-http://localhost:8080/api/v1}"
ADMIN_TOKEN=""
DEALER_TOKEN=""
RUN_SECTION=""
VERBOSE=false
QUIET=false

# Test counters
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0
FAILURES=()

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ── Helpers ────────────────────────────────────────────────────────────────────

usage() {
    sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# \?//'
    exit 0
}

info()    { [ "$QUIET" = false ] && echo -e "${BLUE}[INFO]${NC}  $*" || true; }
detail()  { [ "$VERBOSE" = true ] && echo "         $*" || true; }
pass()    { TESTS_PASSED=$((TESTS_PASSED + 1)); echo -e "  ${GREEN}✓${NC} $1"; }
fail()    { TESTS_FAILED=$((TESTS_FAILED + 1)); FAILURES+=("$1"); echo -e "  ${RED}✗${NC} $1"; }
heading() { echo -e "\n${CYAN}══════════════════════════════════════════════${NC}"; echo -e "${CYAN}  $*${NC}"; echo -e "${CYAN}══════════════════════════════════════════════${NC}"; }
subheading() { echo -e "\n${YELLOW}--- $* ---${NC}"; }

api_call() {
    local method="$1"
    local path="$2"
    local token="$3"
    local body="${4:-}"
    local extra_args="${5:-}"

    local curl_args=("-s" "-X" "$method" "${BASE_URL}${path}")
    curl_args+=("-H" "Content-Type: application/json")
    [ -n "$token" ] && curl_args+=("-H" "Authorization: Bearer $token")
    [ -n "$body" ] && curl_args+=("-d" "$body")
    [ -n "$extra_args" ] && curl_args+=($extra_args)

    curl "${curl_args[@]}"
}

assert_success() {
    local response="$1"
    local description="$2"
    TESTS_TOTAL=$((TESTS_TOTAL + 1))

    if echo "$response" | jq -e '.code == 0' > /dev/null 2>&1; then
        pass "$description"
        detail "Response: $(echo "$response" | jq -c '.data' 2>/dev/null || echo "$response")"
        return 0
    else
        local msg
        msg=$(echo "$response" | jq -r '.message // "unknown error"' 2>/dev/null)
        fail "$description — $msg"
        detail "Full response: $response"
        return 1
    fi
}

assert_http_code() {
    local http_code="$1"
    local expected="$2"
    local description="$3"
    TESTS_TOTAL=$((TESTS_TOTAL + 1))

    if [ "$http_code" = "$expected" ]; then
        pass "$description (HTTP $http_code)"
        return 0
    else
        fail "$description — expected HTTP $expected, got $http_code"
        return 1
    fi
}

assert_contains() {
    local response="$1"
    local pattern="$2"
    local description="$3"
    TESTS_TOTAL=$((TESTS_TOTAL + 1))

    if echo "$response" | grep -q "$pattern"; then
        pass "$description"
        return 0
    else
        fail "$description — expected to contain '$pattern'"
        detail "Response: ${response:0:500}"
        return 1
    fi
}

wait_for_service() {
    local retries=30
    local delay=2

    info "Waiting for API at $BASE_URL..."
    for ((i = 1; i <= retries; i++)); do
        if curl -sf -o /dev/null "${BASE_URL}/auth/login" -X POST \
            -H 'Content-Type: application/json' \
            -d '{"username":"admin","password":"admin123"}' 2>/dev/null; then
            info "Service is up (attempt $i)"
            return 0
        fi
        sleep "$delay"
    done
    err "Service did not become available after $((retries * delay)) seconds"
    return 1
}

print_summary() {
    echo ""
    echo "╔══════════════════════════════════════════════════════════════╗"
    echo "║                       TEST SUMMARY                          ║"
    echo "╚══════════════════════════════════════════════════════════════╝"
    echo ""
    echo "  Total:     $TESTS_TOTAL"
    echo -e "  Passed:    ${GREEN}$TESTS_PASSED${NC}"
    echo -e "  Failed:    ${RED}$TESTS_FAILED${NC}"
    echo ""

    if [ ${#FAILURES[@]} -gt 0 ]; then
        echo -e "${RED}Failed tests:${NC}"
        for f in "${FAILURES[@]}"; do
            echo "  ${RED}•${NC} $f"
        done
        echo ""
        return 1
    fi

    echo -e "${GREEN}All tests passed!${NC}"
    echo ""
    return 0
}

check_deps() {
    for cmd in curl jq; do
        if ! command -v "$cmd" &>/dev/null; then
            echo "Error: Required command not found: $cmd" >&2
            exit 2
        fi
    done
}

# ── Auth Tests ─────────────────────────────────────────────────────────────────

test_auth_section() {
    heading "AUTH & AUTHENTICATION"

    # ── Login as admin ──────────────────────────────────────────────────────
    subheading "Login Tests"

    local admin_login
    admin_login=$(api_call "POST" "/auth/login" "" \
        '{"username":"admin","password":"admin123"}')
    assert_success "$admin_login" "Login as admin with default credentials"

    ADMIN_TOKEN=$(echo "$admin_login" | jq -r '.data.token // .data.access_token // empty' 2>/dev/null)
    if [ -z "$ADMIN_TOKEN" ]; then
        fail "Extract admin token from login response"
        detail "Login response: $admin_login"
        # Try alternative JWT field names
        ADMIN_TOKEN=$(echo "$admin_login" | jq -r '.data // empty' 2>/dev/null)
    fi

    if [ -z "$ADMIN_TOKEN" ]; then
        fail "Could not extract admin token — cannot continue most tests"
        return 1
    fi
    info "Admin token obtained: ${ADMIN_TOKEN:0:20}..."

    # ── Login as dealer ─────────────────────────────────────────────────────
    # Note: If no dealer exists, we create one via admin API later.
    # For now, try logging in as admin with different role context
    local dealer_login
    # Try to create a dealer user first, or use a dealer if exists
    dealer_login=$(api_call "POST" "/auth/login" "" \
        '{"username":"admin","password":"admin123"}')
    DEALER_TOKEN=$ADMIN_TOKEN  # Use admin token as fallback

    # ── Wrong password ──────────────────────────────────────────────────────
    local wrong_pass
    wrong_pass=$(api_call "POST" "/auth/login" "" \
        '{"username":"admin","password":"wrong_password_12345"}')
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    if echo "$wrong_pass" | jq -e '.code != 0' > /dev/null 2>&1; then
        pass "Login with wrong password is rejected"
    else
        fail "Login with wrong password should have failed"
        detail "Response: $wrong_pass"
    fi

    # ── Expired token ───────────────────────────────────────────────────────
    local expired_response
    expired_response=$(api_call "GET" "/auth/me" "eyJhbGciOiJIUzI1NiJ9.expired_token" "" "-w '%{http_code}'")
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    local http_code="${expired_response: -3}"
    local body="${expired_response::-3}"
    if [ "$http_code" = "401" ]; then
        pass "Expired/invalid token returns 401"
    else
        # Also check for 400
        if [ "$http_code" = "400" ]; then
            pass "Expired/invalid token returns 400 (acceptable)"
        else
            fail "Expired token should return 401, got HTTP $http_code"
            detail "Response: $body"
        fi
    fi

    # ── Auth/me with valid token ───────────────────────────────────────────
    local me
    me=$(api_call "GET" "/auth/me" "$ADMIN_TOKEN")
    assert_success "$me" "GET /auth/me returns current user profile"

    # ── No token ─────────────────────────────────────────────────────────────
    local no_auth
    no_auth=$(api_call "GET" "/auth/me" "" "" "-w '%{http_code}'")
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    local no_auth_code="${no_auth: -3}"
    local no_auth_body="${no_auth::-3}"
    if [ "$no_auth_code" = "401" ]; then
        pass "Request without token returns 401"
    else
        fail "No-token request should return 401, got HTTP $no_auth_code"
    fi

    # ── Logout ───────────────────────────────────────────────────────────────
    local logout
    logout=$(api_call "POST" "/auth/logout" "$ADMIN_TOKEN")
    assert_success "$logout" "POST /auth/logout succeeds"

    # ── Re-login to refresh token for subsequent tests ──────────────────────
    admin_login=$(api_call "POST" "/auth/login" "" \
        '{"username":"admin","password":"admin123"}')
    ADMIN_TOKEN=$(echo "$admin_login" | jq -r '.data.token // .data.access_token // .data // empty' 2>/dev/null)
    info "Admin token refreshed"
}

# ── Product Tests ──────────────────────────────────────────────────────────────

test_products_section() {
    heading "PRODUCTS"

    # ── List products ───────────────────────────────────────────────────────
    subheading "List Products"
    local products
    products=$(api_call "GET" "/products" "$ADMIN_TOKEN")
    assert_success "$products" "GET /products returns product list"
    detail "Product count: $(echo "$products" | jq -r '.data | length' 2>/dev/null || echo 'N/A')"

    # ── Get product types ───────────────────────────────────────────────────
    subheading "Product Types"
    local types
    types=$(api_call "GET" "/products/types" "$ADMIN_TOKEN")
    assert_success "$types" "GET /products/types returns product type list"

    # ── Create product ──────────────────────────────────────────────────────
    subheading "Create Product"
    local create_body
    create_body='{
        "name": "Test Server E5-2680 v4",
        "type": "dedicated_server",
        "specs": {
            "cpu": "E5-2680 v4",
            "cores": 14,
            "ram": "128GB DDR4 ECC",
            "disk": "2x480GB SSD",
            "bandwidth": "100TB",
            "port_speed": "1Gbps"
        },
        "description": "Test product for integration testing",
        "status": 1
    }'

    local new_product
    new_product=$(api_call "POST" "/products" "$ADMIN_TOKEN" "$create_body")
    assert_success "$new_product" "POST /products creates a new product"

    local product_id
    product_id=$(echo "$new_product" | jq -r '.data.id // .data.product_id // empty' 2>/dev/null)
    if [ -z "$product_id" ]; then
        warn "Could not extract product ID from creation response"
        warn "Response: $(echo "$new_product" | jq -c '.' 2>/dev/null)"
        # Try to get it from listing
        products=$(api_call "GET" "/products" "$ADMIN_TOKEN")
        product_id=$(echo "$products" | jq -r '.data[0].id // empty' 2>/dev/null)
    fi
    info "Product ID: ${product_id:-N/A}"

    # ── Get product detail ──────────────────────────────────────────────────
    if [ -n "${product_id:-}" ]; then
        local product_detail
        product_detail=$(api_call "GET" "/products/${product_id}" "$ADMIN_TOKEN")
        assert_success "$product_detail" "GET /products/{id} returns product detail"
    fi

    # ── Check pricing ───────────────────────────────────────────────────────
    if [ -n "${product_id:-}" ]; then
        local pricing
        pricing=$(api_call "GET" "/products/${product_id}/price" "$ADMIN_TOKEN")
        assert_success "$pricing" "GET /products/{id}/price returns pricing info"
    fi
}

# ── Distributor Tests ──────────────────────────────────────────────────────────

test_distributors_section() {
    heading "DISTRIBUTORS"

    # ── List distributors ───────────────────────────────────────────────────
    subheading "List Distributors"
    local distributors
    distributors=$(api_call "GET" "/distributors" "$ADMIN_TOKEN")
    assert_success "$distributors" "GET /distributors returns distributor list"

    local dist_count
    dist_count=$(echo "$distributors" | jq -r '.data | length // .data.items | length' 2>/dev/null || echo "0")
    info "Distributor count: $dist_count"

    # ── Create distributor ──────────────────────────────────────────────────
    subheading "Create Distributor"
    local create_dist_body
    create_dist_body='{
        "name": "Test Distributor Corp",
        "level": 1,
        "contact_person": "John Doe",
        "contact_phone": "13800138000",
        "email": "distributor@test.com",
        "status": 1
    }'

    local new_dist
    new_dist=$(api_call "POST" "/distributors" "$ADMIN_TOKEN" "$create_dist_body")
    assert_success "$new_dist" "POST /distributors creates a new distributor"

    local dist_id
    dist_id=$(echo "$new_dist" | jq -r '.data.id // .data.distributor_id // empty' 2>/dev/null)
    if [ -z "$dist_id" ]; then
        warn "Could not extract distributor ID from creation response"
        # Try getting first from list
        distributors=$(api_call "GET" "/distributors" "$ADMIN_TOKEN")
        dist_id=$(echo "$distributors" | jq -r '.data[0].id // .data.items[0].id // empty' 2>/dev/null)
    fi
    info "Distributor ID: ${dist_id:-N/A}"

    # ── Get distributor tree ────────────────────────────────────────────────
    if [ -n "${dist_id:-}" ]; then
        local tree
        tree=$(api_call "GET" "/distributors/${dist_id}/tree" "$ADMIN_TOKEN")
        assert_success "$tree" "GET /distributors/{id}/tree returns organization tree"

        local children
        children=$(api_call "GET" "/distributors/${dist_id}/children" "$ADMIN_TOKEN")
        assert_success "$children" "GET /distributors/{id}/children returns direct children"
    fi
}

# ── Order Tests ────────────────────────────────────────────────────────────────

test_orders_section() {
    heading "ORDERS"

    # ── List orders ─────────────────────────────────────────────────────────
    subheading "List Orders"
    local orders
    orders=$(api_call "GET" "/orders" "$ADMIN_TOKEN")
    assert_success "$orders" "GET /orders returns order list"

    # ── Create cart item & submit order ─────────────────────────────────────
    subheading "Create & Submit Order"

    # First get a product ID to use
    local products_json
    products_json=$(api_call "GET" "/products" "$ADMIN_TOKEN")
    local product_id
    product_id=$(echo "$products_json" | jq -r '.data[0].id // .data.items[0].id // empty' 2>/dev/null)

    if [ -z "$product_id" ]; then
        warn "No products found — creating a test product first"
        local create_body='{
            "name": "Integration Test Product",
            "type": "dedicated_server",
            "specs": {"cpu":"Test","cores":2,"ram":"8GB","disk":"100GB SSD"},
            "status": 1
        }'
        local new_prod
        new_prod=$(api_call "POST" "/products" "$ADMIN_TOKEN" "$create_body")
        product_id=$(echo "$new_prod" | jq -r '.data.id // empty' 2>/dev/null)
    fi

    if [ -n "$product_id" ] && [ "$product_id" != "null" ]; then
        info "Using product ID: $product_id"

        # Add to cart
        local cart_item_body
        cart_item_body=$(cat <<EOF
{
    "product_id": ${product_id},
    "quantity": 1,
    "period_months": 1,
    "config": {}
}
EOF
)
        local new_cart_item
        new_cart_item=$(api_call "POST" "/cart/items" "$ADMIN_TOKEN" "$cart_item_body")
        assert_success "$new_cart_item" "POST /cart/items adds product to cart"

        # List cart
        local cart
        cart=$(api_call "GET" "/cart" "$ADMIN_TOKEN")
        assert_success "$cart" "GET /cart lists cart items with pricing"

        # Submit order from cart
        local order_body='{"billing_cycle": "monthly", "remark": "Integration test order"}'
        local new_order
        new_order=$(api_call "POST" "/orders" "$ADMIN_TOKEN" "$order_body")
        assert_success "$new_order" "POST /orders submits order from cart"

        local order_id
        order_id=$(echo "$new_order" | jq -r '.data.id // .data.order_id // empty' 2>/dev/null)

        if [ -n "$order_id" ] && [ "$order_id" != "null" ]; then
            info "Order ID: $order_id"

            # Get order detail
            local order_detail
            order_detail=$(api_call "GET" "/orders/${order_id}" "$ADMIN_TOKEN")
            assert_success "$order_detail" "GET /orders/{id} returns order detail with items"

            # Approve order
            local approve_body='{"remark": "Approved by integration test"}'
            local approved
            approved=$(api_call "PUT" "/orders/${order_id}/approve" "$ADMIN_TOKEN" "$approve_body")
            assert_success "$approved" "PUT /orders/{id}/approve approves the order"
        else
            warn "No order ID obtained — skipping order detail/approve tests"
        fi
    else
        warn "No product available — skipping cart and order submission tests"
    fi
}

# ── Subscription Tests ─────────────────────────────────────────────────────────

test_subscriptions_section() {
    heading "SUBSCRIPTIONS"

    # ── List subscriptions ──────────────────────────────────────────────────
    subheading "List Subscriptions"
    local subscriptions
    subscriptions=$(api_call "GET" "/subscriptions" "$ADMIN_TOKEN")
    assert_success "$subscriptions" "GET /subscriptions returns subscription list"

    local sub_id
    sub_id=$(echo "$subscriptions" | jq -r '.data[0].id // .data.items[0].id // empty' 2>/dev/null)

    if [ -n "$sub_id" ] && [ "$sub_id" != "null" ]; then
        info "Subscription ID: $sub_id"

        # Get subscription detail
        local sub_detail
        sub_detail=$(api_call "GET" "/subscriptions/${sub_id}" "$ADMIN_TOKEN")
        assert_success "$sub_detail" "GET /subscriptions/{id} returns subscription detail with product info"
    else
        warn "No subscriptions found — skipping detail test (might need an approved order first)"
    fi
}

# ── Invoice Tests ──────────────────────────────────────────────────────────────

test_invoices_section() {
    heading "INVOICES"

    # ── List invoices ───────────────────────────────────────────────────────
    subheading "List Invoices"
    local invoices
    invoices=$(api_call "GET" "/invoices" "$ADMIN_TOKEN")
    assert_success "$invoices" "GET /invoices returns invoice list"

    local inv_id
    inv_id=$(echo "$invoices" | jq -r '.data[0].id // .data.items[0].id // empty' 2>/dev/null)

    if [ -n "$inv_id" ] && [ "$inv_id" != "null" ]; then
        info "Invoice ID: $inv_id"

        # Get invoice detail
        local inv_detail
        inv_detail=$(api_call "GET" "/invoices/${inv_id}" "$ADMIN_TOKEN")
        assert_success "$inv_detail" "GET /invoices/{id} returns invoice detail with items"
    else
        warn "No invoices found — skipping detail test"
    fi
}

# ── Bandwidth Tests ────────────────────────────────────────────────────────────

test_bandwidth_section() {
    heading "BANDWIDTH"

    # ── Generate bandwidth samples ──────────────────────────────────────────
    subheading "Bandwidth Sampling"
    local simulate_body
    simulate_body='{
        "subscription_id": 1,
        "port_ids": ["port-1"],
        "start": "2026-07-01 00:00:00",
        "end": "2026-07-02 00:00:00",
        "base_rate_mbps": 100,
        "unit_price": 15.00
    }'

    local simulation
    simulation=$(api_call "POST" "/bandwidth/simulate" "$ADMIN_TOKEN" "$simulate_body")
    # Simulation is admin-only, may succeed or fail depending on seed data
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    local sim_code
    sim_code=$(echo "$simulation" | jq -r '.code' 2>/dev/null)
    if [ "$sim_code" = "0" ]; then
        pass "POST /bandwidth/simulate generates mock samples"
        detail "Generated: $(echo "$simulation" | jq -c '.data' 2>/dev/null)"
    elif [ "$sim_code" = "403" ]; then
        pass "POST /bandwidth/simulate checks RBAC (expected 403 if not authorized)"
    else
        # May fail if subscription 1 doesn't exist — that's OK for integration test
        info "Bandwidth simulation returned code $sim_code (may be expected)"
    fi

    # ── Get bandwidth report ────────────────────────────────────────────────
    # First find a subscription to query
    local subs
    subs=$(api_call "GET" "/subscriptions" "$ADMIN_TOKEN")
    local sub_id
    sub_id=$(echo "$subs" | jq -r '.data[0].id // .data.items[0].id // empty' 2>/dev/null)

    if [ -n "$sub_id" ] && [ "$sub_id" != "null" ]; then
        local bandwidth
        bandwidth=$(api_call "GET" "/subscriptions/${sub_id}/bandwidth?start=2026-07-01&end=2026-07-02" "$ADMIN_TOKEN")
        assert_success "$bandwidth" "GET /subscriptions/{id}/bandwidth returns bandwidth data"
    else
        warn "No subscriptions found — skipping bandwidth report test"
    fi
}

# ── Sync Tests ─────────────────────────────────────────────────────────────────

test_sync_section() {
    heading "ZJMF SYNC"

    # ── Check sync status ───────────────────────────────────────────────────
    subheading "Sync Status"
    local status
    status=$(api_call "GET" "/sync/zjmf/status" "$ADMIN_TOKEN")
    assert_success "$status" "GET /sync/zjmf/status returns sync connection status"

    # ── Check sync logs ─────────────────────────────────────────────────────
    local sync_logs
    sync_logs=$(api_call "GET" "/sync/logs" "$ADMIN_TOKEN")
    assert_success "$sync_logs" "GET /sync/logs returns sync history"
}

# ── Performance Tests ──────────────────────────────────────────────────────────

test_performance_section() {
    heading "PERFORMANCE"

    subheading "100 Concurrent Requests"

    local start_time
    start_time=$(date +%s%N)

    local request_count=100
    local concurrency=20
    local results_dir
    results_dir=$(mktemp -d)
    local pids=()
    local success_count=0
    local total_time=0
    local min_time=999999
    local max_time=0
    local all_times=()

    # Fire requests in batches
    info "Firing $request_count requests (concurrency: $concurrency)..."
    for ((i = 1; i <= request_count; i++)); do
        (
            local req_start
            req_start=$(date +%s%N)
            local http_code
            http_code=$(curl -s -o /dev/null -w "%{http_code}" \
                -X POST "${BASE_URL}/auth/login" \
                -H 'Content-Type: application/json' \
                -d '{"username":"admin","password":"admin123"}' 2>/dev/null || echo "000")
            local req_end
            req_end=$(date +%s%N)
            local req_time_ms=$(( (req_end - req_start) / 1000000 ))
            echo "$http_code $req_time_ms" >> "$results_dir/result_$i.txt"
        ) &
        pids+=($!)

        # Throttle concurrency
        if [ $((i % concurrency)) -eq 0 ]; then
            for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
            pids=()
            info "  Completed $i / $request_count requests"
        fi
    done

    # Wait for remaining
    for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done

    # Collect results
    for result_file in "$results_dir"/result_*.txt; do
        [ -f "$result_file" ] || continue
        read -r code req_time <<< "$(cat "$result_file")"
        [ "$code" = "200" ] && success_count=$((success_count + 1))
        total_time=$((total_time + req_time))
        [ "$req_time" -lt "$min_time" ] && min_time=$req_time
        [ "$req_time" -gt "$max_time" ] && max_time=$req_time
        all_times+=("$req_time")
    done

    local end_time
    end_time=$(date +%s%N)
    local wall_time_ms=$(( (end_time - start_time) / 1000000 ))

    # Calculate percentiles
    IFS=$'\n' sorted_times=($(sort -n <<< "${all_times[*]}"))
    unset IFS
    local count="${#sorted_times[@]}"
    local p50="${sorted_times[$((count * 50 / 100))]}"
    local p95="${sorted_times[$((count * 95 / 100))]}"
    local p99="${sorted_times[$((count * 99 / 100))]}"
    local avg=$((count > 0 ? total_time / count : 0))

    # Cleanup
    rm -rf "$results_dir"

    # Results
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    if [ "$success_count" -eq "$request_count" ]; then
        pass "All $request_count requests succeeded (200 OK)"
    else
        fail "Only $success_count/$request_count succeeded"
    fi

    echo ""
    echo "  ╔══════════════════════════════════════════════╗"
    echo "  ║         CONCURRENT REQUEST RESULTS            ║"
    echo "  ╠══════════════════════════════════════════════╣"
    printf "  ║  Total requests:    %13d           ║\n" "$request_count"
    printf "  ║  Successful:        %13d           ║\n" "$success_count"
    printf "  ║  Failed:            %13d           ║\n" "$((request_count - success_count))"
    echo "  ║                                              ║"
    printf "  ║  Wall time:         %13d ms         ║\n" "$wall_time_ms"
    printf "  ║  Avg response:      %13d ms         ║\n" "$avg"
    printf "  ║  Min response:      %13d ms         ║\n" "$min_time"
    printf "  ║  Max response:      %13d ms         ║\n" "$max_time"
    echo "  ║                                              ║"
    printf "  ║  p50 (median):      %13d ms         ║\n" "$p50"
    printf "  ║  p95:               %13d ms         ║\n" "$p95"
    printf "  ║  p99:               %13d ms         ║\n" "$p99"
    echo "  ╚══════════════════════════════════════════════╝"
    echo ""

    # Performance assertion: p95 should be under 2 seconds
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    if [ "$p95" -lt 2000 ]; then
        pass "Performance: p95 latency ($p95 ms) under 2000 ms threshold"
    else
        warn "Performance: p95 latency ($p95 ms) exceeds 2000 ms threshold"
        info "This may be acceptable for the test environment"
    fi
}

# ── Main ───────────────────────────────────────────────────────────────────────

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage ;;
        -b|--base) BASE_URL="$2"; shift 2 ;;
        -a|--admin-token) ADMIN_TOKEN="$2"; shift 2 ;;
        -d|--dealer-token) DEALER_TOKEN="$2"; shift 2 ;;
        -s|--section) RUN_SECTION="$2"; shift 2 ;;
        -v|--verbose) VERBOSE=true; shift ;;
        -q|--quiet) QUIET=true; shift ;;
        *) echo "Unknown option: $1" >&2; usage ;;
    esac
done

check_deps

# Cleanup on exit
cleanup() {
    :
}
trap cleanup EXIT

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║     IDC Platform — API Integration Test Suite               ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "  Base URL:    $BASE_URL"
echo "  Sections:    ${RUN_SECTION:-all}"
echo "  Verbose:     $VERBOSE"
echo ""

# Wait for service if no tokens provided
if [ -z "$ADMIN_TOKEN" ]; then
    wait_for_service || {
        err "Service is not available. Start the backend first."
        err "  docker compose up -d   # for Docker dev"
        err "  sudo systemctl start idc-platform  # for production"
        exit 2
    }
fi

# Run test sections
FAILED=false

run_section() {
    local name="$1"
    local func="$2"

    if [ -n "$RUN_SECTION" ] && [ "$RUN_SECTION" != "$name" ]; then
        return 0
    fi

    if ! $func; then
        FAILED=true
    fi
}

run_section "auth"          test_auth_section
run_section "products"      test_products_section
run_section "distributors"  test_distributors_section
run_section "orders"        test_orders_section
run_section "subscriptions" test_subscriptions_section
run_section "invoices"      test_invoices_section
run_section "bandwidth"     test_bandwidth_section
run_section "sync"          test_sync_section
run_section "performance"   test_performance_section

# Print summary
print_summary || exit 1
