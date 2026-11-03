// Dichiarazioni dei sottosistemi della cella flessibile: magazzino, flotta AGV,
// stazione di assemblaggio, centro di controllo e calcolo degli indicatori KPI.

#pragma once

#include "fas/Domain.hpp"
#include "fas/Infrastructure.hpp"

#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fas {

enum class TransportKind { ComponentToStation, FinishedProductToWarehouse };
enum class AgvState { Idle, ToWarehouse, Picking, ToStation, Dropping, ToParking };

struct TransportTask {
    TransportKind kind{TransportKind::ComponentToStation};
    std::string itemId;
    int orderSequence{};
    std::shared_ptr<std::promise<void>> completion;
};

// Gestisce le scorte con un mutex; la prenotazione di una BOM è atomica.
class Warehouse {
public:
    Warehouse(std::map<std::string, int> inventory, EventLogger& logger);
    bool reserveComponents(const ProductSpec& product, std::vector<std::string>& units, std::string& reason);
    void storeFinishedProduct(const std::string& productId);
    void writeSummary(const std::string& path) const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, int> components_;
    std::map<std::string, int> finishedProducts_;
    EventLogger& logger_;
};

// Flotta di AGV: ogni worker possiede un thread e preleva missioni dalla coda condivisa.
class AgvFleet {
public:
    AgvFleet(int count, const SimulationClock& clock, EventLogger& logger, Warehouse& warehouse);
    ~AgvFleet();
    void start();
    std::future<void> dispatch(TransportKind kind, const std::string& itemId, int orderSequence);
    void stop();
    double totalBusyMinutes() const;
    int size() const noexcept { return static_cast<int>(workers_.size()); }

private:
    void workerLoop(int agvNumber);
    void processTask(int agvNumber, const TransportTask& task, double& busyMinutes);
    void logState(int agvNumber, AgvState state, const std::string& detail);

    const SimulationClock& clock_;
    EventLogger& logger_;
    Warehouse& warehouse_;
    BlockingQueue<TransportTask> tasks_;
    std::vector<std::thread> workers_;
    int workerCount_{10};
    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};
    mutable std::mutex metricsMutex_;
    double totalBusyMinutes_{0.0};
};

// La stazione consuma gli ordini in FIFO e ne elabora uno per volta.
class AssemblyStation {
public:
    AssemblyStation(const SimulationClock& clock, EventLogger& logger, Warehouse& warehouse,
                    AgvFleet& fleet, const std::map<std::string, ProductSpec>& products);
    void start();
    void enqueue(ProductionOrder order);
    void finishInput();
    void join();
    std::vector<OrderResult> results() const;
    double busyMinutes() const;

private:
    void run();
    void processOrder(const ProductionOrder& order);

    const SimulationClock& clock_;
    EventLogger& logger_;
    Warehouse& warehouse_;
    AgvFleet& fleet_;
    const std::map<std::string, ProductSpec>& products_;
    BlockingQueue<ProductionOrder> orders_;
    std::thread worker_;
    mutable std::mutex resultsMutex_;
    std::vector<OrderResult> results_;
    std::atomic<double> busyMinutes_{0.0};
};

// Rilascia gli ordini in base ai timestamp, preservando l ordine a parità di orario.
class ControlCenter {
public:
    ControlCenter(const SimulationClock& clock, EventLogger& logger, AssemblyStation& station,
                  std::vector<ProductionOrder> orders);
    void start();
    void join();

private:
    void run();
    const SimulationClock& clock_;
    EventLogger& logger_;
    AssemblyStation& station_;
    std::vector<ProductionOrder> orders_;
    std::thread worker_;
};

// Scrive nel file richiesto gli indicatori aggregati e il dettaglio degli ordini.
void writeKpiReport(const std::string& path, const std::vector<OrderResult>& results,
                    double simulationMinutes, double stationBusyMinutes,
                    double agvBusyMinutes, int agvCount);

} // Fine namespace fas
