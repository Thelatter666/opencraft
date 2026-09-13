#pragma once

#include <memory>
#include <mutex>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace opencraft::core::log {

// Installs the process-wide "opencraft" default logger (colored stdout,
// timestamped pattern) at the given runtime level. Idempotent: subsequent
// calls are no-ops, so any subsystem may call it defensively before logging.
inline void init(spdlog::level::level_enum level = spdlog::level::info) {
    static std::once_flag once;
    std::call_once(once, [level] {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("opencraft", std::move(sink));
        logger->set_level(level);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::set_default_logger(std::move(logger));
    });
}

// Set the runtime filter level of the default logger (no-op before init).
inline void set_level(spdlog::level::level_enum level) {
    spdlog::default_logger()->set_level(level);
}

} // namespace opencraft::core::log

// OC_LOG_* route through spdlog's default logger, so runtime filtering via
// log::init/set_level applies. Trace/debug are additionally compiled out
// unless SPDLOG_ACTIVE_LEVEL is raised to match (spdlog convention).
#define OC_LOG_TRACE(...) SPDLOG_LOGGER_TRACE(spdlog::default_logger(), __VA_ARGS__)
#define OC_LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(spdlog::default_logger(), __VA_ARGS__)
#define OC_LOG_INFO(...) SPDLOG_LOGGER_INFO(spdlog::default_logger(), __VA_ARGS__)
#define OC_LOG_WARN(...) SPDLOG_LOGGER_WARN(spdlog::default_logger(), __VA_ARGS__)
#define OC_LOG_ERROR(...) SPDLOG_LOGGER_ERROR(spdlog::default_logger(), __VA_ARGS__)
#define OC_LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(spdlog::default_logger(), __VA_ARGS__)
