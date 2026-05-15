#include "trace_format.hpp"

#include <cstdlib>
#include <sstream>
#include <string>

namespace rawaccel_agent {

namespace {

bool is_blank_or_comment(const std::string& line)
{
    for (char c : line) {
        if (c == '#') return true;
        if (c != ' ' && c != '\t' && c != '\r') return false;
    }
    return true;
}

} // namespace

bool read_trace(std::istream& in, std::vector<TraceRecord>& out)
{
    out.clear();
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (is_blank_or_comment(line)) continue;
        TraceRecord r{};
        char delim1, delim2;
        std::istringstream iss(line);
        if (!(iss >> r.tick_us >> delim1 >> r.dx >> delim2 >> r.dy)) {
            return false;
        }
        if (delim1 != ',' || delim2 != ',') return false;
        out.push_back(r);
    }
    return true;
}

void write_trace(std::ostream& out, const std::vector<TraceRecord>& records,
                 const std::string& comment)
{
    if (!comment.empty()) {
        // Ensure each comment line is prefixed with '#'.
        std::istringstream iss(comment);
        std::string line;
        while (std::getline(iss, line)) {
            out << "# " << line << '\n';
        }
    }
    out << "# tick_us,dx,dy\n";
    for (const auto& r : records) {
        out << r.tick_us << ',' << r.dx << ',' << r.dy << '\n';
    }
}

} // namespace rawaccel_agent
