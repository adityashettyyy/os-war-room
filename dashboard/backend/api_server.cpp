// OS War Room — API server.
// Dependency-free HTTP/1.1 server (POSIX sockets / Winsock) that runs the simulation on a background
// thread, exposes it as a JSON API and serves the built dashboard. The dashboard polls /api/state.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <mutex>
#include <thread>

#include "../../simulation/detective.hpp"
#include "../../simulation/experiment_lab.hpp"
#include "../../simulation/simulation_engine.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static void closeSock(socket_t s) { closesocket(s); }
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
static const socket_t INVALID_SOCKET = -1;
static void closeSock(socket_t s) { close(s); }
#endif

using namespace owr;
namespace fs = std::filesystem;

struct Request {
    std::string method, path, query, body;
    std::map<std::string, std::string> params;
};
struct Response {
    int status = 200;
    std::string type = "application/json", body;
};

static Response jsonOk(const std::string& body) { Response r; r.body = body; return r; }
static Response jsonErr(int status, const std::string& msg) {
    Response r; r.status = status;
    json::Writer w; w.obj().k("error").v(msg).end();
    r.body = w.str();
    return r;
}

// ------------------------------------------------------------------------------------------------
class Engine {
public:
    Engine(std::string scenarioDir) : scenarios_(std::move(scenarioDir)) {
        thread_ = std::thread([this] { loop(); });
    }
    ~Engine() { stop_ = true; if (thread_.joinable()) thread_.join(); }

    std::mutex mutex;
    Simulation sim;
    bool running = false;
    int speed = 20;                       // ticks per second
    std::string scenarios_;

private:
    void loop() {
        double acc = 0;
        while (!stop_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::lock_guard<std::mutex> lock(mutex);
            if (!running) { acc = 0; continue; }
            acc += speed * 0.05;
            int n = (int)acc;
            acc -= n;
            if (n > 0) sim.step(n);
        }
    }
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

static std::mutex detectiveMutex;

static std::string stateEnvelope(Engine& e) {
    json::Writer w;
    w.obj().k("running").v(e.running).k("speed").v(e.speed).k("pendingActions").v((long)e.sim.pendingActions()).k("state").raw(e.sim.kernel().stateJson()).end();
    return w.str();
}

static std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) { if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static WorkloadSpec workloadFromJson(const json::Value& j) {
    WorkloadSpec w;
    w.cpuBound = std::clamp(j["cpu"].integer(w.cpuBound), 0, 60);
    w.ioBound = std::clamp(j["io"].integer(w.ioBound), 0, 60);
    w.interactive = std::clamp(j["ui"].integer(w.interactive), 0, 60);
    w.memMinKB = std::clamp(j["memMinKB"].integer(w.memMinKB), 1, 4096);
    w.memMaxKB = std::clamp(j["memMaxKB"].integer(w.memMaxKB), 1, 4096);
    w.arrivalSpread = std::clamp(j["arrivalSpread"].integer(w.arrivalSpread), 0, 1000);
    w.ioScale = std::clamp(j["ioScale"].num(w.ioScale), 0.1, 10.0);
    w.seed = (uint32_t)j["seed"].num(w.seed);
    if (w.total() < 1) throw std::runtime_error("workload needs at least one process");
    if (w.total() > 100) throw std::runtime_error("workload limited to 100 processes");
    return w;
}

static std::vector<int> intList(const json::Value& v) {
    std::vector<int> r;
    for (auto& x : v.a) r.push_back(x.integer());
    return r;
}
static std::vector<std::vector<int>> matrix(const json::Value& v) {
    std::vector<std::vector<int>> m;
    for (auto& row : v.a) m.push_back(intList(row));
    return m;
}

static std::string scenarioList(const std::string& dir) {
    json::Writer w;
    w.obj().k("scenarios").arr();
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto& f : fs::directory_iterator(dir, ec)) if (f.path().extension() == ".json") files.push_back(f.path());
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        try {
            auto d = json::parse(Simulation::readFile(f.string()));
            ScenarioInfo i;
            i.name = f.stem().string(); i.title = d["title"].str(i.name); i.description = d["description"].str(); i.suggestedTicks = d["suggestedTicks"].integer(200);
            for (auto& x : d["demonstrates"].a) i.demonstrates.push_back(x.str());
            w.raw(i.toJson());
        } catch (const std::exception&) {}
    }
    w.end().end();
    return w.str();
}

static Response route(Engine& e, const Request& rq) {
    const std::string& p = rq.path;
    json::Value body;
    if (rq.method == "POST" && !rq.body.empty()) body = json::parse(rq.body);

    if (p == "/api/health") return jsonOk(R"({"ok":true,"name":"OS War Room","version":"1.0.0"})");

    // ---- read-only views ----
    if (rq.method == "GET" && p == "/api/state") { std::lock_guard<std::mutex> l(e.mutex); return jsonOk(stateEnvelope(e)); }
    if (rq.method == "GET" && p.rfind("/api/process/", 0) == 0) {
        std::lock_guard<std::mutex> l(e.mutex);
        std::string rest = p.substr(13);
        if (rest.find('/') == std::string::npos && !rest.empty() && std::all_of(rest.begin(), rest.end(), ::isdigit)) return jsonOk(e.sim.kernel().processJson(std::atoi(rest.c_str())));
    }
    if (rq.method == "GET" && p == "/api/log") {
        std::lock_guard<std::mutex> l(e.mutex);
        std::set<Category> cats;
        auto it = rq.params.find("cats");
        if (it != rq.params.end())
            for (auto& c : splitCsv(it->second)) if (auto cat = parseEnum<Category>(CAT_NAMES, c)) cats.insert(*cat);
        auto get = [&](const char* k, long d) { auto f = rq.params.find(k); return f == rq.params.end() ? d : std::atol(f->second.c_str()); };
        return jsonOk(e.sim.kernel().logJson(get("since", 0), cats, (int)get("pid", -1), (size_t)std::clamp(get("limit", 300), 1L, 2000L)));
    }
    if (rq.method == "GET" && p == "/api/scenarios") return jsonOk(scenarioList(e.scenarios_));
    if (rq.method == "GET" && p == "/api/failures") {
        json::Writer w; w.obj().k("failures").arr();
        for (auto& [id, title] : failure::catalog()) w.obj().k("id").v(id).k("title").v(title).end();
        w.end().end();
        return jsonOk(w.str());
    }

    // ---- simulation control ----
    if (rq.method == "POST" && p == "/api/control") {
        std::lock_guard<std::mutex> l(e.mutex);
        std::string a = body["action"].str();
        if (body.has("speed")) e.speed = std::clamp(body["speed"].integer(e.speed), 1, 1000);
        if (a == "run") e.running = true;
        else if (a == "pause") e.running = false;
        else if (a == "step") { e.running = false; e.sim.step(std::clamp(body["steps"].integer(1), 1, 10000)); }
        else if (a == "reset") {
            e.running = false;
            Config c;
            applyConfig(c, body["config"]);
            e.sim.reset(c);
        } else if (!a.empty() && a != "speed") return jsonErr(400, "unknown control action '" + a + "'");
        return jsonOk(stateEnvelope(e));
    }
    if (rq.method == "POST" && p == "/api/process") {
        std::lock_guard<std::mutex> l(e.mutex);
        ProcessSpec s = specFromJson(body);
        int pid = e.sim.kernel().createProcess(s);
        if (pid < 0) return jsonErr(409, "process table is full or program invalid");
        json::Writer w; w.obj().k("pid").v(pid).end();
        return jsonOk(w.str());
    }
    if (rq.method == "POST" && p.rfind("/api/process/", 0) == 0 && p.size() > 5 && p.substr(p.size() - 5) == "/kill") {
        std::lock_guard<std::mutex> l(e.mutex);
        int pid = std::atoi(p.substr(13, p.size() - 18).c_str());
        if (!e.sim.kernel().shellTerminate(pid)) return jsonErr(404, "no such live process");
        return jsonOk(R"({"ok":true})");
    }
    if (rq.method == "POST" && p == "/api/config") {
        std::lock_guard<std::mutex> l(e.mutex);
        Kernel& k = e.sim.kernel();
        if (body.has("physicalKB") || body.has("memoryKB") || body.has("pageSizeKB"))
            return jsonErr(400, "physical memory and page size can only change on reset");
        Config c = k.config();
        applyConfig(c, body);                     // validates every field
        if (body.has("scheduler") || body.has("quantum")) k.setScheduler(c.sched, c.quantum);
        if (body.has("replacement")) k.setReplacement(c.repl);
        Config& live = k.mutableConfig();
        live.aging = c.aging; live.agingInterval = c.agingInterval; live.thrashingControl = c.thrashingControl;
        live.autoDeadlockRecovery = c.autoDeadlockRecovery; live.recovery = c.recovery; live.ctxSwitchCost = c.ctxSwitchCost;
        live.pageFaultLatency = c.pageFaultLatency; live.deadlockRecoveryDelay = c.deadlockRecoveryDelay; live.starvationTicks = c.starvationTicks;
        live.deviceRepairTicks = c.deviceRepairTicks;
        return jsonOk(stateEnvelope(e));
    }
    if (rq.method == "POST" && p == "/api/failure") {
        std::lock_guard<std::mutex> l(e.mutex);
        auto r = failure::inject(e.sim.kernel(), body["action"].str(), body);
        return jsonOk(r.toJson());
    }
    if (rq.method == "POST" && p == "/api/scenario") {
        std::string name = body["name"].str();
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '-'; }))
            return jsonErr(400, "invalid scenario name");
        std::string text;
        try { text = Simulation::readFile(e.scenarios_ + "/" + name + ".json"); } catch (...) { return jsonErr(404, "no such scenario"); }
        std::lock_guard<std::mutex> l(e.mutex);
        e.running = false;
        auto info = e.sim.loadScenario(json::parse(text));
        json::Writer w; w.obj().k("scenario").raw(info.toJson()).k("state").raw(stateEnvelope(e)).end();
        return jsonOk(w.str());
    }
    if (rq.method == "POST" && p == "/api/deadlock/recover") {
        std::lock_guard<std::mutex> l(e.mutex);
        e.sim.kernel().recoverDeadlocksNow();
        return jsonOk(R"({"ok":true})");
    }
    if (rq.method == "POST" && p == "/api/ipc/send") {
        std::lock_guard<std::mutex> l(e.mutex);
        if (!e.sim.kernel().shellSend(body["queue"].str("inbox"), body["text"].str("hello"))) return jsonErr(409, "queue full");
        return jsonOk(R"({"ok":true})");
    }
    if (rq.method == "POST" && p == "/api/sync/setup") {
        std::lock_guard<std::mutex> l(e.mutex);
        Kernel& k = e.sim.kernel();
        for (auto& m : body["mutexes"].a) k.sync().createMutex(m.str());
        for (auto& s : body["semaphores"].a) k.sync().createSemaphore(s["name"].str(), s["value"].integer(1));
        for (auto& q : body["queues"].a) k.ipc().createQueue(q["name"].str(), q["capacity"].integer(4));
        return jsonOk(R"({"ok":true})");
    }
    if (rq.method == "POST" && p == "/api/translate") {
        std::lock_guard<std::mutex> l(e.mutex);
        auto t = e.sim.kernel().memory().translate(body["pid"].integer(), (long)body["vaddr"].num());
        json::Writer w;
        w.obj().k("valid").v(t.valid).k("page").v(t.page).k("offset").v(t.offset).k("present").v(t.present).k("frame").v(t.frame).k("physical").v((long long)t.physical).end();
        return jsonOk(w.str());
    }

    // ---- labs (use private kernels; never touch the live simulation) ----
    if (rq.method == "POST" && p == "/api/experiment") {
        WorkloadSpec w = workloadFromJson(body["workload"]);
        Config c;
        c.thrashingControl = false;
        applyConfig(c, body["config"]);
        std::vector<SchedPolicy> sp;
        for (auto& s : body["schedulers"].a) { auto x = parseSched(s.str()); if (!x) return jsonErr(400, "unknown scheduler"); sp.push_back(*x); }
        if (sp.empty()) sp = {SchedPolicy::FCFS, SchedPolicy::RR, SchedPolicy::MLFQ, SchedPolicy::ADAPTIVE};
        std::vector<ReplPolicy> rp;
        for (auto& s : body["replacements"].a) { auto x = parseRepl(s.str()); if (!x) return jsonErr(400, "unknown replacement policy"); rp.push_back(*x); }
        json::Writer out;
        out.obj().k("schedulers").arr();
        for (auto& r : compareSchedulers(w, c, sp)) out.raw(r.toJson());
        out.end().k("replacements").arr();
        for (auto& r : compareReplacement(w, c, rp)) out.raw(r.toJson());
        out.end().end();
        return jsonOk(out.str());
    }
    if (rq.method == "POST" && p == "/api/whatif") {
        WorkloadSpec w = workloadFromJson(body["workload"]);
        Config c;
        c.thrashingControl = false;
        applyConfig(c, body["config"]);
        std::vector<double> vals;
        for (auto& v : body["values"].a) vals.push_back(v.num());
        if (vals.empty() || vals.size() > 12) return jsonErr(400, "provide 1 to 12 values");
        auto pts = whatIf(w, c, body["variable"].str("processes"), vals);
        json::Writer out;
        out.obj().k("variable").v(body["variable"].str("processes")).k("points").arr();
        for (auto& pt : pts) out.obj().k("value").v(pt.value).k("result").raw(pt.r.toJson()).end();
        out.end().end();
        return jsonOk(out.str());
    }
    if (rq.method == "POST" && p == "/api/replacement-lab") {
        std::vector<int> refs = intList(body["refs"]);
        int frames = std::clamp(body["frames"].integer(3), 1, 64);
        if (refs.empty() || refs.size() > 500) return jsonErr(400, "provide 1 to 500 page references");
        json::Writer out;
        out.obj().k("refs").ints(refs).k("frames").v(frames).k("results").arr();
        for (auto pol : {ReplPolicy::FIFO, ReplPolicy::LRU, ReplPolicy::CLOCK, ReplPolicy::OPTIMAL}) {
            auto r = simulateReplacement(refs, frames, pol);
            out.obj().k("policy").v(str(pol)).k("faults").v(r.faults).k("hits").v(r.hits).k("steps").arr();
            for (auto& s : r.steps) out.obj().k("ref").v(s.ref).k("fault").v(s.fault).k("victim").v(s.victim).k("frames").ints(s.frames).end();
            out.end().end();
        }
        out.end().end();
        return jsonOk(out.str());
    }
    if (rq.method == "POST" && p == "/api/banker") {
        Banker b;
        b.available = intList(body["available"]);
        b.maxDemand = matrix(body["max"]);
        b.allocation = matrix(body["allocation"]);
        if (!b.wellFormed() || b.processes() == 0 || b.resources() == 0) return jsonErr(400, "matrices must be consistent and allocation <= max");
        json::Writer out;
        std::vector<int> seq;
        bool safe = b.isSafe(seq);
        out.obj().k("safe").v(safe).k("sequence").ints(seq).k("need").arr();
        for (int i = 0; i < b.processes(); ++i) out.ints(b.need(i));
        out.end();
        if (body.has("request")) {
            std::string why;
            int who = body["request"]["process"].integer();
            if (who < 0 || who >= b.processes()) return jsonErr(400, "request.process out of range");
            std::vector<int> amt = intList(body["request"]["amounts"]);
            if ((int)amt.size() != b.resources()) return jsonErr(400, "request.amounts has the wrong length");
            bool granted = b.request(who, amt, why);
            out.k("request").obj().k("granted").v(granted).k("reason").v(why).end();
        }
        out.end();
        return jsonOk(out.str());
    }

    // ---- OS detective ----
    if (p == "/api/detective" && rq.method == "GET") { std::lock_guard<std::mutex> l(detectiveMutex); return jsonOk(detectiveListJson()); }
    if (p.rfind("/api/detective/", 0) == 0) {
        std::string rest = p.substr(15);
        bool diag = rest.size() > 9 && rest.substr(rest.size() - 9) == "/diagnose";
        std::string num = diag ? rest.substr(0, rest.size() - 9) : rest;
        if (num.empty() || !std::all_of(num.begin(), num.end(), ::isdigit)) return jsonErr(404, "not found");
        int id = std::atoi(num.c_str());
        if (id < 1 || id > detectiveCaseCount()) return jsonErr(404, "no such case");
        std::lock_guard<std::mutex> l(detectiveMutex);
        if (diag && rq.method == "POST") return jsonOk(detectiveDiagnose(id, body["answer"].str()));
        if (!diag && rq.method == "GET") return jsonOk(detectiveCaseJson(id));
    }
    return jsonErr(404, "not found");
}

// ------------------------------------------------------------------------------------------------
static std::string mimeFor(const std::string& path) {
    static const std::map<std::string, std::string> m{{".html", "text/html; charset=utf-8"}, {".js", "text/javascript"}, {".css", "text/css"}, {".svg", "image/svg+xml"},
        {".json", "application/json"}, {".png", "image/png"}, {".ico", "image/x-icon"}, {".woff2", "font/woff2"}, {".map", "application/json"}};
    auto it = m.find(fs::path(path).extension().string());
    return it == m.end() ? "application/octet-stream" : it->second;
}

static Response serveStatic(const std::string& root, std::string path) {
    if (path == "/") path = "/index.html";
    if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos) return jsonErr(400, "bad path");
    fs::path f = fs::path(root) / path.substr(1);
    std::error_code ec;
    if (!fs::is_regular_file(f, ec)) f = fs::path(root) / "index.html";      // single-page-app fallback
    if (!fs::is_regular_file(f, ec)) {
        Response r; r.status = 404; r.type = "text/plain";
        r.body = "Dashboard not built. Run: cd dashboard/frontend && npm install && npm run build   (or use the API directly)\n";
        return r;
    }
    Response r;
    r.type = mimeFor(f.string());
    try { r.body = Simulation::readFile(f.string()); } catch (...) { return jsonErr(500, "read error"); }
    return r;
}

static std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
        else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}

static bool readRequest(socket_t c, Request& rq) {
    std::string data;
    char buf[8192];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        int n = (int)recv(c, buf, sizeof buf, 0);
        if (n <= 0) return false;
        data.append(buf, (size_t)n);
        headerEnd = data.find("\r\n\r\n");
        if (data.size() > (1u << 20)) return false;
    }
    std::string head = data.substr(0, headerEnd);
    std::string rest = data.substr(headerEnd + 4);
    std::istringstream hs(head);
    std::string line, target, version;
    std::getline(hs, line);
    std::istringstream rl(line);
    rl >> rq.method >> target >> version;
    size_t len = 0;
    while (std::getline(hs, line)) {
        std::string low;
        for (char ch : line) low += (char)std::tolower((unsigned char)ch);
        if (low.rfind("content-length:", 0) == 0) len = (size_t)std::atol(line.c_str() + 15);
    }
    if (len > (1u << 20)) return false;
    while (rest.size() < len) {
        int n = (int)recv(c, buf, sizeof buf, 0);
        if (n <= 0) return false;
        rest.append(buf, (size_t)n);
    }
    rq.body = rest.substr(0, len);
    auto q = target.find('?');
    rq.path = urlDecode(target.substr(0, q));
    if (q != std::string::npos) {
        rq.query = target.substr(q + 1);
        std::istringstream qs(rq.query);
        std::string kv;
        while (std::getline(qs, kv, '&')) {
            auto eq = kv.find('=');
            if (eq != std::string::npos) rq.params[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
        }
    }
    return !rq.method.empty();
}

static void sendAll(socket_t c, const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        int n = (int)send(c, s.data() + off, (int)std::min<size_t>(s.size() - off, 1 << 16), 0);
        if (n <= 0) return;
        off += (size_t)n;
    }
}

static void handle(socket_t c, Engine& engine, const std::string& staticDir) {
#ifndef _WIN32
    timeval tv{5, 0};
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
#endif
    Request rq;
    Response rs;
    if (!readRequest(c, rq)) { closeSock(c); return; }
    try {
        if (rq.method == "OPTIONS") { rs.status = 204; rs.body.clear(); }
        else if (rq.path.rfind("/api/", 0) == 0) rs = route(engine, rq);
        else if (rq.method == "GET") rs = serveStatic(staticDir, rq.path);
        else rs = jsonErr(405, "method not allowed");
    } catch (const std::exception& ex) {
        rs = jsonErr(400, ex.what());
    }
    static const std::map<int, const char*> reason{{200, "OK"}, {204, "No Content"}, {400, "Bad Request"}, {404, "Not Found"}, {405, "Method Not Allowed"}, {409, "Conflict"}, {500, "Internal Server Error"}};
    std::string out = "HTTP/1.1 " + std::to_string(rs.status) + " " + (reason.count(rs.status) ? reason.at(rs.status) : "OK") + "\r\n" +
                      "Content-Type: " + rs.type + "\r\nContent-Length: " + std::to_string(rs.body.size()) + "\r\n" +
                      "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Content-Type\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\n" +
                      "Cache-Control: no-store\r\nConnection: close\r\n\r\n" + rs.body;
    sendAll(c, out);
    closeSock(c);
}

int main(int argc, char** argv) {
    int port = 8080;
    std::string staticDir = "dashboard/frontend/dist", scenarioDir = "scenarios";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--static" && i + 1 < argc) staticDir = argv[++i];
        else if (a == "--scenarios" && i + 1 < argc) scenarioDir = argv[++i];
        else if (a == "--help") { std::puts("owr_server [--port 8080] [--static dir] [--scenarios dir]"); return 0; }
    }
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    socket_t srv = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);     // local use only: this is a teaching simulator, not a public service
    addr.sin_port = htons((uint16_t)port);
    if (bind(srv, (sockaddr*)&addr, sizeof addr) != 0 || listen(srv, 64) != 0) { std::fprintf(stderr, "cannot listen on port %d\n", port); return 1; }

    Engine engine(scenarioDir);
    std::printf("OS War Room API listening on http://127.0.0.1:%d  (dashboard: %s, scenarios: %s)\n", port, staticDir.c_str(), scenarioDir.c_str());
    std::fflush(stdout);
    for (;;) {
        socket_t c = accept(srv, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        std::thread([c, &engine, staticDir] { handle(c, engine, staticDir); }).detach();
    }
}
