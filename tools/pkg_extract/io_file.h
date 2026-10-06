// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the part of shadPS4's Common::FS::IOFile that the package extractor uses, on stdio
// with 64-bit offsets and wide (Unicode) paths on Windows.
#pragma once

#include <cstdio>
#include <filesystem>
#include <type_traits>

namespace Common::FS {

enum class FileAccessMode { Read, Write };
enum class SeekOrigin { SetOrigin, CurrentPosition, End };

class IOFile {
public:
    IOFile() = default;
    IOFile(const std::filesystem::path& path, FileAccessMode mode) {
        Open(path, mode);
    }
    ~IOFile() {
        Close();
    }
    IOFile(const IOFile&) = delete;
    IOFile& operator=(const IOFile&) = delete;

    bool Open(const std::filesystem::path& path, FileAccessMode mode) {
        Close();
#ifdef _WIN32
        file = _wfopen(path.c_str(), mode == FileAccessMode::Read ? L"rb" : L"wb");
#else
        file = std::fopen(path.c_str(), mode == FileAccessMode::Read ? "rb" : "wb");
#endif
        if (file) {
            std::setvbuf(file, nullptr, _IOFBF, 1 << 20);
        }
        return file != nullptr;
    }
    void Close() {
        if (file) {
            std::fclose(file);
            file = nullptr;
        }
    }
    bool IsOpen() const {
        return file != nullptr;
    }
    unsigned long long GetSize() {
        const long long at = Tell();
        Seek(0, SeekOrigin::End);
        const long long size = Tell();
        Seek(at);
        return static_cast<unsigned long long>(size);
    }
    long long Tell() {
#ifdef _WIN32
        return _ftelli64(file);
#else
        return ftello(file);
#endif
    }
    bool Seek(long long offset, SeekOrigin origin = SeekOrigin::SetOrigin) {
        const int whence = origin == SeekOrigin::SetOrigin ? SEEK_SET
                           : origin == SeekOrigin::CurrentPosition ? SEEK_CUR
                                                                  : SEEK_END;
#ifdef _WIN32
        return _fseeki64(file, offset, whence) == 0;
#else
        return fseeko(file, offset, whence) == 0;
#endif
    }
    template <typename T>
    std::size_t ReadRaw(void* data, std::size_t count) {
        return std::fread(data, sizeof(T), count, file);
    }
    template <typename T>
    std::size_t WriteRaw(const void* data, std::size_t count) {
        return std::fwrite(data, sizeof(T), count, file);
    }
    // A value, or the elements of a contiguous container (std::array, std::vector).
    template <typename T>
    bool Read(T& value) {
        if constexpr (requires { value.data(); value.size(); }) {
            const std::size_t bytes = value.size() * sizeof(*value.data());
            return std::fread(value.data(), 1, bytes, file) == bytes;
        } else {
            static_assert(std::is_trivially_copyable_v<T>);
            return std::fread(&value, 1, sizeof(T), file) == sizeof(T);
        }
    }
    template <typename T>
    bool Write(const T& value) {
        if constexpr (requires { value.data(); value.size(); }) {
            const std::size_t bytes = value.size() * sizeof(*value.data());
            return std::fwrite(value.data(), 1, bytes, file) == bytes;
        } else {
            return std::fwrite(&value, 1, sizeof(T), file) == sizeof(T);
        }
    }

private:
    std::FILE* file = nullptr;
};

} // namespace Common::FS
