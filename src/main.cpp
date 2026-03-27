#include "ExchangeApplication.h"
#include "CSVWriter.h"
#include <iostream>
#include <memory>
#include <chrono>

int main(int argc, char* argv[]) {
    // Allow optional CLI override of input/output paths
    std::string inputFile  = (argc > 1) ? argv[1] : "orders.csv";
    std::string outputFile = (argc > 2) ? argv[2] : "execution_rep.csv";

    std::cout << "[Exchange] Starting Flower Exchange Matching Engine\n"
              << "[Exchange] Input  : " << inputFile  << '\n'
              << "[Exchange] Output : " << outputFile << '\n';

    try {
        // Create a CSV writer on the heap and pass it to the engine.
        // The engine only sees the IWriter interface, not the CSV details.
        auto writer = std::make_unique<CSVWriter>(outputFile);

        // Construct the engine — composes 5 books, 5 queues, 6 threads
        ExchangeApplication engine(std::move(writer));

        // Time the full run for the speed evaluation criterion
        auto t0 = std::chrono::high_resolution_clock::now();
        engine.run(inputFile);
        auto t1 = std::chrono::high_resolution_clock::now();

        double elapsedMs =
            std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::cout << "[Exchange] Completed in " << elapsedMs << " ms\n";

    } catch (const std::exception& ex) {
        std::cerr << "[Exchange] FATAL: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}