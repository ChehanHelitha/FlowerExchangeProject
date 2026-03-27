#include "CSVWriter.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>

CSVWriter::CSVWriter(const std::string& outputPath) {
    outputFile_.open(outputPath, std::ios::out | std::ios::trunc);
    if (!outputFile_.is_open()) {
        throw std::runtime_error("Cannot open output file: " + outputPath);
    }
    writeHeader();
}

CSVWriter::~CSVWriter() {
    if (outputFile_.is_open()) {
        outputFile_.flush();
        outputFile_.close();
    }
}

void CSVWriter::writeReport(ExecutionReport report) {
    // Lock to ensure thread-safe writing. Currently called from a single
    // writer thread, but this protects against future scenarios where
    // multiple threads might write to the same file.
    std::lock_guard<std::mutex> lock(writeMutex_);
    outputFile_ << formatRow(report) << '\n';
    // No flush after each line as it slows things down too much.
    // We flush everything at once when the file closes.
}

void CSVWriter::writeHeader() {
    outputFile_ << "Order ID,Client Order ID,Instrument,Side,"
                   "Exec Status,Quantity,Price,Reason,Transaction Time\n";
}

std::string CSVWriter::formatRow(const ExecutionReport& report) {
    std::ostringstream oss;

    // Price formatted to 2 decimal places
    oss << std::fixed << std::setprecision(2);

    oss << report.orderId          << ','
        << report.clientOrderId    << ','
        << report.instrument       << ','
        << report.side             << ','
        << statusToString(report.status) << ','
        << report.quantity         << ','
        << report.price            << ','
        << report.reason           << ','
        << report.transactTime;

    return oss.str();
}

std::string CSVWriter::statusToString(ExecStatus status) {
    switch (status) {
        case ExecStatus::New:      return "New";
        case ExecStatus::Rejected: return "Reject";
        case ExecStatus::Fill:     return "Fill";
        case ExecStatus::PFill:    return "PFill";
        default:                   return "Unknown";
    }
}