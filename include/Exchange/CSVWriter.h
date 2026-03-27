#pragma once
#include "IWriter.h"
#include <fstream>
#include <string>
#include <mutex>

// CSVWriter implements the IWriter interface to persist execution reports
// as CSV files. By inheriting from IWriter, it allows ExchangeApplication
// to work with any writer (CSV, database, network) through a common interface,
// making the system extensible without modifying engine code.
class CSVWriter : public IWriter {
public:
    explicit CSVWriter(const std::string& outputPath);

    // Marks that this function overrides the base class virtual method.
    // If the signature doesn't match, the compiler will catch it as an error.
    void writeReport(ExecutionReport report) override;

    // Flush and close the file cleanly on destruction.
    ~CSVWriter() override;

private:
    std::ofstream outputFile_;
    std::mutex    writeMutex_;  // Guards the file stream — multiple threads may call writeReport()

    void writeHeader();

    // Format a single ExecutionReport as a CSV row
    static std::string formatRow(const ExecutionReport& report);

    // Convert ExecStatus enum to the string the spec requires
    static std::string statusToString(ExecStatus status);
};