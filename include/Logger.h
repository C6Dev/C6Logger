#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace C6Logger {
	enum class LogLevel {
		trace,
		debug,
		info,
		warning,
		error,
		critical
	};

	void Log(LogLevel level, const std::string& message, const std::string& messenger);
}