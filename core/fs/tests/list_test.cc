#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "arc/fs/walk.hh"
#include "arc/result.hh"

using namespace arc::fs;

static std::string makeTempDir() {
    char tmpl[] = "/tmp/arc-list-XXXXXX";
    char* p = mkdtemp(tmpl);
    assert(p != nullptr);
    return p;
}

static void writeFile(const std::string& path) {
    std::ofstream out(path);
    assert(out.good());
    out << "x";
    assert(out.good());
}

static void testListDirOrdersDirsThenFilesAndSkipsHidden() {
    const std::string dir = makeTempDir();
    assert(mkdir((dir + "/sub").c_str(), 0755) == 0);
    writeFile(dir + "/a.md");
    writeFile(dir + "/Zebra.md");
    writeFile(dir + "/.hidden");

    arc::Result<std::vector<Entry>> listed = listDir(dir);
    assert(listed.ok());
    const std::vector<Entry>& entries = listed.value();
    assert(entries.size() == 3);
    assert(entries[0].isDir);
    assert(entries[0].path.find("sub") != std::string::npos);
    assert(!entries[1].isDir);
    assert(entries[1].path.find("a.md") != std::string::npos);
    assert(entries[2].path.find("Zebra.md") != std::string::npos);
    for (const Entry& e : entries) assert(e.path.find(".hidden") == std::string::npos);

    arc::Result<std::vector<Entry>> notDir = listDir(dir + "/a.md");
    assert(!notDir.ok());
    assert(notDir.error().kind == arc::ErrorKind::Io);
    assert(!notDir.error().message.empty());
}

int main() {
    testListDirOrdersDirsThenFilesAndSkipsHidden();
    std::printf("list_test: all assertions passed\n");
    return 0;
}
