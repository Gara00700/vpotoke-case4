#include <fstream>
#include <iostream>
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

int main() {
    ifstream file("indicators.csv");
    if (!file) { cout << "Файл indicators.csv не найден\n"; return 1; }

    vector<Reading> readings;
    string line;
    getline(file, line);  // пропускаем заголовок
    while (getline(file, line)) {
        vector<string> c = split(line);
        c.resize(7);
        readings.push_back({c[0], c[1], c[2], c[3], c[4], c[5], c[6]});
    }

    cout << "Прочитано строк: " << readings.size() << "\n";
    for (int i = 0; i < 3 && i < (int)readings.size(); i++)
        cout << readings[i].time << " " << readings[i].location << " "
             << readings[i].indicator << " = " << readings[i].value << "\n";
}
