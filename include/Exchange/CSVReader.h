#pragma once
#include "Order.h"
#include <string>
#include <vector>
#include <functional>

class CSVReader {
public:
    explicit CSVReader(std::string filePath);

    // Callback invoked for each successfully parsed order.
    // The reader doesn't know how to process orders — it delegates
    // via callback, decoupling the reader from business logic.
    using OrderCallback = std::function<void(Order, std::string )>;

    // Reads the file line by line, invoking callback for each row.
    int readOrders(const OrderCallback& callback) const;

private:
    std::string filePath_;

    // split a CSV line by comma, respecting empty fields
    static std::vector<std::string> splitLine(const std::string& line);

    //trim leading/trailing whitespace from a token
    static std::string trim(const std::string& s);
};