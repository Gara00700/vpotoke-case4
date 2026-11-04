#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

struct Reading {
    string time, location, indicator, value;
    string norm_min, norm_max, status;
};

vector<string> split(const string& line) {
    vector<string> cells;
    string cell;
    stringstream ss(line);
    while (getline(ss, cell, ',')) cells.push_back(cell);
    return cells;
}

set<string> load_sites() {
    set<string> sites;
    ifstream file("sites.csv");
    string line;
    getline(file, line);  // пропускаем заголовок
    while (getline(file, line)) sites.insert(split(line)[0]);
    return sites;
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

string validate(const Reading& r, const set<string>& sites) {
    if (sites.count(r.location) == 0) return "UNKNOWN_LOCATION";
    if (r.value != "" && !is_number(r.value)) return "VALIDATION_ERROR";
    if (!is_number(r.norm_min) || !is_number(r.norm_max)) return "VALIDATION_ERROR";
    return "";
}

int main() {
    set<string> sites = load_sites();
    if (sites.empty()) { cout << "Файл sites.csv не найден или пуст\n"; return 1; }

    ifstream file("indicators.csv");
    if (!file) { cout << "Файл indicators.csv не найден\n"; return 1; }

    vector<Reading> readings;
    string line;
    getline(file, line);  // пропускаем заголовок
    while (getline(file, line)) {
        vector<string> c = split(line);
        c.resize(7);
        Reading r{c[0], c[1], c[2], c[3], c[4], c[5], c[6]};
        string error = validate(r, sites);
        if (error != "") {
            cout << "ОТКЛОНЕНО " << r.time << " " << r.location << ": " << error << "\n";
            continue;
        }
        readings.push_back(r);
    }

    cout << "Принято строк: " << readings.size() << "\n";
}
