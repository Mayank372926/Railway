// Indian Railway Ticket Booking System - C++17 backend, no external libraries.
// Build: g++ -std=c++17 -O2 -pthread backend/main.cpp -o backend/railway
// Run:   ./backend/railway 8080 frontend
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace std;

struct Station { string code, name, state; };
struct Stop { string code; int km, minute; };  // minute = minutes since departure day 00:00
struct Train { string number, name; vector<Stop> stops; };
struct Passenger { string name; int age; char gender; int seat; };
struct Booking {
    string pnr, train, date, from, to, cls, status, pay, payRef, contact;
    int fare = 0;
    vector<Passenger> pax;
};
struct ClassInfo { const char *id, *label; int coaches, perCoach; double rate; int resv; char prefix; };
struct Resp { int code; string body; };

// ---------- Sample data (illustrative schedules, NOT live IRCTC data) ----------
static const vector<Station> STATIONS = {
    {"NDLS", "New Delhi", "Delhi"},          {"MMCT", "Mumbai Central", "Maharashtra"},
    {"CSMT", "Mumbai CSMT", "Maharashtra"},  {"PUNE", "Pune Junction", "Maharashtra"},
    {"NGP", "Nagpur", "Maharashtra"},        {"ST", "Surat", "Gujarat"},
    {"BRC", "Vadodara", "Gujarat"},          {"ADI", "Ahmedabad", "Gujarat"},
    {"KOTA", "Kota Junction", "Rajasthan"},  {"JP", "Jaipur", "Rajasthan"},
    {"JU", "Jodhpur", "Rajasthan"},          {"UDZ", "Udaipur City", "Rajasthan"},
    {"AGC", "Agra Cantt", "Uttar Pradesh"},  {"GWL", "Gwalior", "Madhya Pradesh"},
    {"BPL", "Bhopal", "Madhya Pradesh"},     {"CNB", "Kanpur Central", "Uttar Pradesh"},
    {"LKO", "Lucknow", "Uttar Pradesh"},     {"PRYJ", "Prayagraj", "Uttar Pradesh"},
    {"BSB", "Varanasi", "Uttar Pradesh"},    {"PNBE", "Patna", "Bihar"},
    {"RNC", "Ranchi", "Jharkhand"},          {"R", "Raipur", "Chhattisgarh"},
    {"HWH", "Howrah (Kolkata)", "West Bengal"}, {"NJP", "New Jalpaiguri", "West Bengal"},
    {"GHY", "Guwahati", "Assam"},            {"BBS", "Bhubaneswar", "Odisha"},
    {"VSKP", "Visakhapatnam", "Andhra Pradesh"}, {"BZA", "Vijayawada", "Andhra Pradesh"},
    {"TPTY", "Tirupati", "Andhra Pradesh"},  {"SC", "Secunderabad", "Telangana"},
    {"MAS", "Chennai Central", "Tamil Nadu"}, {"CBE", "Coimbatore", "Tamil Nadu"},
    {"MDU", "Madurai", "Tamil Nadu"},        {"SBC", "Bengaluru City", "Karnataka"},
    {"MYS", "Mysuru", "Karnataka"},          {"MAQ", "Mangaluru Central", "Karnataka"},
    {"MAO", "Madgaon", "Goa"},               {"ERS", "Ernakulam", "Kerala"},
    {"TVC", "Thiruvananthapuram", "Kerala"}, {"CDG", "Chandigarh", "Chandigarh"},
    {"ASR", "Amritsar", "Punjab"},           {"JAT", "Jammu Tawi", "Jammu & Kashmir"},
    {"DDN", "Dehradun", "Uttarakhand"},
};
static const ClassInfo CLASSES[] = {
    {"SL", "Sleeper", 3, 72, 0.60, 20, 'S'},
    {"3A", "AC 3 Tier", 2, 64, 1.60, 40, 'B'},
    {"2A", "AC 2 Tier", 1, 48, 2.30, 50, 'A'},
    {"1A", "AC First", 1, 24, 3.90, 60, 'H'},
};
// number, name, stops as CODE@km@HHMM (time wraps past midnight automatically)
static const char* RAW[][3] = {
    {"12951", "Mumbai Rajdhani", "MMCT@0@1655 ST@263@1955 BRC@392@2125 KOTA@920@0250 NDLS@1384@0815"},
    {"12302", "Howrah Rajdhani", "NDLS@0@1700 CNB@440@2255 PRYJ@634@0045 PNBE@998@0525 HWH@1447@1005"},
    {"12627", "Karnataka Express", "SBC@0@1920 SC@710@0545 NGP@1290@1215 BPL@1640@1730 AGC@2180@0205 NDLS@2380@0620"},
    {"12621", "Tamil Nadu Express", "MAS@0@2200 BZA@430@0545 NGP@1090@1535 BPL@1440@2145 AGC@1980@0235 NDLS@2180@0640"},
    {"12009", "Mumbai Ahmedabad Shatabdi", "MMCT@0@0625 ST@263@0855 BRC@392@1015 ADI@492@1215"},
    {"12431", "Trivandrum Rajdhani", "TVC@0@1915 ERS@220@2315 MAQ@640@0545 MAO@1000@1115 ST@1853@2330 BRC@1982@0105 KOTA@2510@0710 NDLS@2975@1305"},
    {"12841", "Coromandel Express", "HWH@0@1450 BBS@440@2105 VSKP@880@0345 BZA@1230@0745 MAS@1660@1345"},
    {"12723", "Telangana Express", "SC@0@0650 NGP@580@1315 BPL@930@1900 AGC@1470@0150 NDLS@1670@0620"},
    {"12915", "Ashram Express", "ADI@0@1740 JP@630@0310 NDLS@930@0740"},
    {"12013", "Amritsar Shatabdi", "NDLS@0@0450 ASR@450@1055"},
    {"12345", "Saraighat Express", "HWH@0@1550 NJP@560@0540 GHY@960@1235"},
    {"12607", "Lalbagh Express", "MAS@0@1530 SBC@360@2130 MYS@500@0010"},
    {"11007", "Deccan Express", "CSMT@0@0715 PUNE@192@1030"},
    {"12056", "Dehradun Shatabdi", "NDLS@0@0645 DDN@300@1135"},
    {"12559", "Shiv Ganga Express", "NDLS@0@2005 PRYJ@634@0615 BSB@754@0830"},
    {"12003", "Lucknow Shatabdi", "NDLS@0@0610 LKO@510@1250"},
    {"12462", "Mandore Express", "JU@0@1730 JP@330@2330 NDLS@630@0655"},
    {"12992", "Mewar Express", "UDZ@0@1830 JP@430@0615"},
    {"12410", "Gondwana Express", "R@0@1520 NGP@290@2025 BPL@640@0220 GWL@940@0620 AGC@1060@0750 NDLS@1260@1235"},
    {"12637", "Pandian Express", "MAS@0@2115 MDU@490@0540"},
    {"12675", "Kovai Express", "MAS@0@0630 CBE@500@1315"},
    {"12797", "Venkatadri Express", "SC@0@1940 TPTY@570@0520"},
    {"12453", "Ranchi Rajdhani", "RNC@0@1630 PRYJ@650@0045 CNB@850@0300 NDLS@1290@0715"},
    {"12011", "Kalka Shatabdi", "NDLS@0@0740 CDG@245@1105"},
    {"12265", "Jammu Duronto", "NDLS@0@2205 JAT@580@0525"},
};

static vector<Train> trains;
static map<string, Booking> bookings;
static map<string, vector<char>> seatMap;  // "train|date|class" -> occupied flags
static mutex mtx;
static string frontendDir = "frontend";
static const string DB_FILE = "bookings.db";

// ---------- Helpers ----------
static vector<string> split(const string& s, char d) {
    vector<string> out;
    string cur;
    stringstream ss(s);
    while (getline(ss, cur, d)) out.push_back(cur);
    return out;
}
static string get(const map<string, string>& m, const string& k) {
    auto it = m.find(k);
    return it == m.end() ? "" : it->second;
}
static string jesc(const string& s) {
    string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c < 32) o += ' ';
        else o += c;
    }
    return o;
}
static string urlDecode(const string& s) {
    string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size() + 0 && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += (char)stoi(s.substr(i + 1, 2), nullptr, 16);
            i += 2;
        } else o += s[i];
    }
    return o;
}
static map<string, string> parseForm(const string& s) {
    map<string, string> m;
    for (auto& kv : split(s, '&')) {
        size_t eq = kv.find('=');
        if (eq == string::npos) continue;
        m[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
    }
    return m;
}
static Resp err(int code, const string& msg) { return {code, "{\"error\":\"" + jesc(msg) + "\"}"}; }
static bool allDigits(const string& s) { return !s.empty() && all_of(s.begin(), s.end(), [](unsigned char c) { return isdigit(c); }); }
static bool lettersOnly(const string& s, size_t lo, size_t hi) {
    return s.size() >= lo && s.size() <= hi && all_of(s.begin(), s.end(), [](unsigned char c) { return isalpha(c) || c == ' ' || c == '.'; });
}
static string hhmm(int minute) {
    char b[8];
    snprintf(b, sizeof b, "%02d:%02d", (minute % 1440) / 60, minute % 60);
    return b;
}
static const Station* findStation(const string& c) {
    for (auto& s : STATIONS) if (s.code == c) return &s;
    return nullptr;
}
static const ClassInfo* findClass(const string& id) {
    for (auto& c : CLASSES) if (id == c.id) return &c;
    return nullptr;
}
static const Train* findTrain(const string& n) {
    for (auto& t : trains) if (t.number == n) return &t;
    return nullptr;
}
static bool leg(const Train& t, const string& from, const string& to, int& fi, int& ti) {
    fi = ti = -1;
    for (int i = 0; i < (int)t.stops.size(); i++) {
        if (t.stops[i].code == from) fi = i;
        if (t.stops[i].code == to) ti = i;
    }
    return fi >= 0 && ti > fi;
}
static void loadTrains() {
    for (auto& r : RAW) {
        Train t{r[0], r[1], {}};
        int day = 0, prev = -1;
        for (auto& tok : split(r[2], ' ')) {
            auto p = split(tok, '@');
            int hm = stoi(p[2]), m = (hm / 100) * 60 + hm % 100;
            if (prev >= 0 && day * 1440 + m < prev) day++;
            prev = day * 1440 + m;
            t.stops.push_back({p[0], stoi(p[1]), prev});
        }
        trains.push_back(t);
    }
}
// "" when the date is fine, otherwise a message. Uses India time.
static string checkDate(const string& d) {
    int y, m, dd;
    if (d.size() != 10 || sscanf(d.c_str(), "%4d-%2d-%2d", &y, &m, &dd) != 3) return "Date must be YYYY-MM-DD";
    tm t{};
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = dd; t.tm_hour = 12; t.tm_isdst = -1;
    time_t when = mktime(&t);
    if (t.tm_year != y - 1900 || t.tm_mon != m - 1 || t.tm_mday != dd) return "Invalid calendar date";
    time_t now = time(nullptr);
    tm n = *localtime(&now);
    n.tm_hour = 12; n.tm_min = 0; n.tm_sec = 0; n.tm_isdst = -1;
    long diff = lround(difftime(when, mktime(&n)) / 86400.0);
    if (diff < 0) return "Journey date is in the past";
    if (diff > 120) return "Bookings open only 120 days in advance";
    return "";
}

// ---------- Seats, berths and fares ----------
static string berthType(const string& cls, int s) {  // s = seat number inside the coach, 1-based
    static const char* t8[] = {"LB", "MB", "UB", "LB", "MB", "UB", "SL", "SU"};
    static const char* t6[] = {"LB", "UB", "LB", "UB", "SL", "SU"};
    if (cls == "SL" || cls == "3A") return t8[(s - 1) % 8];
    if (cls == "2A") return t6[(s - 1) % 6];
    return (s % 2) ? "LB" : "UB";
}
static string berthLabel(const ClassInfo& c, int idx) {
    int seat = idx % c.perCoach + 1;
    return string(1, c.prefix) + to_string(idx / c.perCoach + 1) + "-" + to_string(seat) + " " + berthType(c.id, seat);
}
static vector<char>& seatsFor(const string& train, const string& date, const ClassInfo& c) {
    auto& v = seatMap[train + "|" + date + "|" + c.id];
    if (v.empty()) v.assign(c.coaches * c.perCoach, 0);
    return v;
}
static int freeSeats(const string& train, const string& date, const ClassInfo& c) {
    auto& v = seatsFor(train, date, c);
    return (int)count(v.begin(), v.end(), 0);
}
static int fareFor(const Train& t, int km, const ClassInfo& c) {
    double f = km * c.rate;
    for (const char* k : {"Rajdhani", "Shatabdi", "Duronto"})
        if (t.name.find(k) != string::npos) f *= 1.4;
    return (int)lround(f) + c.resv;
}

// ---------- Persistence ----------
static void saveAll() {
    ofstream f(DB_FILE, ios::trunc);
    for (auto& kv : bookings) {
        const Booking& b = kv.second;
        f << b.pnr << '\t' << b.train << '\t' << b.date << '\t' << b.from << '\t' << b.to << '\t' << b.cls << '\t'
          << b.status << '\t' << b.fare << '\t' << b.pay << '\t' << b.payRef << '\t' << b.contact << '\t';
        for (auto& p : b.pax) f << p.name << ':' << p.age << ':' << p.gender << ':' << p.seat << ';';
        f << '\n';
    }
}
static void loadAll() {
    ifstream f(DB_FILE);
    string line;
    while (getline(f, line)) {
        auto c = split(line, '\t');
        if (c.size() < 12 || !findClass(c[5])) continue;
        Booking b{c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[8], c[9], c[10], atoi(c[7].c_str()), {}};
        for (auto& ps : split(c[11], ';')) {
            auto p = split(ps, ':');
            if (p.size() == 4) b.pax.push_back({p[0], atoi(p[1].c_str()), p[2][0], atoi(p[3].c_str())});
        }
        if (b.status == "CONFIRMED")
            for (auto& p : b.pax) seatsFor(b.train, b.date, *findClass(b.cls))[p.seat] = 1;
        bookings[b.pnr] = b;
    }
}

// ---------- API handlers ----------
static Resp apiStations() {
    ostringstream o;
    o << "[";
    for (size_t i = 0; i < STATIONS.size(); i++) {
        if (i) o << ",";
        o << "{\"code\":\"" << STATIONS[i].code << "\",\"name\":\"" << jesc(STATIONS[i].name) << "\",\"state\":\"" << jesc(STATIONS[i].state) << "\"}";
    }
    o << "]";
    return {200, o.str()};
}
static Resp apiTrains(const map<string, string>& q) {
    string from = get(q, "from"), to = get(q, "to"), date = get(q, "date");
    if (!findStation(from) || !findStation(to)) return err(400, "Choose valid stations");
    if (from == to) return err(400, "Origin and destination must be different");
    string de = checkDate(date);
    if (!de.empty()) return err(400, de);
    lock_guard<mutex> lk(mtx);
    ostringstream o;
    o << "[";
    bool first = true;
    for (auto& t : trains) {
        int fi, ti;
        if (!leg(t, from, to, fi, ti)) continue;
        int km = t.stops[ti].km - t.stops[fi].km;
        int dep = t.stops[fi].minute, arr = t.stops[ti].minute, dur = arr - dep;
        if (!first) o << ",";
        first = false;
        o << "{\"number\":\"" << t.number << "\",\"name\":\"" << jesc(t.name) << "\",\"dep\":\"" << hhmm(dep) << "\",\"arr\":\"" << hhmm(arr)
          << "\",\"dayOffset\":" << (arr / 1440 - dep / 1440) << ",\"duration\":\"" << dur / 60 << "h " << dur % 60 << "m\",\"km\":" << km << ",\"classes\":[";
        for (int i = 0; i < 4; i++) {
            const ClassInfo& c = CLASSES[i];
            o << (i ? "," : "") << "{\"id\":\"" << c.id << "\",\"label\":\"" << c.label << "\",\"fare\":" << fareFor(t, km, c) << ",\"avail\":" << freeSeats(t.number, date, c) << "}";
        }
        o << "]}";
    }
    o << "]";
    return {200, o.str()};
}
static string ticketJson(const Booking& b) {
    const Train* t = findTrain(b.train);
    const ClassInfo* c = findClass(b.cls);
    int fi = 0, ti = 0;
    leg(*t, b.from, b.to, fi, ti);
    ostringstream o;
    o << "{\"pnr\":\"" << b.pnr << "\",\"train\":\"" << b.train << "\",\"trainName\":\"" << jesc(t->name) << "\",\"date\":\"" << b.date
      << "\",\"from\":\"" << jesc(findStation(b.from)->name) << " (" << b.from << ")\",\"to\":\"" << jesc(findStation(b.to)->name) << " (" << b.to
      << ")\",\"dep\":\"" << hhmm(t->stops[fi].minute) << "\",\"arr\":\"" << hhmm(t->stops[ti].minute) << "\",\"dayOffset\":"
      << (t->stops[ti].minute / 1440 - t->stops[fi].minute / 1440) << ",\"cls\":\"" << c->label << "\",\"status\":\"" << b.status
      << "\",\"fare\":" << b.fare << ",\"pay\":\"" << jesc(b.pay) << "\",\"payRef\":\"" << b.payRef << "\",\"contact\":\"XXXXXX" << b.contact.substr(6)
      << "\",\"passengers\":[";
    for (size_t i = 0; i < b.pax.size(); i++) {
        const Passenger& p = b.pax[i];
        o << (i ? "," : "") << "{\"name\":\"" << jesc(p.name) << "\",\"age\":" << p.age << ",\"gender\":\"" << p.gender << "\",\"berth\":\"" << berthLabel(*c, p.seat) << "\"}";
    }
    o << "]}";
    return o.str();
}
static bool luhn(const string& d) {
    int sum = 0;
    bool dbl = false;
    for (int i = (int)d.size() - 1; i >= 0; i--) {
        int x = d[i] - '0';
        if (dbl && (x *= 2) > 9) x -= 9;
        sum += x;
        dbl = !dbl;
    }
    return sum % 10 == 0;
}
// Simulated gateway: validates the details only, nothing is ever charged and card data is never stored.
static string checkPayment(const string& mode, const string& info, const string& cvv, string& label) {
    if (mode == "UPI") {
        size_t at = info.find('@');
        if (info.size() < 5 || info.size() > 50 || at == 0 || at == string::npos || at == info.size() - 1 || info.find(' ') != string::npos)
            return "Enter a valid UPI ID (name@bank)";
        label = "UPI";
    } else if (mode == "CARD") {
        string d;
        for (char ch : info) if (ch != ' ') d += ch;
        if (d.size() != 16 || !allDigits(d) || !luhn(d)) return "Invalid card number";
        if (cvv.size() != 3 || !allDigits(cvv)) return "Invalid CVV";
        label = "CARD ****" + d.substr(12);
    } else if (mode == "NET") {
        if (!lettersOnly(info, 2, 30)) return "Enter your bank name";
        label = "NETBANKING " + info;
    } else return "Choose a payment method";
    return "";
}
static string newPnr() {
    static mt19937_64 rng{random_device{}()};
    for (;;) {
        string p = to_string(uniform_int_distribution<long long>(1000000000LL, 9999999999LL)(rng));
        if (!bookings.count(p)) return p;
    }
}
static Resp apiBook(const map<string, string>& f) {
    string tn = get(f, "train"), date = get(f, "date"), from = get(f, "from"), to = get(f, "to");
    const Train* t = findTrain(tn);
    const ClassInfo* c = findClass(get(f, "cls"));
    int fi, ti;
    if (!t || !c || !leg(*t, from, to, fi, ti)) return err(400, "Invalid train, class or route");
    string de = checkDate(date);
    if (!de.empty()) return err(400, de);
    int n = atoi(get(f, "n").c_str());
    if (n < 1 || n > 6) return err(400, "You can book 1 to 6 passengers at a time");
    string contact = get(f, "contact");
    if (contact.size() != 10 || !allDigits(contact)) return err(400, "Enter a 10-digit mobile number");
    vector<Passenger> pax;
    vector<string> prefs;
    for (int i = 0; i < n; i++) {
        string k = to_string(i), name = get(f, "name" + k), g = get(f, "gender" + k);
        int age = atoi(get(f, "age" + k).c_str());
        string who = "Passenger " + to_string(i + 1) + ": ";
        if (!lettersOnly(name, 2, 40)) return err(400, who + "enter a valid name (letters only)");
        if (age < 1 || age > 120) return err(400, who + "age must be between 1 and 120");
        if (g != "M" && g != "F" && g != "O") return err(400, who + "choose a gender");
        pax.push_back({name, age, g[0], -1});
        prefs.push_back(get(f, "pref" + k));
    }
    string payLabel, perr = checkPayment(get(f, "pay"), get(f, "payinfo"), get(f, "cvv"), payLabel);
    if (!perr.empty()) return err(402, perr);

    lock_guard<mutex> lk(mtx);
    auto& seats = seatsFor(tn, date, *c);
    int left = (int)count(seats.begin(), seats.end(), 0);
    if (left < n) return err(409, left ? "Only " + to_string(left) + " seat(s) left in this class" : "No seats left in this class");
    for (int i = 0; i < n; i++) {  // preferred berth first, otherwise first free seat
        int pick = -1;
        for (int s = 0; s < (int)seats.size() && pick < 0; s++)
            if (!seats[s] && berthType(c->id, s % c->perCoach + 1) == prefs[i]) pick = s;
        for (int s = 0; s < (int)seats.size() && pick < 0; s++)
            if (!seats[s]) pick = s;
        seats[pick] = 1;
        pax[i].seat = pick;
    }
    Booking b;
    b.pnr = newPnr(); b.train = tn; b.date = date; b.from = from; b.to = to; b.cls = c->id; b.status = "CONFIRMED";
    b.fare = n * fareFor(*t, t->stops[ti].km - t->stops[fi].km, *c);
    b.pay = payLabel; b.payRef = "PAY" + b.pnr.substr(2); b.contact = contact; b.pax = pax;
    bookings[b.pnr] = b;
    saveAll();
    return {200, ticketJson(b)};
}
static Resp apiTicket(const map<string, string>& q) {
    lock_guard<mutex> lk(mtx);
    auto it = bookings.find(get(q, "pnr"));
    if (it == bookings.end()) return err(404, "No booking found for this PNR");
    return {200, ticketJson(it->second)};
}
static Resp apiCancel(const map<string, string>& f) {
    lock_guard<mutex> lk(mtx);
    auto it = bookings.find(get(f, "pnr"));
    if (it == bookings.end()) return err(404, "No booking found for this PNR");
    Booking& b = it->second;
    if (b.status != "CONFIRMED") return err(409, "This ticket is already cancelled");
    auto& seats = seatsFor(b.train, b.date, *findClass(b.cls));
    for (auto& p : b.pax) seats[p.seat] = 0;
    b.status = "CANCELLED";
    saveAll();
    string j = ticketJson(b);
    j.pop_back();
    return {200, j + ",\"refund\":" + to_string(b.fare * 90 / 100) + "}"};
}

// ---------- Minimal HTTP server ----------
static void sendAll(int fd, const string& s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return;
        off += n;
    }
}
static void reply(int fd, int code, const string& type, const string& body) {
    static const map<int, string> reason = {{200, "OK"}, {400, "Bad Request"}, {402, "Payment Required"}, {404, "Not Found"}, {409, "Conflict"}, {413, "Payload Too Large"}};
    ostringstream h;
    h << "HTTP/1.1 " << code << " " << (reason.count(code) ? reason.at(code) : "Error") << "\r\nContent-Type: " << type
      << "\r\nContent-Length: " << body.size() << "\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n";
    sendAll(fd, h.str() + body);
}
static void handleClient(int fd) {
    timeval tv{10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    string req;
    char buf[4096];
    size_t he;
    while ((he = req.find("\r\n\r\n")) == string::npos) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0 || req.size() > 16384) { close(fd); return; }
        req.append(buf, n);
    }
    string head = req.substr(0, he), body = req.substr(he + 4), lower = head;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    size_t cl = lower.find("content-length:");
    size_t want = cl == string::npos ? 0 : (size_t)atol(lower.c_str() + cl + 15);
    if (want > 16384) { reply(fd, 413, "application/json", err(413, "Body too large").body); close(fd); return; }
    while (body.size() < want) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) { close(fd); return; }
        body.append(buf, n);
    }
    istringstream rl(head);
    string method, target, path, query;
    rl >> method >> target;
    path = target;
    size_t qm = target.find('?');
    if (qm != string::npos) { path = target.substr(0, qm); query = target.substr(qm + 1); }

    Resp r;
    string ctype = "application/json";
    if (method == "GET" && (path == "/" || path == "/index.html")) {
        ifstream f(frontendDir + "/index.html", ios::binary);
        stringstream ss;
        ss << f.rdbuf();
        if (!f) r = err(404, "Frontend not found");
        else { r = {200, ss.str()}; ctype = "text/html; charset=utf-8"; }
    }
    else if (method == "GET" && path == "/api/stations") r = apiStations();
    else if (method == "GET" && path == "/api/trains") r = apiTrains(parseForm(query));
    else if (method == "GET" && path == "/api/ticket") r = apiTicket(parseForm(query));
    else if (method == "POST" && path == "/api/book") r = apiBook(parseForm(body));
    else if (method == "POST" && path == "/api/cancel") r = apiCancel(parseForm(body));
    else r = err(404, "Not found");
    reply(fd, r.code, ctype, r.body);
    close(fd);
}
int main(int argc, char** argv) {
    setenv("TZ", "Asia/Kolkata", 1);
    tzset();
    signal(SIGPIPE, SIG_IGN);
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    if (argc > 2) frontendDir = argv[2];
    loadTrains();
    loadAll();
    int srv = socket(AF_INET, SOCK_STREAM, 0), on = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(port);
    if (bind(srv, (sockaddr*)&a, sizeof a) < 0 || listen(srv, 64) < 0) { perror("bind/listen"); return 1; }
    printf("Railway server on http://0.0.0.0:%d  (%zu stations, %zu trains, %zu bookings loaded)\n", port, STATIONS.size(), trains.size(), bookings.size()), fflush(stdout);
    for (;;) {
        int fd = accept(srv, nullptr, nullptr);
        if (fd >= 0) thread(handleClient, fd).detach();
    }
}
