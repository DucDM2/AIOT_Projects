#!/usr/bin/env bash
# Kiểm tra MISRA C:2012 cho lõi relay_core bằng cppcheck + addon misra.py.
#   bash tools/misra_check.sh            → báo cáo ở reports/misra.txt
#
# Ghi chú: một số bản cppcheck trên Windows chạy "--addon=misra" mà KHÔNG báo gì
# (không tìm thấy Python) → script tạo file .dump rồi gọi misra.py trực tiếp cho chắc.
# Muốn in nội dung luật: tự soạn file luật (MISRA cấm phân phối nguyên văn) rồi đặt
# MISRA_RULE_TEXTS=đường_dẫn_file.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/reports"
WORK="$OUT/misra_work"
CORE="$ROOT/components/relay_core"

CPPCHECK_BIN="$(command -v cppcheck)"
ADDONS="$(dirname "$CPPCHECK_BIN")/../share/Cppcheck/addons"
[ -f "$ADDONS/misra.py" ] || ADDONS="$(dirname "$CPPCHECK_BIN")/addons"
[ -f "$ADDONS/misra.py" ] || { echo "Khong tim thay misra.py cua cppcheck"; exit 2; }
# Chọn Python chạy được thật (trên Windows "python3" thường là lối tắt Microsoft Store giả).
PY=""
for cand in python python3 py; do
    if command -v "$cand" >/dev/null 2>&1 && "$cand" -c "import sys" >/dev/null 2>&1; then
        PY="$(command -v "$cand")"; break
    fi
done
[ -n "$PY" ] || { echo "Khong tim thay Python"; exit 2; }

rm -rf "$WORK"
mkdir -p "$WORK"
cp "$CORE"/src/*.c "$CORE"/port/esp32/*.c "$WORK"/

RULE_ARGS=()
if [ -n "${MISRA_RULE_TEXTS:-}" ]; then RULE_ARGS=(--rule-texts="$MISRA_RULE_TEXTS"); fi

for f in "$WORK"/*.c; do
    cppcheck --dump --quiet --std=c99 --platform=unix32 -I "$CORE/include" "$f" >/dev/null 2>&1
done

# Lỗi chung (không phải MISRA) của cppcheck
cppcheck --quiet --std=c99 --platform=unix32 --enable=warning,style,portability,performance \
    --inline-suppr -I "$CORE/include" "$WORK"/*.c \
    --template='{file}:{line}: [{id}] {message}' 2> "$OUT/cppcheck.txt" || true

"$PY" "$ADDONS/misra.py" "${RULE_ARGS[@]}" "$WORK"/*.dump > "$OUT/misra.txt" 2>&1 || true

# Bỏ các cảnh báo đã được chấp thuận lệch chuẩn (DEVIATION) bằng cppcheck-suppress trong mã.
echo "== cppcheck (loi chung) =="
sed "s#$WORK/##" "$OUT/cppcheck.txt" || true
echo "== MISRA C:2012 =="
grep -E "^\[" "$OUT/misra.txt" | sed "s#.*misra_work[/\\\\]##" | sort | uniq \
    | sed -E 's#^(reg_io_esp32\.c:.*misra-c2012-11\.4.*)$#\1  <== DEVIATION D-01 (da chap thuan)#' || true
echo "-- Tong hop theo luat (sau khi tru lech chuan da chap thuan) --"
grep -E "^\[" "$OUT/misra.txt" | grep -vE "reg_io_esp32\.c:[0-9]+\].*misra-c2012-11\.4" \
    | grep -oE "misra-c2012-[0-9.]+" | sort | uniq -c | sort -rn || true
UNAPPROVED=$(grep -E "^\[" "$OUT/misra.txt" | grep -vcE "reg_io_esp32\.c:[0-9]+\].*misra-c2012-11\.4" || true)
echo "Vi pham chua chap thuan (relay_core): ${UNAPPROVED}"

# ---------------------------------------------------------------------------
# Phần glue ESP-IDF (main/) – CHỈ THAM KHẢO: phân tích không có header ESP-IDF nên
# Rule 17.3 (hàm chưa khai báo) là dương tính giả và bị lọc bỏ. Xem docs/MISRA.md.
# ---------------------------------------------------------------------------
WMAIN="$OUT/misra_main"
rm -rf "$WMAIN"; mkdir -p "$WMAIN"
cp "$ROOT"/main/*.c "$ROOT"/main/*.h "$WMAIN"/
cp "$ROOT/main/app_secrets.example.h" "$WMAIN/app_secrets.h"
cat > "$WMAIN/sdkconfig.h" <<'CFG'
#define CONFIG_APP_PIN_RELAY1 16
#define CONFIG_APP_PIN_RELAY2 17
#define CONFIG_APP_PIN_LED 23
#define CONFIG_APP_PIN_BUTTON 0
#define CONFIG_APP_RELAY_ACTIVE_HIGH 1
#define CONFIG_APP_LED_ACTIVE_LOW 1
#define CONFIG_APP_RELAY_MIN_SWITCH_MS 300
#define CONFIG_APP_BUTTON_LONG_MS 1500
#define CONFIG_APP_MQTT_TOPIC_PREFIX "relay2"
#define CONFIG_APP_STATUS_HEARTBEAT_S 60
CFG
for f in "$WMAIN"/*.c; do
    cppcheck --dump --quiet --std=c99 --platform=unix32 -I "$WMAIN" -I "$CORE/include" "$f" >/dev/null 2>&1
done
"$PY" "$ADDONS/misra.py" "${RULE_ARGS[@]}" "$WMAIN"/*.dump > "$OUT/misra_main.txt" 2>&1 || true
echo "== MISRA C:2012 – main/ (glue ESP-IDF, tham khao, da loc 17.3 + misra-config) =="
grep -E "^\[" "$OUT/misra_main.txt" | grep -vE "misra-c2012-17\.3|misra-config" \
    | sed "s#.*misra_main[/\\\\]##" | sort | uniq || true
echo "Vi pham trong main/: $(grep -E '^\[' "$OUT/misra_main.txt" | grep -vcE 'misra-c2012-17\.3|misra-config' || true)"

[ "${UNAPPROVED}" = "0" ]
