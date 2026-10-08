#include "logger.hpp"

#include "Ashita.h"

#include <cstdio>
#include <ctime>

namespace targetlines
{
    namespace
    {
        constexpr unsigned long long kRuntimeLogMaxSize = 5ull * 1024ull * 1024ull;
    }

    void Logger::initialize(ILogManager* manager, const std::string& runtime_log_path)
    {
        manager_          = manager;
        runtime_log_path_ = runtime_log_path;
    }

    void Logger::info(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        write(static_cast<uint32_t>(Ashita::LogLevel::Info), format, args);
        va_end(args);
    }

    void Logger::warn(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        write(static_cast<uint32_t>(Ashita::LogLevel::Warn), format, args);
        va_end(args);
    }

    void Logger::runtime(const char* format, ...)
    {
        char buffer[2048] {};
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        append_runtime(buffer);
    }

    void Logger::write(uint32_t level, const char* format, va_list args)
    {
        char buffer[2048] {};
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        if (manager_ != nullptr)
        {
            manager_->Log(level, "TargetLines", buffer);
        }
        append_runtime(buffer);
    }

    void Logger::rotate_if_needed(size_t incoming_size)
    {
        WIN32_FILE_ATTRIBUTE_DATA attributes {};
        if (!GetFileAttributesExA(runtime_log_path_.c_str(), GetFileExInfoStandard, &attributes))
        {
            return;
        }

        const unsigned long long size = (static_cast<unsigned long long>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
        if (size + incoming_size < kRuntimeLogMaxSize)
        {
            return;
        }

        // Rotation is best-effort. Multibox clients share this file and a failed
        // move simply leaves the active log in place.
        const std::string backup = runtime_log_path_ + ".1";
        DeleteFileA(backup.c_str());
        MoveFileExA(runtime_log_path_.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    void Logger::append_runtime(const char* message)
    {
        if (runtime_log_path_.empty() || message == nullptr)
        {
            return;
        }

        char stamp[32] {};
        const std::time_t now = std::time(nullptr);
        std::tm local {};
        if (localtime_s(&local, &now) == 0)
        {
            std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
        }

        rotate_if_needed(std::strlen(message) + sizeof(stamp) + 2);

        FILE* file = nullptr;
        if (fopen_s(&file, runtime_log_path_.c_str(), "ab") != 0 || file == nullptr)
        {
            return;
        }
        std::fprintf(file, "%s %s\n", stamp, message);
        std::fclose(file);
    }
} // namespace targetlines
