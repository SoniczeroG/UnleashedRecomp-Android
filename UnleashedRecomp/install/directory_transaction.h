#pragma once

#include <filesystem>
#include <string>

// The backup lives on the same filesystem as the destination. A restart can
// recover the window between the two renames without touching the old files.
namespace DirectoryTransaction
{
    inline bool recover(const std::filesystem::path &destination,
        const std::filesystem::path &backup, std::string &error)
    {
        std::error_code ec;
        if (!std::filesystem::exists(backup, ec))
        {
            if (ec) error = ec.message();
            return !ec;
        }
        const bool committed = std::filesystem::exists(destination, ec);
        if (!ec)
        {
            if (committed) std::filesystem::remove_all(backup, ec);
            else std::filesystem::rename(backup, destination, ec);
        }
        if (ec) error = "Cannot recover " + destination.string() + ": " + ec.message();
        return !ec;
    }

    inline bool replace(const std::filesystem::path &prepared,
        const std::filesystem::path &destination, const std::filesystem::path &backup,
        std::string &error)
    {
        if (!recover(destination, backup, error)) return false;
        std::error_code ec;
        const bool hadPrevious = std::filesystem::exists(destination, ec);
        if (!ec && hadPrevious) std::filesystem::rename(destination, backup, ec);
        if (ec)
        {
            error = "Cannot back up " + destination.string() + ": " + ec.message();
            return false;
        }
        std::filesystem::rename(prepared, destination, ec);
        if (ec)
        {
            error = "Cannot install " + destination.string() + ": " + ec.message();
            if (hadPrevious)
            {
                std::error_code restoreEc;
                std::filesystem::rename(backup, destination, restoreEc);
                if (restoreEc) error += "; previous files retained at " + backup.string();
            }
            return false;
        }
        // A leftover backup is safe: recover() will clean it on the next launch.
        if (hadPrevious) std::filesystem::remove_all(backup, ec);
        return true;
    }
}
