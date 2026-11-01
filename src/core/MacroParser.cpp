#include "MacroParser.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <utility>

namespace mp {

namespace {

constexpr int kMaxDepth = 64;
constexpr size_t kMaxInputs = 50'000'000;

struct Fail {
    std::string message;
};

[[noreturn]] void fail(std::string msg) { throw Fail{std::move(msg)}; }

// ---------------------------------------------------------------------------
// Generic value tree for GDR1 (msgpack / JSON)
// ---------------------------------------------------------------------------

struct Value {
    enum class Kind { Null, Bool, Int, UInt, Double, String, Array, Map, Binary };
    Kind kind = Kind::Null;
    bool b = false;
    int64_t i = 0;
    uint64_t u = 0;
    double d = 0.0;
    std::string s;
    std::vector<Value> arr;      // array elements, or map values
    std::vector<std::string> keys; // map keys (parallel to arr)

    Value const* get(std::string_view key) const {
        if (kind != Kind::Map) return nullptr;
        for (size_t k = 0; k < keys.size(); k++)
            if (keys[k] == key) return &arr[k];
        return nullptr;
    }
    bool isNumber() const { return kind == Kind::Int || kind == Kind::UInt || kind == Kind::Double; }
    double num() const {
        switch (kind) {
            case Kind::Int: return static_cast<double>(i);
            case Kind::UInt: return static_cast<double>(u);
            case Kind::Double: return d;
            case Kind::Bool: return b ? 1.0 : 0.0;
            default: return 0.0;
        }
    }
};

// ---------------------------------------------------------------------------
// msgpack
// ---------------------------------------------------------------------------

class MsgpackReader {
public:
    explicit MsgpackReader(std::span<const uint8_t> d) : m_data(d) {}

    Value read(int depth = 0) {
        if (depth > kMaxDepth) fail("msgpack nesting too deep");
        uint8_t t = byte();
        Value v;
        if (t <= 0x7f) { v.kind = Value::Kind::UInt; v.u = t; return v; }
        if (t >= 0xe0) { v.kind = Value::Kind::Int; v.i = static_cast<int8_t>(t); return v; }
        if ((t & 0xf0) == 0x80) return readMap(t & 0x0f, depth);
        if ((t & 0xf0) == 0x90) return readArray(t & 0x0f, depth);
        if ((t & 0xe0) == 0xa0) return readStr(t & 0x1f);
        switch (t) {
            case 0xc0: return v;
            case 0xc2: v.kind = Value::Kind::Bool; v.b = false; return v;
            case 0xc3: v.kind = Value::Kind::Bool; v.b = true; return v;
            case 0xc4: return readBin(be(1));
            case 0xc5: return readBin(be(2));
            case 0xc6: return readBin(be(4));
            case 0xca: {
                uint32_t bits = static_cast<uint32_t>(be(4));
                float f; std::memcpy(&f, &bits, 4);
                v.kind = Value::Kind::Double; v.d = f; return v;
            }
            case 0xcb: {
                uint64_t bits = be(8);
                double f; std::memcpy(&f, &bits, 8);
                v.kind = Value::Kind::Double; v.d = f; return v;
            }
            case 0xcc: v.kind = Value::Kind::UInt; v.u = be(1); return v;
            case 0xcd: v.kind = Value::Kind::UInt; v.u = be(2); return v;
            case 0xce: v.kind = Value::Kind::UInt; v.u = be(4); return v;
            case 0xcf: v.kind = Value::Kind::UInt; v.u = be(8); return v;
            case 0xd0: v.kind = Value::Kind::Int; v.i = static_cast<int8_t>(be(1)); return v;
            case 0xd1: v.kind = Value::Kind::Int; v.i = static_cast<int16_t>(be(2)); return v;
            case 0xd2: v.kind = Value::Kind::Int; v.i = static_cast<int32_t>(be(4)); return v;
            case 0xd3: v.kind = Value::Kind::Int; v.i = static_cast<int64_t>(be(8)); return v;
            case 0xd9: return readStr(be(1));
            case 0xda: return readStr(be(2));
            case 0xdb: return readStr(be(4));
            case 0xdc: return readArray(be(2), depth);
            case 0xdd: return readArray(be(4), depth);
            case 0xde: return readMap(be(2), depth);
            case 0xdf: return readMap(be(4), depth);
            // fixext / ext: skip payload, value becomes null
            case 0xd4: skip(1 + 1); return v;
            case 0xd5: skip(1 + 2); return v;
            case 0xd6: skip(1 + 4); return v;
            case 0xd7: skip(1 + 8); return v;
            case 0xd8: skip(1 + 16); return v;
            case 0xc7: { auto n = be(1); skip(1 + n); return v; }
            case 0xc8: { auto n = be(2); skip(1 + n); return v; }
            case 0xc9: { auto n = be(4); skip(1 + n); return v; }
            default: fail("invalid msgpack byte 0x" + hex(t) + " at offset " + std::to_string(m_pos - 1));
        }
    }

    size_t remaining() const { return m_data.size() - m_pos; }

private:
    std::span<const uint8_t> m_data;
    size_t m_pos = 0;

    static std::string hex(uint8_t b) {
        char buf[3];
        static constexpr char digits[] = "0123456789abcdef";
        buf[0] = digits[b >> 4]; buf[1] = digits[b & 15]; buf[2] = 0;
        return buf;
    }
    void need(size_t n) {
        if (n > remaining()) fail("msgpack data is truncated");
    }
    uint8_t byte() { need(1); return m_data[m_pos++]; }
    uint64_t be(int n) {
        need(static_cast<size_t>(n));
        uint64_t r = 0;
        for (int k = 0; k < n; k++) r = (r << 8) | m_data[m_pos++];
        return r;
    }
    void skip(uint64_t n) { need(n); m_pos += n; }
    Value readStr(uint64_t n) {
        need(n);
        Value v; v.kind = Value::Kind::String;
        v.s.assign(reinterpret_cast<char const*>(m_data.data() + m_pos), n);
        m_pos += n;
        return v;
    }
    Value readBin(uint64_t n) {
        Value v = readStr(n);
        v.kind = Value::Kind::Binary;
        return v;
    }
    Value readArray(uint64_t n, int depth) {
        // every element needs at least one byte
        if (n > remaining()) fail("msgpack array length exceeds data");
        Value v; v.kind = Value::Kind::Array;
        v.arr.reserve(n);
        for (uint64_t k = 0; k < n; k++) v.arr.push_back(read(depth + 1));
        return v;
    }
    Value readMap(uint64_t n, int depth) {
        if (n > remaining() / 2) fail("msgpack map length exceeds data");
        Value v; v.kind = Value::Kind::Map;
        v.keys.reserve(n);
        v.arr.reserve(n);
        for (uint64_t k = 0; k < n; k++) {
            Value key = read(depth + 1);
            std::string ks;
            if (key.kind == Value::Kind::String) ks = std::move(key.s);
            else if (key.kind == Value::Kind::UInt) ks = std::to_string(key.u);
            else if (key.kind == Value::Kind::Int) ks = std::to_string(key.i);
            else fail("unsupported msgpack map key type");
            Value val = read(depth + 1);
            v.keys.push_back(std::move(ks));
            v.arr.push_back(std::move(val));
        }
        return v;
    }
};

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

class JsonReader {
public:
    explicit JsonReader(std::span<const uint8_t> d) : m_data(d) {}

    Value parseDocument() {
        Value v = parse(0);
        ws();
        if (m_pos != m_data.size()) fail("unexpected trailing data in JSON");
        return v;
    }

private:
    std::span<const uint8_t> m_data;
    size_t m_pos = 0;

    bool eof() const { return m_pos >= m_data.size(); }
    char peek() const { return eof() ? '\0' : static_cast<char>(m_data[m_pos]); }
    char next() {
        if (eof()) fail("JSON data is truncated");
        return static_cast<char>(m_data[m_pos++]);
    }
    void ws() {
        while (!eof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') m_pos++;
            else break;
        }
    }
    void expect(char const* lit) {
        for (char const* p = lit; *p; p++)
            if (next() != *p) fail(std::string("invalid JSON literal, expected ") + lit);
    }
    Value parse(int depth) {
        if (depth > kMaxDepth) fail("JSON nesting too deep");
        ws();
        char c = peek();
        Value v;
        switch (c) {
            case '{': {
                m_pos++;
                v.kind = Value::Kind::Map;
                ws();
                if (peek() == '}') { m_pos++; return v; }
                while (true) {
                    ws();
                    if (peek() != '"') fail("expected string key in JSON object");
                    std::string key = parseString();
                    ws();
                    if (next() != ':') fail("expected ':' in JSON object");
                    Value val = parse(depth + 1);
                    v.keys.push_back(std::move(key));
                    v.arr.push_back(std::move(val));
                    ws();
                    char d = next();
                    if (d == '}') break;
                    if (d != ',') fail("expected ',' or '}' in JSON object");
                }
                return v;
            }
            case '[': {
                m_pos++;
                v.kind = Value::Kind::Array;
                ws();
                if (peek() == ']') { m_pos++; return v; }
                while (true) {
                    v.arr.push_back(parse(depth + 1));
                    ws();
                    char d = next();
                    if (d == ']') break;
                    if (d != ',') fail("expected ',' or ']' in JSON array");
                }
                return v;
            }
            case '"':
                v.kind = Value::Kind::String;
                v.s = parseString();
                return v;
            case 't': expect("true"); v.kind = Value::Kind::Bool; v.b = true; return v;
            case 'f': expect("false"); v.kind = Value::Kind::Bool; v.b = false; return v;
            case 'n': expect("null"); return v;
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
                fail("unexpected character in JSON");
        }
    }
    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    uint32_t hex4() {
        uint32_t r = 0;
        for (int k = 0; k < 4; k++) {
            char c = next();
            r <<= 4;
            if (c >= '0' && c <= '9') r |= c - '0';
            else if (c >= 'a' && c <= 'f') r |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') r |= c - 'A' + 10;
            else fail("invalid \\u escape in JSON string");
        }
        return r;
    }
    std::string parseString() {
        next(); // opening quote
        std::string out;
        while (true) {
            char c = next();
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            char e = next();
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && peek() == '\\') {
                        size_t save = m_pos;
                        next();
                        if (next() == 'u') {
                            uint32_t lo = hex4();
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else m_pos = save;
                        } else m_pos = save;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("invalid escape in JSON string");
            }
        }
    }
    Value parseNumber() {
        size_t start = m_pos;
        bool isFloat = false;
        if (peek() == '-') m_pos++;
        while (!eof()) {
            char c = peek();
            if (c >= '0' && c <= '9') m_pos++;
            else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') { isFloat = true; m_pos++; }
            else break;
        }
        std::string text(reinterpret_cast<char const*>(m_data.data() + start), m_pos - start);
        Value v;
        try {
            if (!isFloat) {
                if (text[0] == '-') { v.kind = Value::Kind::Int; v.i = std::stoll(text); }
                else { v.kind = Value::Kind::UInt; v.u = std::stoull(text); }
            } else {
                v.kind = Value::Kind::Double;
                v.d = std::stod(text);
            }
        } catch (...) {
            fail("invalid JSON number '" + text + "'");
        }
        return v;
    }
};

// ---------------------------------------------------------------------------
// GDR1 (msgpack / JSON value tree -> Macro)
// ---------------------------------------------------------------------------

std::string toStr(Value const* v) {
    if (!v) return {};
    switch (v->kind) {
        case Value::Kind::String: return v->s;
        case Value::Kind::Int: return std::to_string(v->i);
        case Value::Kind::UInt: return std::to_string(v->u);
        case Value::Kind::Double: {
            std::string s = std::to_string(v->d);
            while (s.size() > 1 && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.pop_back();
            return s;
        }
        case Value::Kind::Bool: return v->b ? "true" : "false";
        default: return {};
    }
}

double toNum(Value const* v, double def) {
    if (!v || !(v->isNumber() || v->kind == Value::Kind::Bool)) return def;
    return v->num();
}

bool toBool(Value const* v, bool def) {
    if (!v) return def;
    if (v->kind == Value::Kind::Bool) return v->b;
    if (v->isNumber()) return v->num() != 0.0;
    return def;
}

uint64_t toFrame(Value const& v, size_t index) {
    auto bad = [&](char const* why) -> uint64_t {
        fail("input #" + std::to_string(index) + ": " + why);
    };
    switch (v.kind) {
        case Value::Kind::UInt: return v.u;
        case Value::Kind::Int:
            if (v.i < 0) return bad("negative frame");
            return static_cast<uint64_t>(v.i);
        case Value::Kind::Double:
            if (!std::isfinite(v.d) || v.d < 0) return bad("invalid frame");
            if (v.d != std::floor(v.d)) return bad("fractional frame number");
            return static_cast<uint64_t>(v.d);
        default: return bad("frame is not a number");
    }
}

Macro fromGdr1(Value const& root, Format fmt) {
    if (root.kind != Value::Kind::Map) fail("macro root is not an object");

    Macro m;
    m.format = fmt;
    m.formatVersion = static_cast<int>(toNum(root.get("version"), 1.0));
    m.author = toStr(root.get("author"));
    m.description = toStr(root.get("description"));
    m.duration = toNum(root.get("duration"), 0.0);
    m.gameVersion = toNum(root.get("gameVersion"), 0.0);
    m.framerate = toNum(root.get("framerate"), 240.0);
    m.ldm = toBool(root.get("ldm"), false);
    if (auto bot = root.get("bot")) {
        m.botName = toStr(bot->get("name"));
        m.botVersion = toStr(bot->get("version"));
    }
    if (auto lvl = root.get("level")) {
        m.levelName = toStr(lvl->get("name"));
        m.levelId = static_cast<uint32_t>(toNum(lvl->get("id"), 0.0));
    }

    auto inputs = root.get("inputs");
    if (!inputs || inputs->kind != Value::Kind::Array) fail("macro has no \"inputs\" array");
    if (inputs->arr.size() > kMaxInputs) fail("macro has too many inputs");

    bool anyPlatButton = false;
    m.inputs.reserve(inputs->arr.size());
    for (size_t k = 0; k < inputs->arr.size(); k++) {
        auto const& in = inputs->arr[k];
        if (in.kind != Value::Kind::Map) fail("input #" + std::to_string(k) + " is not an object");
        auto frame = in.get("frame");
        auto down = in.get("down");
        if (!frame) fail("input #" + std::to_string(k) + " has no frame");
        if (!down) fail("input #" + std::to_string(k) + " has no down/up state");

        Input i;
        i.frame = toFrame(*frame, k);
        i.down = toBool(down, false);
        i.player2 = toBool(in.get("2p"), false);
        double btn = toNum(in.get("btn"), 1.0);
        if (btn != 1.0 && btn != 2.0 && btn != 3.0)
            fail("input #" + std::to_string(k) + " has unknown button " + toStr(in.get("btn")));
        i.button = static_cast<uint8_t>(btn);
        if (i.button != 1) anyPlatButton = true;
        m.inputs.push_back(i);
    }
    m.platformer = root.get("platformer") ? toBool(root.get("platformer"), false) : anyPlatButton;
    return m;
}

// ---------------------------------------------------------------------------
// GDR2 binary (matches maxnut/GDReplayFormat gdr2 reference implementation)
// ---------------------------------------------------------------------------

class Gdr2Reader {
public:
    explicit Gdr2Reader(std::span<const uint8_t> d) : m_data(d) {}

    size_t remaining() const { return m_data.size() - m_pos; }

    uint64_t varint() {
        uint64_t result = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            if (m_pos >= m_data.size()) fail("GDR2 data is truncated (varint)");
            uint8_t b = m_data[m_pos++];
            if (shift < 64) result |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return result;
        }
        fail("GDR2 varint is too long");
    }
    // Floats/doubles are written as raw big-endian bytes by the reference writer.
    template <class T>
    T rawBE() {
        if (remaining() < sizeof(T)) fail("GDR2 data is truncated");
        uint8_t buf[sizeof(T)];
        for (size_t k = 0; k < sizeof(T); k++) buf[sizeof(T) - 1 - k] = m_data[m_pos + k];
        m_pos += sizeof(T);
        T v;
        std::memcpy(&v, buf, sizeof(T));
        return v;
    }
    std::string str() {
        uint64_t n = varint();
        if (n > remaining()) fail("GDR2 string length exceeds data");
        std::string s(reinterpret_cast<char const*>(m_data.data() + m_pos), n);
        m_pos += n;
        return s;
    }
    void skip(uint64_t n) {
        if (n > remaining()) fail("GDR2 extension size exceeds data");
        m_pos += n;
    }
    void magic() {
        if (remaining() < 3 || std::memcmp(m_data.data(), "GDR", 3) != 0) fail("missing GDR magic");
        m_pos = 3;
    }

private:
    std::span<const uint8_t> m_data;
    size_t m_pos = 0;
};

Macro parseGdr2(std::span<const uint8_t> data) {
    Gdr2Reader r(data);
    r.magic();

    Macro m;
    m.format = Format::GDR2;
    m.formatVersion = static_cast<int>(r.varint());
    std::string inputTag = r.str();
    m.author = r.str();
    m.description = r.str();
    m.duration = r.rawBE<float>();
    m.gameVersion = static_cast<double>(static_cast<int32_t>(r.varint()));
    m.framerate = r.rawBE<double>();
    (void)r.varint(); // seed
    (void)r.varint(); // coins
    m.ldm = r.varint() != 0;
    m.platformer = r.varint() != 0;
    m.botName = r.str();
    m.botVersion = std::to_string(static_cast<int32_t>(r.varint()));
    m.levelId = static_cast<uint32_t>(r.varint());
    m.levelName = r.str();

    r.skip(r.varint()); // replay extension block (bot specific)

    uint64_t deaths = r.varint();
    if (deaths > r.remaining()) fail("GDR2 death count exceeds data");
    for (uint64_t k = 0; k < deaths; k++) (void)r.varint();

    uint64_t count = r.varint();
    uint64_t p1Count = r.varint();
    if (count > kMaxInputs || count > r.remaining()) fail("GDR2 input count exceeds data");
    if (p1Count > count) fail("GDR2 player 1 input count exceeds total");

    bool hasInputExt = !inputTag.empty();
    m.inputs.reserve(count);
    uint64_t prev = 0;
    for (uint64_t k = 0; k < count; k++) {
        if (k == p1Count) prev = 0; // player 2 block restarts delta encoding
        uint64_t packed = r.varint();
        Input in;
        if (m.platformer) {
            in.down = packed & 1;
            in.button = static_cast<uint8_t>((packed >> 1) & 3);
            in.frame = prev + (packed >> 3);
        } else {
            in.down = packed & 1;
            in.button = 1;
            in.frame = prev + (packed >> 1);
        }
        if (in.frame < prev) fail("GDR2 frame overflow");
        in.player2 = k >= p1Count;
        if (in.button == 0) fail("GDR2 input #" + std::to_string(k) + " has no button");
        if (hasInputExt) r.skip(r.varint()); // per-input extension (physics etc.)
        prev = in.frame;
        m.inputs.push_back(in);
    }
    if (r.remaining() != 0) fail("GDR2 has " + std::to_string(r.remaining()) + " unexpected trailing bytes");

    // GDR2 stores all player 1 inputs, then all player 2 inputs. The format
    // defines the real sequence as a stable sort by frame (player 1 first on
    // ties), exactly like the reference reader does.
    std::stable_sort(m.inputs.begin(), m.inputs.end(),
        [](Input const& a, Input const& b) { return a.frame < b.frame; });
    return m;
}

void finalize(Macro& m) {
    if (!(m.framerate > 0.0) || !std::isfinite(m.framerate)) fail("macro framerate is invalid");
    if (m.inputs.empty()) fail("macro contains no inputs");

    m.hadToSort = !std::is_sorted(m.inputs.begin(), m.inputs.end(),
        [](Input const& a, Input const& b) { return a.frame < b.frame; });
    if (m.hadToSort) {
        std::stable_sort(m.inputs.begin(), m.inputs.end(),
            [](Input const& a, Input const& b) { return a.frame < b.frame; });
    }

    if (m.botName == "xdBot") {
        m.clock = FrameClock::XdBotLevelTime;
        m.playerFlag = PlayerFlag::XdBotRaw;
    }

    // Diagnostics only: the inputs are played exactly as stored.
    std::map<std::pair<bool, uint8_t>, bool> held;
    for (auto const& in : m.inputs) {
        bool& h = held[{in.player2, in.button}];
        if (in.down) {
            if (h) m.repeatedPresses++;
            h = true;
        } else {
            if (!h) m.unmatchedReleases++;
            h = false;
        }
    }
    for (auto const& [k, h] : held)
        if (h) m.unmatchedPresses++;
}

} // namespace

char const* formatName(Format f) {
    switch (f) {
        case Format::GDR2: return "GDR2";
        case Format::GDR1Msgpack: return "GDR1 (msgpack)";
        case Format::GDR1Json: return "GDR1 (JSON)";
    }
    return "?";
}

ParseResult parseMacro(std::span<const uint8_t> data) {
    try {
        if (data.empty()) fail("file is empty");
        Macro m;
        if (data.size() >= 3 && std::memcmp(data.data(), "GDR", 3) == 0) {
            m = parseGdr2(data);
        } else {
            size_t p = 0;
            // skip UTF-8 BOM / whitespace for JSON detection
            if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) p = 3;
            while (p < data.size() && (data[p] == ' ' || data[p] == '\n' || data[p] == '\r' || data[p] == '\t')) p++;
            if (p < data.size() && data[p] == '{') {
                JsonReader jr(data.subspan(p));
                m = fromGdr1(jr.parseDocument(), Format::GDR1Json);
            } else {
                MsgpackReader mr(data);
                Value root = mr.read();
                m = fromGdr1(root, Format::GDR1Msgpack);
            }
        }
        finalize(m);
        return m;
    } catch (Fail const& f) {
        return ParseError{f.message};
    } catch (std::bad_alloc const&) {
        return ParseError{"out of memory while reading macro"};
    }
}

ParseResult parseMacroFile(std::filesystem::path const& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return ParseError{"could not open file"};
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    if (size < 0) return ParseError{"could not read file size"};
    if (static_cast<uint64_t>(size) > 512ull * 1024 * 1024) return ParseError{"file is too large"};
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) return ParseError{"could not read file"};
    return parseMacro(data);
}

} // namespace mp
