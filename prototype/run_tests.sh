#!/usr/bin/env bash
# Прогон тестов T4-1…T4-6: вывод программы и выходные CSV сверяются с expected.txt.
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -o sitcenter sitcenter.cpp || exit 1
pass=0; fail=0
for t in tests/*/; do
  args=(--data "$t" --out "$t")
  [ -f "$t/transitions.csv" ] && args+=(--transitions "$t/transitions.csv")
  [ -f "$t/employees.csv" ] && args+=(--employees "$t/employees.csv")
  ./sitcenter "${args[@]}" > "$t/actual.txt"
  all=$(cat "$t/actual.txt" "$t/alerts_out.csv" "$t/incidents_out.csv" "$t/audit_log.csv")
  missing=$(while IFS= read -r line; do [ -n "$line" ] && ! grep -qF -- "$line" <<< "$all" && echo "$line"; done < "$t/expected.txt")
  if [ -z "$missing" ]; then echo "PASS  $(basename "$t")"; pass=$((pass+1))
  else echo "FAIL  $(basename "$t")"; echo "$missing" | sed 's/^/      нет: /'; fail=$((fail+1)); fi
done
echo "Итого: пройдено $pass, не пройдено $fail"
[ "$fail" -eq 0 ]
