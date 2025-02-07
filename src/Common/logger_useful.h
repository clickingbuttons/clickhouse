#pragma once

/// Macros for convenient usage of Poco logger.

#include <fmt/format.h>
#include <Poco/Logger.h>
#include <Poco/Message.h>
#include <Common/CurrentThread.h>

/// This wrapper is useful to save formatted message into a String before sending it to a logger
class LogToStrImpl
{
    String & out_str;
    Poco::Logger * logger;
    bool propagate_to_actual_log = true;
public:
    LogToStrImpl(String & out_str_, Poco::Logger * logger_) : out_str(out_str_) , logger(logger_) {}
    LogToStrImpl & operator -> () { return *this; }
    bool is(Poco::Message::Priority priority) { propagate_to_actual_log &= logger->is(priority); return true; }
    LogToStrImpl * getChannel() {return this; }
    const String & name() const { return logger->name(); }
    void log(const Poco::Message & message)
    {
        out_str = message.getText();
        if (!propagate_to_actual_log)
            return;
        if (auto * channel = logger->getChannel())
            channel->log(message);
    }
};

#define LogToStr(x, y) std::make_unique<LogToStrImpl>(x, y)

namespace
{
    template <typename... Ts> constexpr size_t numArgs(Ts &&...) { return sizeof...(Ts); }
    template <typename T, typename... Ts> constexpr auto firstArg(T && x, Ts &&...) { return std::forward<T>(x); }
    /// For implicit conversion of fmt::basic_runtime<> to char* for std::string ctor
    template <typename T, typename... Ts> constexpr auto firstArg(fmt::basic_runtime<T> && data, Ts &&...) { return data.str.data(); }

    [[maybe_unused]] const ::Poco::Logger * getLogger(const ::Poco::Logger * logger) { return logger; };
    [[maybe_unused]] const ::Poco::Logger * getLogger(const std::atomic<::Poco::Logger *> & logger) { return logger.load(); };
    [[maybe_unused]] std::unique_ptr<LogToStrImpl> getLogger(std::unique_ptr<LogToStrImpl> && logger) { return logger; };

    template<typename T> struct is_fmt_runtime : std::false_type {};
    template<typename T> struct is_fmt_runtime<fmt::basic_runtime<T>> : std::true_type {};

    /// Usually we use LOG_*(...) macros with either string literals or fmt::runtime(whatever) as a format string.
    /// This function is useful to get a string_view to a static format string passed to LOG_* macro.
    template <typename T> constexpr std::string_view tryGetStaticFormatString(T && x)
    {
        if constexpr (is_fmt_runtime<T>::value)
        {
            /// It definitely was fmt::runtime(something).
            /// We are not sure about a lifetime of the string, so return empty view.
            /// Also it can be arbitrary string, not a formatting pattern.
            /// So returning empty pattern will not pollute the set of patterns.
            return std::string_view();
        }
        else
        {
            /// Most likely it was a string literal.
            /// Unfortunately, there's no good way to check if something is a string literal.
            /// But fmtlib requires a format string to be compile-time constant unless fmt::runtime is used.
            static_assert(std::is_nothrow_convertible<T, const char * const>::value);
            static_assert(!std::is_pointer<T>::value);
            return std::string_view(x);
        }
    }
}

#define LOG_IMPL_FIRST_ARG(X, ...) X

/// Logs a message to a specified logger with that level.
/// If more than one argument is provided,
///  the first argument is interpreted as template with {}-substitutions
///  and the latter arguments treat as values to substitute.
/// If only one argument is provided, it is threat as message without substitutions.

#define LOG_IMPL(logger, priority, PRIORITY, ...) do                              \
{                                                                                 \
    auto _logger = ::getLogger(logger);                                           \
    const bool _is_clients_log = (DB::CurrentThread::getGroup() != nullptr) &&    \
        (DB::CurrentThread::getGroup()->client_logs_level >= (priority));         \
    if (_is_clients_log || _logger->is((PRIORITY)))                               \
    {                                                                             \
        std::string formatted_message = numArgs(__VA_ARGS__) > 1 ? fmt::format(__VA_ARGS__) : firstArg(__VA_ARGS__); \
        if (auto _channel = _logger->getChannel())                                \
        {                                                                         \
            std::string file_function;                                            \
            file_function += __FILE__;                                            \
            file_function += "; ";                                                \
            file_function += __PRETTY_FUNCTION__;                                 \
            Poco::Message poco_message(_logger->name(), formatted_message,        \
                (PRIORITY), file_function.c_str(), __LINE__, tryGetStaticFormatString(LOG_IMPL_FIRST_ARG(__VA_ARGS__)));    \
            _channel->log(poco_message);                                          \
        }                                                                         \
    }                                                                             \
} while (false)


#define LOG_TEST(logger, ...)    LOG_IMPL(logger, DB::LogsLevel::test, Poco::Message::PRIO_TEST, __VA_ARGS__)
#define LOG_TRACE(logger, ...)   LOG_IMPL(logger, DB::LogsLevel::trace, Poco::Message::PRIO_TRACE, __VA_ARGS__)
#define LOG_DEBUG(logger, ...)   LOG_IMPL(logger, DB::LogsLevel::debug, Poco::Message::PRIO_DEBUG, __VA_ARGS__)
#define LOG_INFO(logger, ...)    LOG_IMPL(logger, DB::LogsLevel::information, Poco::Message::PRIO_INFORMATION, __VA_ARGS__)
#define LOG_WARNING(logger, ...) LOG_IMPL(logger, DB::LogsLevel::warning, Poco::Message::PRIO_WARNING, __VA_ARGS__)
#define LOG_ERROR(logger, ...)   LOG_IMPL(logger, DB::LogsLevel::error, Poco::Message::PRIO_ERROR, __VA_ARGS__)
#define LOG_FATAL(logger, ...)   LOG_IMPL(logger, DB::LogsLevel::error, Poco::Message::PRIO_FATAL, __VA_ARGS__)
