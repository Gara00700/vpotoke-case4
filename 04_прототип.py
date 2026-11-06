#!/usr/bin/env python3
"""
Запуск прототипа — кейс 4, команда 7.

Сам прототип написан на C++17: prototype/sitcenter.cpp.
Этот файл собирает программу и запускает её на данных кейса.

    python3 04_прототип.py           # данные из 01_данные, результат в out/
    python3 04_прототип.py --tests   # прогон тестов T4-1…T4-5

Результат: out/alerts_out.csv, out/incidents_out.csv, out/audit_log.csv.
Нужен компилятор g++ с поддержкой C++17.
"""
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parent
src = root / "prototype" / "sitcenter.cpp"
exe = root / "prototype" / "sitcenter"

if "--tests" in sys.argv:
    sys.exit(subprocess.call(["bash", str(root / "prototype" / "run_tests.sh")]))

subprocess.run(["g++", "-std=c++17", "-O2", "-o", str(exe), str(src)], check=True)
out = root / "out"
out.mkdir(exist_ok=True)
sys.exit(subprocess.call([str(exe), "--data", str(root / "01_данные"),
                          "--employees", str(root / "extra_data" / "employees.csv"),
                          "--out", str(out)], cwd=root))
