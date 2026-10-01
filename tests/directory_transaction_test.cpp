#include <cassert>
#include <fstream>
#include <iostream>
#include "../UnleashedRecomp/install/directory_transaction.h"

static void write(const std::filesystem::path &dir, const char *value)
{
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "DLC.xml") << value;
}

static std::string read(const std::filesystem::path &dir)
{
    std::string value;
    std::ifstream(dir / "DLC.xml") >> value;
    return value;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const std::filesystem::path root(argv[1]);
    assert(!std::filesystem::exists(root));
    std::filesystem::create_directories(root);
    const auto target = root / "installed";
    const auto prepared = root / "prepared";
    const auto backup = root / ".previous";
    std::string error;
    write(target, "old");
    write(prepared, "new");
    assert(DirectoryTransaction::replace(prepared, target, backup, error));
    assert(read(target) == "new" && !std::filesystem::exists(backup));
    // Publication fails after moving the old pack aside: it must be restored.
    assert(!DirectoryTransaction::replace(root / "missing", target, backup, error));
    assert(read(target) == "new" && !std::filesystem::exists(backup));
    // Simulate process death between the two renames.
    std::filesystem::rename(target, backup);
    assert(DirectoryTransaction::recover(target, backup, error));
    assert(read(target) == "new" && !std::filesystem::exists(backup));
    // Simulate process death after publication, before backup cleanup.
    write(backup, "old");
    assert(DirectoryTransaction::recover(target, backup, error));
    assert(read(target) == "new" && !std::filesystem::exists(backup));
    std::cout << "DLC replacement, failed-publication rollback and restart recovery: PASS\n";
}
