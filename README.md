# Кейс 4 «Ситуационный центр: мониторинг и инциденты» — команда 7

Кейс-чемпионат «ВПОТОКЕ». Диспетчерская птицеводческого предприятия: мониторинг показателей датчиков и управление инцидентами.

## Прототип (C++17)

    g++ -std=c++17 -O2 -o prototype/sitcenter prototype/sitcenter.cpp
    ./prototype/sitcenter --employees extra_data/employees.csv --out frontend/data
    bash prototype/run_tests.sh          # тесты T4-1…T4-5

Параметры: `--data` папка с sites/indicators/incidents (по умолчанию `01_данные`), `--extra` дополнительный файл показателей, `--transitions` файл переходов статусов, `--employees` учётные записи сотрудников, `--out` куда писать результат.

Выход: `alerts_out.csv` (alert_id, at, location, indicator, value, severity, action), `incidents_out.csv`, `audit_log.csv`.

Правила: формула severity из 02_правила.md (граница нормы — норма, до 10% — WARNING, больше — CRITICAL, пусто — STALE), A-3 автоинцидент при 2 CRITICAL за 15 мин, A-5 дубль в окне 5 мин, жизненный цикл и роли — раздел 5 кода.

## Учётные записи и зоны ответственности

`extra_data/employees.csv`: employee_id, name, role (dispatcher / manager / engineer / admin), sites (участки через `;`), active. Менять записи может только администратор:

    ./prototype/sitcenter employee list
    ./prototype/sitcenter employee add e_106 "Петров П.П." engineer "site_01;site_03" --actor a_001
    ./prototype/sitcenter employee assign e_106 "site_02" --actor a_001
    ./prototype/sitcenter employee disable e_106 --actor a_001

Каждое изменение дописывается в `extra_data/accounts_log.csv`. С `--employees` программа берёт роль из учётной записи, автоинцидент назначает инженеру участка, не даёт назначить инженера чужого участка (`OUT_OF_ZONE`) и не пускает инженера к инцидентам чужих участков.

## Панель диспетчера (frontend)

    python3 -m http.server 8000
    # открыть http://localhost:8000/frontend/

Страница читает файлы из `frontend/data` и обновляется каждые 5 секунд. Если открыть `index.html` двойным щелчком, страница попросит выбрать CSV вручную.

## Структура

| Путь | Что лежит |
| --- | --- |
| `01_данные/` | исходные CSV организатора, не изменяются |
| `extra_data/employees.csv` | учётные записи сотрудников и закреплённые участки |
| `02_правила.md` … `11_ИБ.md` | материалы кейса по шаблонам организатора |
| `04_прототип.py` | шаблон организатора на Python (для справки) |
| `prototype/` | прототип на C++17 и тесты T4-1…T4-5 |
| `frontend/` | панель диспетчера (HTML без зависимостей) |
| `diagrams/schemes.drawio` | схемы: потоки данных, ER, жизненный цикл |
