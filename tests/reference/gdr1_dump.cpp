// Reads a GDR1 macro (msgpack or JSON) with the official maxnut/GDReplayFormat
// gdr1 library and dumps what it reads, so the test suite can compare our
// parser against the reference. Optionally re-exports it as GDR1 JSON.
//
// usage: gdr1_dump <in.gdr> <out.expected> [out.json]

#include <gdr/gdr.hpp>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

struct Ref : gdr::Replay<Ref, gdr::Input> {
    Ref() : Replay("ref", "1") {}
};

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <in.gdr> <out.expected> [out.json]\n", argv[0]); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Ref r;
    try {
        r = Ref::importData(data);
    } catch (std::exception const& e) {
        std::fprintf(stderr, "reference import failed: %s\n", e.what());
        return 1;
    }
    std::ofstream out(argv[2]);
    out << "framerate " << r.framerate << "\n";
    out << "platformer -1\n"; // GDR1 has no platformer field
    out << "bot " << r.botInfo.name << "\n";
    out << "count " << r.inputs.size() << "\n";
    for (auto const& i : r.inputs)
        out << i.frame << " " << i.button << " " << (i.player2 ? 1 : 0) << " " << (i.down ? 1 : 0) << "\n";

    if (argc >= 4) {
        auto json = r.exportData(true);
        std::ofstream j(argv[3], std::ios::binary);
        j.write(reinterpret_cast<char const*>(json.data()), static_cast<std::streamsize>(json.size()));
    }
    return 0;
}
