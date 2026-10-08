// TargetLines for Ashita v4 - diagnostics logging.
//
// Messages go to Ashita's log manager (logs/ folder) and, for the bounded
// runtime diagnostics the Windower addon kept, to config/targetlines/runtime.log
// which rotates to runtime.log.1 at 5 MiB.

#ifndef TARGETLINES_LOGGER_HPP_INCLUDED
#define TARGETLINES_LOGGER_HPP_INCLUDED

#include <cstdarg>
#include <cstdint>
#include <string>

struct ILogManager;

namespace targetlines
{
    class Logger
    {
    public:
        void initialize(ILogManager* manager, const std::string& runtime_log_path);

        // Ashita log (Info level) and runtime.log.
        void info(const char* format, ...);
        // Ashita log (Warn level) and runtime.log.
        void warn(const char* format, ...);
        // runtime.log only.
        void runtime(const char* format, ...);

        const std::string& runtime_log_path(void) const
        {
            return runtime_log_path_;
        }

    private:
        void write(uint32_t level, const char* format, va_list args);
        void append_runtime(const char* message);
        void rotate_if_needed(size_t incoming_size);

        ILogManager* manager_ = nullptr;
        std::string runtime_log_path_;
    };
} // namespace targetlines

#endif // TARGETLINES_LOGGER_HPP_INCLUDED
