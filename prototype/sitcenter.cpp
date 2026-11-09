// Читает участки, показатели датчиков и инциденты из CSV, проверяет данные,
// применяет правила оповещений A-1…A-5 и жизненный цикл инцидента,
// записывает alerts_out.csv, incidents_out.csv и audit_log.csv.
//
// Сборка:  g++ -std=c++17 -O2 -o prototype/sitcenter prototype/sitcenter.cpp
// Запуск:  ./prototype/sitcenter [--data 01_данные] [--extra файл.csv]
//                                [--transitions файл.csv] [--employees файл.csv] [--out папка]
// Учётные записи:
//          ./prototype/sitcenter employee list   --employees extra_data/employees.csv
//          ./prototype/sitcenter employee add    <id> <ФИО> <роль> <участки> --actor <админ>
//          ./prototype/sitcenter employee assign <id> <участки> --actor <админ>
//          ./prototype/sitcenter employee disable <id> --actor <админ>
//          (участки через ';', например "site_01;site_02"; опции --employees и --data общие)

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

// ============================================================
// 1. Модель данных
// ============================================================

struct Reading {            // строка indicators.csv
    string time, location, indicator, value, norm_min, norm_max, status;
    long long minute = 0;   // время в минутах, для сортировки и окон правил
};

struct Incident {           // строка incidents.csv
    string id, type, location, time, responsible, status, deadline, source;
};

struct Alert {              // строка alerts_out.csv
    string id, at, location, indicator, value, severity, action;
};

struct Employee {           // строка employees.csv: учётная запись и зона ответственности
    string id, name, role;
    set<string> sites;      // участки, за которые отвечает сотрудник
    bool active = true;     // уволенного не удаляем, а отключаем: история в аудите остаётся
};

// ============================================================
// 2. Чтение CSV и разбор значений
// ============================================================

vector<string> split(string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();  // файлы из Windows
    vector<string> cells;
    string cell;
    stringstream ss(line);
    while (getline(ss, cell, ',')) cells.push_back(cell);
    return cells;
}

// Читает файл без строки заголовка. Возвращает false, если файла нет.
bool read_csv(const string& path, vector<vector<string>>& rows) {
    ifstream file(path);
    if (!file) return false;
    string line;
    getline(file, line);
    while (getline(file, line))
        if (!line.empty()) rows.push_back(split(line));
    return true;
}

bool is_number(const string& s) {
    try {
        size_t used;
        stod(s, &used);
        return used == s.size();
    } catch (...) {
        return false;
    }
}

// "2025-11-02T18:00:00Z" -> минуты от 1970-01-01. -1, если формат неверный.
long long to_minutes(const string& iso) {
    int y, mo, d, h, mi;
    if (sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d", &y, &mo, &d, &h, &mi) != 5) return -1;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59) return -1;
    y -= mo <= 2;                                    // алгоритм days_from_civil
    long long era = y / 400, yoe = y - era * 400;
    long long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return days * 1440 + h * 60 + mi;
}

// Обратное преобразование: минуты -> "YYYY-MM-DDTHH:MM:00Z".
string to_iso(long long minutes) {
    long long days = minutes / 1440 + 719468, rest = minutes % 1440;
    long long era = days / 146097, doe = days - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    long long d = doy - (153 * mp + 2) / 5 + 1, mo = mp < 10 ? mp + 3 : mp - 9;
    long long y = yoe + era * 400 + (mo <= 2);
    char buf[64];
    snprintf(buf, sizeof buf, "%04lld-%02lld-%02lldT%02lld:%02lld:00Z", y, mo, d, rest / 60, rest % 60);
    return buf;
}

// ============================================================
// 3. Проверка входных данных (коды ошибок из 10_API.yaml)
// ============================================================

const set<string> INDICATORS = {"temperature", "humidity", "ventilation"};
const set<string> STATUSES = {"NEW", "ASSIGNED", "IN_PROGRESS", "CLOSED"};

string validate_reading(const Reading& r, const set<string>& sites) {
    if (sites.count(r.location) == 0) return "UNKNOWN_LOCATION";
    if (r.minute < 0 || INDICATORS.count(r.indicator) == 0) return "VALIDATION_ERROR";
    if (r.value != "" && !is_number(r.value)) return "VALIDATION_ERROR";
    if (!is_number(r.norm_min) || !is_number(r.norm_max)) return "VALIDATION_ERROR";
    if (stod(r.norm_min) > stod(r.norm_max)) return "VALIDATION_ERROR";
    return "";
}

// Текст ответа об ошибке в формате контракта: 422 + код + поле.
string error_text(const string& code) {
    if (code == "UNKNOWN_LOCATION") return "422 VALIDATION_ERROR field=location code=UNKNOWN_LOCATION";
    if (code == "VALIDATION_ERROR") return "422 VALIDATION_ERROR";
    return code;
}

string validate_incident(const Incident& inc, const set<string>& sites) {
    if (sites.count(inc.location) == 0) return "UNKNOWN_LOCATION";
    if (inc.id.empty() || STATUSES.count(inc.status) == 0) return "VALIDATION_ERROR";
    if (to_minutes(inc.time) < 0 || to_minutes(inc.deadline) < 0) return "VALIDATION_ERROR";
    return "";
}

// ============================================================
// 3а. Учётные записи сотрудников и зоны ответственности
// ============================================================

const set<string> ROLES = {"dispatcher", "manager", "engineer", "admin"};

set<string> parse_sites(const string& text) {  // "site_01;site_02" -> {site_01, site_02}
    set<string> result;
    string item;
    stringstream ss(text);
    while (getline(ss, item, ';'))
        if (item != "") result.insert(item);
    return result;
}

string join_sites(const set<string>& sites) {
    string text;
    for (const string& s : sites) text += (text == "" ? "" : ";") + s;
    return text;
}

// employees.csv: employee_id,name,role,sites,active
bool load_employees(const string& path, map<string, Employee>& employees) {
    vector<vector<string>> rows;
    if (!read_csv(path, rows)) return false;
    for (auto& c : rows) {
        c.resize(5);
        employees[c[0]] = Employee{c[0], c[1], c[2], parse_sites(c[3]), c[4] != "no"};
    }
    return true;
}

void save_employees(const string& path, const map<string, Employee>& employees) {
    ofstream out(path);
    out << "employee_id,name,role,sites,active\n";
    for (const auto& [id, e] : employees)
        out << e.id << "," << e.name << "," << e.role << "," << join_sites(e.sites) << ","
            << (e.active ? "yes" : "no") << "\n";
}

// Активный сотрудник с нужной ролью, отвечающий за участок.
bool covers(const map<string, Employee>& employees, const string& id, const string& role, const string& site) {
    auto it = employees.find(id);
    return it != employees.end() && it->second.active && it->second.role == role && it->second.sites.count(site);
}

// Первый активный инженер участка: ему уходит автоинцидент. Пусто, если никого нет.
string engineer_for(const map<string, Employee>& employees, const string& site) {
    for (const auto& [id, e] : employees)
        if (covers(employees, id, "engineer", site)) return id;
    return "";
}

// ============================================================
// 4. Правила оповещений (02_правила.md)
// ============================================================

const int DEDUP_WINDOW_MIN = 5;      // A-5: дубль в окне 5 минут
const int CRITICAL_WINDOW_MIN = 15;  // A-3: 2 CRITICAL за 15 минут -> автоинцидент
const int SLA_CRITICAL_CLOSE_MIN = 60;  // автоинцидент всегда CRITICAL: закрыть за 1 ч

// Формула severity, 5 веток. Граница нормы входит в норму.
string calc_severity(const Reading& r) {
    if (r.value == "") return "STALE";
    double v = stod(r.value), lo = stod(r.norm_min), hi = stod(r.norm_max);
    if (lo <= v && v <= hi) return "NORMAL";
    double bound = v > hi ? hi : lo;
    if (bound == 0) return "CRITICAL";
    double deviation_pct = (v > hi ? v - hi : lo - v) / bound * 100;
    return deviation_pct > 10 ? "CRITICAL" : "WARNING";
}

// A-1, A-2, A-4: что делать с алертом.
string alert_action(const string& severity) {
    return severity == "WARNING" ? "ALERT_CREATED" : "ESCALATED";
}

// ============================================================
// 5. Жизненный цикл инцидента и RBAC
// ============================================================

struct Transition {
    string from, to;
    set<string> roles;
    string required_reason;  // пусто, если причина не нужна
};

const vector<Transition> TRANSITIONS = {
    {"NEW", "ASSIGNED", {"dispatcher", "manager"}, ""},
    {"ASSIGNED", "IN_PROGRESS", {"engineer"}, ""},
    {"IN_PROGRESS", "CLOSED", {"manager"}, ""},         // закрывает только руководитель (11_ИБ, U-2)
    {"ASSIGNED", "CLOSED", {"manager"}, "NO_ACTION"},  // ложный вызов
};
// Не перечислены, значит запрещены: NEW -> CLOSED, CLOSED -> любой.

string check_transition(const Incident& inc, const string& to, const string& role, const string& reason) {
    for (const Transition& t : TRANSITIONS) {
        if (t.from != inc.status || t.to != to) continue;
        if (t.roles.count(role) == 0) return "FORBIDDEN";
        if (t.required_reason != "" && reason != t.required_reason) return "INVALID_TRANSITION";
        return "";
    }
    return "INVALID_TRANSITION";
}

// ============================================================
// 6. Аудит: журнал только дополняется (append-only)
// ============================================================

struct AuditLog {
    vector<string> rows;
    void add(const string& entity_type, const string& entity_id, const string& action,
             const string& from, const string& to, const string& actor, const string& at) {
        char id[16];
        snprintf(id, sizeof id, "evt_%04zu", rows.size() + 1);
        rows.push_back(string(id) + "," + entity_type + "," + entity_id + "," + action + "," +
                       from + "," + to + "," + actor + "," + at);
    }
};

// ============================================================
// 7. Обработка показателей
// ============================================================

struct Report {
    vector<Alert> alerts;
    vector<Incident> auto_incidents;
    int rejected = 0, duplicates = 0, mismatches = 0;
};

void process_readings(vector<Reading>& readings, Report& rep, AuditLog& audit,
                      const map<string, Employee>& employees) {
    stable_sort(readings.begin(), readings.end(),
                [](const Reading& a, const Reading& b) { return a.minute < b.minute; });

    map<string, long long> last_event;     // участок|показатель -> время последнего события
    map<string, long long> last_critical;  // участок|показатель -> время последнего CRITICAL
    set<string> has_incident;              // по датчику уже создан автоинцидент

    for (const Reading& r : readings) {
        string key = r.location + "|" + r.indicator;

        // A-5: дедупликация
        if (last_event.count(key) && r.minute - last_event[key] < DEDUP_WINDOW_MIN) {
            rep.duplicates++;
            cout << "  DUPLICATE  " << r.time << " " << r.location << " " << r.indicator << "\n";
            continue;
        }
        last_event[key] = r.minute;

        string severity = calc_severity(r);
        if (r.status != "" && r.status != severity) {
            rep.mismatches++;
            cout << "  РАСХОЖДЕНИЕ " << r.time << " " << r.location << " " << r.indicator << "=" << r.value
                 << ": в файле " << r.status << ", по формуле " << severity << "\n";
        }
        if (severity == "NORMAL") continue;

        char id[16];
        snprintf(id, sizeof id, "alrt_%04zu", rep.alerts.size() + 1);
        Alert a{id, r.time, r.location, r.indicator, r.value, severity, alert_action(severity)};
        rep.alerts.push_back(a);
        audit.add("alert", a.id, "created", "", a.severity, "system", a.at);

        // A-3: второй CRITICAL по датчику за 15 минут -> автоинцидент
        if (severity != "CRITICAL") continue;
        bool second = last_critical.count(key) && r.minute - last_critical[key] <= CRITICAL_WINDOW_MIN;
        last_critical[key] = r.minute;
        if (second && !has_incident.count(key)) {
            has_incident.insert(key);
            char inc_id[16];
            snprintf(inc_id, sizeof inc_id, "inc_a%02zu", rep.auto_incidents.size() + 1);
            Incident inc{inc_id, r.indicator, r.location, r.time, engineer_for(employees, r.location), "NEW",
                         to_iso(r.minute + SLA_CRITICAL_CLOSE_MIN), "auto"};
            rep.auto_incidents.push_back(inc);
            audit.add("incident", inc.id, "created", "", "NEW", "system", r.time);
        }
    }
}

// ============================================================
// 8. Управление учётными записями (только роль admin)
// ============================================================

int employee_command(int argc, char** argv) {
    string data_dir = "01_данные", path = "extra_data/employees.csv", actor;
    vector<string> args;  // позиционные: команда и её параметры
    for (int i = 2; i < argc; i++) {
        string key = argv[i];
        if (i + 1 < argc && key == "--data") data_dir = argv[++i];
        else if (i + 1 < argc && key == "--employees") path = argv[++i];
        else if (i + 1 < argc && key == "--actor") actor = argv[++i];
        else args.push_back(key);
    }
    map<string, Employee> employees;
    load_employees(path, employees);
    string cmd = args.empty() ? "list" : args[0];

    if (cmd == "list") {
        for (const auto& [id, e] : employees)
            cout << "  " << id << "  " << e.role << "  " << e.name << "  [" << join_sites(e.sites) << "]"
                 << (e.active ? "" : "  ОТКЛЮЧЁН") << "\n";
        return 0;
    }

    // RBAC: менять учётные записи может только активный администратор.
    if (!employees.count(actor) || !employees[actor].active || employees[actor].role != "admin") {
        cout << "  ОТКАЗ: 403 FORBIDDEN (учётными записями управляет только admin)\n";
        return 1;
    }
    vector<vector<string>> rows;
    read_csv(data_dir + "/sites.csv", rows);
    set<string> known;
    for (auto& c : rows) known.insert(c[0]);

    string error, id = args.size() > 1 ? args[1] : "";
    set<string> sites;
    if (cmd == "add" && args.size() == 5) {
        sites = parse_sites(args[4]);
        if (id == "" || employees.count(id)) error = "422 VALIDATION_ERROR field=employee_id";
        else if (!ROLES.count(args[3])) error = "422 VALIDATION_ERROR field=role";
    } else if (cmd == "assign" && args.size() == 3) {
        sites = parse_sites(args[2]);
        if (!employees.count(id)) error = "422 VALIDATION_ERROR field=employee_id";
    } else if (cmd == "disable" && args.size() == 2) {
        if (!employees.count(id)) error = "422 VALIDATION_ERROR field=employee_id";
    } else {
        cout << "  Неверная команда, см. начало sitcenter.cpp\n";
        return 2;
    }
    for (const string& s : sites)
        if (!known.count(s)) error = error_text("UNKNOWN_LOCATION");
    if (error != "") { cout << "  ОТКАЗ: " << error << "\n"; return 1; }

    string before = employees.count(id) ? join_sites(employees[id].sites) : "";
    if (cmd == "add") employees[id] = Employee{id, args[2], args[3], sites, true};
    if (cmd == "assign") employees[id].sites = sites;
    if (cmd == "disable") employees[id].active = false;
    save_employees(path, employees);

    // Аудит изменений учётных записей: файл только дополняется.
    string log_path = path.substr(0, path.find_last_of('/') + 1) + "accounts_log.csv";
    bool fresh = !ifstream(log_path);
    ofstream log(log_path, ios::app);
    if (fresh) log << "action,employee_id,sites_before,sites_after,actor\n";
    log << cmd << "," << id << "," << before << "," << join_sites(employees[id].sites) << "," << actor << "\n";
    cout << "  OK " << cmd << " " << id << " [" << join_sites(employees[id].sites) << "]\n";
    return 0;
}

// ============================================================
// 9. Запуск
// ============================================================

int main(int argc, char** argv) {
    if (argc > 1 && string(argv[1]) == "employee") return employee_command(argc, argv);

    string data_dir = "01_данные", out_dir = ".", transitions_path, employees_path;
    vector<string> indicator_files;
    for (int i = 1; i + 1 < argc; i += 2) {
        string key = argv[i], val = argv[i + 1];
        if (key == "--data") data_dir = val;
        else if (key == "--extra") indicator_files.push_back(val);
        else if (key == "--transitions") transitions_path = val;
        else if (key == "--out") out_dir = val;
        else if (key == "--employees") employees_path = val;
    }
    indicator_files.insert(indicator_files.begin(), data_dir + "/indicators.csv");

    // Справочник участков
    vector<vector<string>> rows;
    if (!read_csv(data_dir + "/sites.csv", rows) || rows.empty()) {
        cout << "Ошибка: нет " << data_dir << "/sites.csv\n";
        return 2;
    }
    set<string> sites;
    for (auto& c : rows) sites.insert(c[0]);

    // Учётные записи: без файла работаем как раньше, роль берём из запроса перехода.
    map<string, Employee> employees;
    if (employees_path != "" && !load_employees(employees_path, employees)) {
        cout << "Ошибка: нет файла " << employees_path << "\n";
        return 2;
    }

    Report rep;
    AuditLog audit;

    // Показатели
    cout << "== Показатели ==\n";
    vector<Reading> readings;
    for (const string& path : indicator_files) {
        rows.clear();
        if (!read_csv(path, rows)) { cout << "Ошибка: нет файла " << path << "\n"; return 2; }
        for (auto& c : rows) {
            c.resize(7);
            Reading r{c[0], c[1], c[2], c[3], c[4], c[5], c[6], to_minutes(c[0])};
            string error = validate_reading(r, sites);
            if (error != "") {
                rep.rejected++;
                cout << "  ОТКЛОНЕНО  " << r.time << " " << r.location << " " << r.indicator << ": " << error_text(error) << "\n";
                continue;
            }
            readings.push_back(r);
        }
    }
    process_readings(readings, rep, audit, employees);

    // Инциденты из мобильного приложения + автоинциденты
    cout << "== Инциденты ==\n";
    vector<Incident> incidents;
    rows.clear();
    read_csv(data_dir + "/incidents.csv", rows);
    set<string> ids;
    for (auto& c : rows) {
        c.resize(7);
        Incident inc{c[0], c[1], c[2], c[3], c[4], c[5], c[6], "mobile"};
        string error = validate_incident(inc, sites);
        if (error == "" && ids.count(inc.id)) error = "DUPLICATE_EVENT";
        if (error != "") {
            rep.rejected++;
            cout << "  ОТКЛОНЕНО  " << inc.id << " " << inc.location << ": " << error_text(error) << "\n";
            continue;
        }
        ids.insert(inc.id);
        incidents.push_back(inc);
        audit.add("incident", inc.id, "created", "", inc.status, "mobile", inc.time);
    }
    for (const Incident& inc : rep.auto_incidents) {
        incidents.push_back(inc);
        cout << "  АВТОИНЦИДЕНТ " << inc.id << " " << inc.type << " " << inc.location
             << ", дедлайн " << inc.deadline << "\n";
    }

    // Переходы статусов (TransitionRequest)
    if (transitions_path != "") {
        rows.clear();
        read_csv(transitions_path, rows);
        for (auto& c : rows) {  // incident_id,to,actor_role,actor,responsible,reason,at
            c.resize(7);
            auto inc = find_if(incidents.begin(), incidents.end(), [&](const Incident& x) { return x.id == c[0]; });
            string error = inc == incidents.end() ? "VALIDATION_ERROR" : "";
            if (error == "" && !employees.empty()) {
                // Роль берём из учётной записи, а не из запроса.
                auto who = employees.find(c[3]);
                if (who == employees.end() || !who->second.active) error = "FORBIDDEN";
                else c[2] = who->second.role;
                // Инженер работает только на своих участках.
                if (error == "" && c[2] == "engineer" && !covers(employees, c[3], "engineer", inc->location))
                    error = "FORBIDDEN field=location code=OUT_OF_ZONE";
                // Назначить можно только инженера, который отвечает за этот участок.
                if (error == "" && c[1] == "ASSIGNED" && !covers(employees, c[4], "engineer", inc->location))
                    error = "422 VALIDATION_ERROR field=responsible code=OUT_OF_ZONE";
            }
            if (error == "") error = check_transition(*inc, c[1], c[2], c[5]);
            if (error != "") {
                cout << "  ПЕРЕХОД ОТКЛОНЁН " << c[0] << " -> " << c[1] << " (" << c[2] << "): " << error << "\n";
                continue;
            }
            string action = c[1] == "ASSIGNED" ? "assigned" : c[1] == "CLOSED" ? "closed" : "status_changed";
            audit.add("incident", inc->id, action, inc->status, c[1], c[3], c[6]);
            cout << "  ПЕРЕХОД " << inc->id << " " << inc->status << " -> " << c[1] << " (" << c[3] << ")\n";
            inc->status = c[1];
            if (c[1] == "ASSIGNED") inc->responsible = c[4];
        }
    }

    // Выходные файлы
    ofstream out(out_dir + "/alerts_out.csv");
    out << "alert_id,at,location,indicator,value,severity,action\n";
    for (const Alert& a : rep.alerts)
        out << a.id << "," << a.at << "," << a.location << "," << a.indicator << "," << a.value << ","
            << a.severity << "," << a.action << "\n";

    ofstream inc_out(out_dir + "/incidents_out.csv");
    inc_out << "incident_id,type,location,time,responsible,status,deadline,source\n";
    for (const Incident& i : incidents)
        inc_out << i.id << "," << i.type << "," << i.location << "," << i.time << "," << i.responsible << ","
                << i.status << "," << i.deadline << "," << i.source << "\n";

    ofstream audit_out(out_dir + "/audit_log.csv");
    audit_out << "event_id,entity_type,entity_id,action,from_status,to_status,actor,at\n";
    for (const string& row : audit.rows) audit_out << row << "\n";

    // Итог
    map<string, int> count;
    for (const Alert& a : rep.alerts) count[a.severity]++;
    cout << "== Итог ==\n"
         << "  показателей принято " << readings.size() << ", отклонено строк " << rep.rejected
         << ", дублей " << rep.duplicates << ", расхождений статуса " << rep.mismatches << "\n"
         << "  алертов: CRITICAL " << count["CRITICAL"] << ", WARNING " << count["WARNING"]
         << ", STALE " << count["STALE"] << "\n"
         << "  инцидентов " << incidents.size() << " (авто " << rep.auto_incidents.size() << ")"
         << ", событий аудита " << audit.rows.size() << "\n"
         << "  файлы: " << out_dir << "/alerts_out.csv, incidents_out.csv, audit_log.csv\n";
    return 0;
}
