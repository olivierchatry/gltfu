#ifndef PROGRESS_REPORTER_H
#define PROGRESS_REPORTER_H

#include <string>
#include <functional>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace gltfu {

/**
 * @brief Progress reporter for streaming progress updates
 * 
 * Supports two modes:
 * - Human-readable text output
 * - JSON streaming (one JSON object per line)
 */
class ProgressReporter {
public:
    enum class Format {
        Text,
        JSON
    };

    using ProgressCallback = std::function<void(const std::string&)>;

    ProgressReporter(Format format = Format::Text, std::ostream& out = std::cout)
        : format_(format), out_(out) {}

    void setFormat(Format format) { format_ = format; }
    
    /**
     * @brief Report a progress update
     * @param operation The operation being performed (e.g., "loading", "deduplicating")
     * @param message Human-readable message
     * @param progress Progress value (0.0 to 1.0, or -1 for indeterminate)
     * @param details Optional additional details
     */
    void report(const std::string& operation, 
                const std::string& message, 
                double progress = -1.0,
                const std::string& details = "") {
        if (format_ == Format::JSON) {
            reportJSON(operation, message, progress, details);
        } else {
            reportText(operation, message, progress, details);
        }
    }

    /**
     * @brief Report an error
     */
    void error(const std::string& operation, const std::string& message) {
        if (format_ == Format::JSON) {
            out_ << "{\"type\":\"error\",\"operation\":\"" << escapeJSON(operation) 
                 << "\",\"message\":\"" << escapeJSON(message) << "\"}" << std::endl;
        } else {
            out_ << "Error [" << operation << "]: " << message << std::endl;
        }
    }

    /**
     * @brief Report success
     */
    void success(const std::string& operation, const std::string& message) {
        if (format_ == Format::JSON) {
            out_ << "{\"type\":\"success\",\"operation\":\"" << escapeJSON(operation) 
                 << "\",\"message\":\"" << escapeJSON(message) << "\"}" << std::endl;
        } else {
            out_ << "✓ " << message << std::endl;
        }
    }

private:
    void reportJSON(const std::string& operation, 
                    const std::string& message,
                    double progress,
                    const std::string& details) {
        out_ << "{\"type\":\"progress\",\"operation\":\"" << escapeJSON(operation) << "\"";
        out_ << ",\"message\":\"" << escapeJSON(message) << "\"";
        
        if (progress >= 0.0) {
            out_ << ",\"progress\":" << std::fixed << std::setprecision(4) << progress;
        }
        
        if (!details.empty()) {
            out_ << ",\"details\":\"" << escapeJSON(details) << "\"";
        }
        
        out_ << "}" << std::endl;
    }

    void reportText(const std::string& operation,
                    const std::string& message,
                    double progress,
                    const std::string& details) {
        if (progress >= 0.0) {
            int percent = static_cast<int>(progress * 100);
            out_ << "[" << operation << "] " << message << " (" << percent << "%)";
        } else {
            out_ << "[" << operation << "] " << message;
        }
        
        if (!details.empty()) {
            out_ << " - " << details;
        }
        
        out_ << std::endl;
    }

    std::string escapeJSON(const std::string& str) {
        std::ostringstream ss;
        for (char c : str) {
            switch (c) {
                case '"':  ss << "\\\""; break;
                case '\\': ss << "\\\\"; break;
                case '\b': ss << "\\b"; break;
                case '\f': ss << "\\f"; break;
                case '\n': ss << "\\n"; break;
                case '\r': ss << "\\r"; break;
                case '\t': ss << "\\t"; break;
                default:
                    if (c < 0x20) {
                        ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    } else {
                        ss << c;
                    }
            }
        }
        return ss.str();
    }

    Format format_;
    std::ostream& out_;
};

} // namespace gltfu

#endif // PROGRESS_REPORTER_H
