#include "CSVReader.h"
#include "Utils.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

CSVReader::CSVReader(std::string filePath)
    : filePath_(std::move(filePath))
{}

int CSVReader::readOrders(const OrderCallback& callback) const {
    std::ifstream file(filePath_);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open input file: " + filePath_);
    }

    std::string line;
    int         rowCount  = 0;
    int         seqNum    = 0;

    // Skip header row
    std::getline(file, line);

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        ++rowCount;
        ++seqNum;

        // Assign an order ID immediately — even rejected orders
        std::string orderId = Utils::generateOrderId();

        // We use try-catch here because converting strings to numbers can fail.
        // If a field contains letters instead of digits, std::stoi or std::stod
        // will throw an exception. Catching it here lets us create a rejection
        // report instead of crashing the program.
        try {
            auto tokens = splitLine(line);

            // Build a partially populated order; validator will check fields
            Order order;
            order.orderId = orderId;
            order.seqNum  = seqNum;

            // Field 0: ClientOrderId
            if (tokens.size() > 0) order.clientOrderId = trim(tokens[0]);

            // Field 1: Instrument
            if (tokens.size() > 1) order.instrument = trim(tokens[1]);

            // Field 2: Side  — WHY try/catch: "abc" → stoi throws
            if (tokens.size() > 2 && !trim(tokens[2]).empty()) {
                order.side = std::stoi(trim(tokens[2]));
            }

            // Field 3: Quantity
            if (tokens.size() > 3 && !trim(tokens[3]).empty()) {
                order.quantity = std::stoi(trim(tokens[3]));
            }

            // Field 4: Price  — WHY try/catch: "-" or blank → stod throws
            if (tokens.size() > 4 && !trim(tokens[4]).empty()) {
                order.price = std::stod(trim(tokens[4]));
            }

            // Send the order to the caller (ExchangeApplication) for processing
            // We use std::move() here to avoid copying the order's data unnecessarily.
            callback(std::move(order), line);

        } catch (const std::invalid_argument& e) {
            // Non-numeric field — create a shell order for rejection report
            Order shell;
            shell.orderId       = orderId;
            shell.seqNum        = seqNum;
            shell.clientOrderId = "(parse error)";
            shell.instrument    = "Unknown";
            shell.side          = 0;
            shell.price         = 0.0;
            shell.quantity      = 0;
            callback(std::move(shell), line);

        } catch (const std::out_of_range& e) {
            Order shell;
            shell.orderId       = orderId;
            shell.seqNum        = seqNum;
            shell.clientOrderId = "(range error)";
            shell.instrument    = "Unknown";
            shell.side          = 0;
            shell.price         = 0.0;
            shell.quantity      = 0;
            callback(std::move(shell), line);
        }
    }

    return rowCount;
}

std::vector<std::string> CSVReader::splitLine(const std::string& line) {
    std::vector<std::string> tokens;
    std::string              token;
    std::istringstream       ss(line);
    while (std::getline(ss, token, ',')) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string CSVReader::trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}