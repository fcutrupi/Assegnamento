// Punto di ingresso: legge le opzioni, costruisce i sottosistemi, avvia i thread
// e genera i report al termine ordinato della simulazione.

#include "fas/Domain.hpp"
#include "fas/Infrastructure.hpp"
#include "fas/Simulation.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct Options {
    std::string inputDirectory = "data/case01_basic";
    std::string outputDirectory = "output";
    int agvCount = 10;
    double millisecondsPerMinute = 5.0;
};

// Interpreta gli argomenti della riga di comando e controlla i vincoli essenziali.
Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](const std::string& flag) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value after " + flag);
            return argv[++i];
        };
        if (arg == "--input") options.inputDirectory = value(arg);
        else if (arg == "--output") options.outputDirectory = value(arg);
        else if (arg == "--agvs") options.agvCount = std::stoi(value(arg));
        else if (arg == "--scale-ms") options.millisecondsPerMinute = std::stod(value(arg));
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: fas_simulator [--input DIR] [--output DIR] [--agvs N] [--scale-ms MS]\n";
            std::exit(0);
        } else throw std::runtime_error("Unknown argument: " + arg);
    }
    if (options.agvCount < 10) throw std::runtime_error("--agvs must be at least 10 for this assignment");
    if (!std::isfinite(options.millisecondsPerMinute) || options.millisecondsPerMinute <= 0.0)
        throw std::runtime_error("--scale-ms must be a finite positive value");
    return options;
}
}

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        std::filesystem::create_directories(options.outputDirectory);
        fas::InputData input = fas::loadInputData(options.inputDirectory);

        // Tutti i sottosistemi condividono lo stesso riferimento temporale e logger.
        fas::SimulationClock clock(options.millisecondsPerMinute);
        clock.start();
        const int baseMinute = input.orders.empty() ? 8 * 60 : input.orders.front().scheduledMinute;
        fas::EventLogger logger(clock, options.outputDirectory + "/sim_log.txt", baseMinute);
        fas::Warehouse warehouse(std::move(input.initialInventory), logger);
        fas::AgvFleet fleet(options.agvCount, clock, logger, warehouse);
        fas::AssemblyStation station(clock, logger, warehouse, fleet, input.products);
        fas::ControlCenter control(clock, logger, station, std::move(input.orders));

        logger.write("simulation started with " + std::to_string(options.agvCount) + " AGVs");
        // Avviamo prima i consumatori e poi il centro che pubblica gli ordini.
        fleet.start();
        station.start();
        control.start();
        // Il join del centro garantisce che non vengano pubblicati altri ordini;
        // quello della stazione attende il completamento dell'intera coda.
        control.join();
        station.join();
        fleet.stop();

        const double horizon = clock.elapsedMinutes();
        const auto results = station.results();
        fas::writeKpiReport(options.outputDirectory + "/kpi_report.txt", results, horizon,
                            station.busyMinutes(), fleet.totalBusyMinutes(), fleet.size());
        warehouse.writeSummary(options.outputDirectory + "/warehouse_summary.txt");
        logger.write("simulation stopped cleanly");
        std::cout << "Simulation complete. Results written to " << options.outputDirectory << "/\n";
        std::cout << "Orders processed: " << results.size() << "; AGVs: " << fleet.size() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
