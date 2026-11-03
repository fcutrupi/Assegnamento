// Implementazione dei sottosistemi e del coordinamento della simulazione.
// Ogni AGV e la stazione di assemblaggio lavorano in thread separati.

#include "fas/Simulation.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fas {

Warehouse::Warehouse(std::map<std::string, int> inventory, EventLogger& logger)
    : components_(std::move(inventory)), logger_(logger) {}

bool Warehouse::reserveComponents(const ProductSpec& product, std::vector<std::string>& units, std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);

    // La BOM può riportare più righe per lo stesso componente: sommiamo prima
    // le quantità, altrimenti ogni riga verrebbe confrontata con la stessa scorta.
    std::map<std::string, int> requiredQuantities;
    for (const auto& requirement : product.billOfMaterials)
        requiredQuantities[requirement.componentId] += requirement.quantity;

    // Prima controlliamo tutti i componenti; così un ordine rifiutato non consuma
    // parzialmente le scorte del magazzino.
    for (const auto& requirement : requiredQuantities) {
        const auto it = components_.find(requirement.first);
        const int available = it == components_.end() ? 0 : it->second;
        if (available < requirement.second) {
            std::ostringstream message;
            message << "insufficient inventory for product " << product.productId << ": component "
                    << requirement.first << " requires " << requirement.second
                    << ", available " << available;
            reason = message.str();
            return false;
        }
    }

    // Solo dopo il controllo completo aggiorniamo le quantità e creiamo le unità
    // da affidare agli AGV .
    for (const auto& requirement : requiredQuantities) {
        components_[requirement.first] -= requirement.second;
        for (int i = 0; i < requirement.second; ++i)
            units.push_back(requirement.first);
    }
    return true;
}

void Warehouse::storeFinishedProduct(const std::string& productId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++finishedProducts_[productId];
    }
    logger_.write("warehouse stored finished product " + productId);
}

void Warehouse::writeSummary(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot create warehouse summary: " + path);
    out << "Remaining components\n";
    for (const auto& item : components_) out << item.first << ' ' << item.second << '\n';
    out << "\nFinished products\n";
    for (const auto& item : finishedProducts_) out << item.first << ' ' << item.second << '\n';
}

AgvFleet::AgvFleet(int count, const SimulationClock& clock, EventLogger& logger, Warehouse& warehouse)
    : clock_(clock), logger_(logger), warehouse_(warehouse), workerCount_(count) {
    if (count < 10) throw std::invalid_argument("The assignment requires at least 10 AGVs");
    workers_.reserve(static_cast<std::size_t>(count));
}

AgvFleet::~AgvFleet() { stop(); }

void AgvFleet::start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) return;
    for (int i = 1; i <= workerCount_; ++i)
        workers_.emplace_back(&AgvFleet::workerLoop, this, i);
}

std::future<void> AgvFleet::dispatch(TransportKind kind, const std::string& itemId, int orderSequence) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    tasks_.push(TransportTask{kind, itemId, orderSequence, promise});
    return future;
}

void AgvFleet::stop() {
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) return;
    tasks_.close();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
}

double AgvFleet::totalBusyMinutes() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return totalBusyMinutes_;
}

void AgvFleet::logState(int agvNumber, AgvState state, const std::string& detail) {
    const char* names[] = {"IDLE", "TO_WAREHOUSE", "PICKING", "TO_STATION", "DROPPING", "TO_PARKING"};
    logger_.write("AGV" + std::to_string(agvNumber) + " [" + names[static_cast<int>(state)] + "] " + detail);
}

void AgvFleet::workerLoop(int agvNumber) {
    while (auto task = tasks_.pop()) {
        double busy = 0.0;
        try {
            processTask(agvNumber, *task, busy);
            task->completion->set_value();
        } catch (...) {
            try { task->completion->set_exception(std::current_exception()); } catch (...) {}
        }
        std::lock_guard<std::mutex> lock(metricsMutex_);
        totalBusyMinutes_ += busy;
    }
    logState(agvNumber, AgvState::Idle, "worker stopped");
}

void AgvFleet::processTask(int agvNumber, const TransportTask& task, double& busyMinutes) {
    auto travel = [&](double minutes, AgvState state, const std::string& description) {
        logState(agvNumber, state, description);
        clock_.sleepMinutes(minutes);
        busyMinutes += minutes;
    };
    auto activity = [&](double minutes, AgvState state, const std::string& description) {
        logState(agvNumber, state, description);
        clock_.sleepMinutes(minutes);
        busyMinutes += minutes;
    };

    if (task.kind == TransportKind::ComponentToStation) {
        travel(2.0, AgvState::ToWarehouse, "travelling from parking to warehouse for " + task.itemId +
               " (order " + std::to_string(task.orderSequence + 1) + ")");
        activity(1.0, AgvState::Picking, "picking component " + task.itemId);
        travel(3.0, AgvState::ToStation, "carrying component " + task.itemId + " to assembly station");
        activity(1.0, AgvState::Dropping, "delivering component " + task.itemId + " to assembly station");
    } else {
        travel(4.0, AgvState::ToStation, "travelling to assembly station for finished product " + task.itemId);
        activity(1.0, AgvState::Picking, "picking finished product " + task.itemId);
        travel(3.0, AgvState::ToWarehouse, "transporting finished product " + task.itemId + " to warehouse");
        activity(1.0, AgvState::Dropping, "delivering finished product " + task.itemId + " to warehouse");
        warehouse_.storeFinishedProduct(task.itemId);
    }
    travel(2.0, AgvState::ToParking, "returning to parking location");
}

AssemblyStation::AssemblyStation(const SimulationClock& clock, EventLogger& logger, Warehouse& warehouse,
                                 AgvFleet& fleet, const std::map<std::string, ProductSpec>& products)
    : clock_(clock), logger_(logger), warehouse_(warehouse), fleet_(fleet), products_(products) {}

void AssemblyStation::start() { worker_ = std::thread(&AssemblyStation::run, this); }
void AssemblyStation::enqueue(ProductionOrder order) { orders_.push(std::move(order)); }
void AssemblyStation::finishInput() { orders_.close(); }
void AssemblyStation::join() { if (worker_.joinable()) worker_.join(); }

std::vector<OrderResult> AssemblyStation::results() const {
    std::lock_guard<std::mutex> lock(resultsMutex_);
    return results_;
}

double AssemblyStation::busyMinutes() const { return busyMinutes_.load(); }

void AssemblyStation::run() {
    while (auto order = orders_.pop()) {
        // Un errore inatteso su un ordine non deve terminare il processo con std::terminate.
        // Registriamo l'ordine come non completato e continuiamo con i successivi.
        try {
            processOrder(*order);
        } catch (const std::exception& error) {
            logger_.write("errore durante l'ordine " + std::to_string(order->sequence + 1) +
                          ": " + error.what());
            OrderResult result;
            result.order = *order;
            result.reason = std::string("errore interno: ") + error.what();
            std::lock_guard<std::mutex> lock(resultsMutex_);
            results_.push_back(std::move(result));
        } catch (...) {
            logger_.write("errore sconosciuto durante l'ordine " + std::to_string(order->sequence + 1));
            OrderResult result;
            result.order = *order;
            result.reason = "errore interno sconosciuto";
            std::lock_guard<std::mutex> lock(resultsMutex_);
            results_.push_back(std::move(result));
        }
    }
}

void AssemblyStation::processOrder(const ProductionOrder& order) {
    OrderResult result;
    result.order = order;
    const auto productIt = products_.find(order.productId);
    if (productIt == products_.end()) {
        result.reason = "unknown product " + order.productId;
        std::lock_guard<std::mutex> lock(resultsMutex_);
        results_.push_back(result);
        return;
    }
    const ProductSpec& product = productIt->second;
    logger_.write("station started preparation for order " + std::to_string(order.sequence + 1) +
                  " (" + product.productId + ")");

    std::vector<std::string> componentUnits;
    std::string failureReason;
    if (!warehouse_.reserveComponents(product, componentUnits, failureReason)) {
        result.reason = failureReason;
        logger_.write("order " + std::to_string(order.sequence + 1) + " rejected: " + failureReason);
        std::lock_guard<std::mutex> lock(resultsMutex_);
        results_.push_back(result);
        return;
    }

    std::vector<std::future<void>> deliveries;
    deliveries.reserve(componentUnits.size());
    for (const auto& component : componentUnits)
        deliveries.push_back(fleet_.dispatch(TransportKind::ComponentToStation, component, order.sequence));
    for (auto& delivery : deliveries) delivery.get();

    // Ipotesi progettuale: il setup dura 2 minuti simulati per ogni tipo distinto
    // di componente presente nella BOM. Il tempo di setup si somma al tempo base.
    std::set<std::string> componentTypes;
    for (const auto& requirement : product.billOfMaterials)
        componentTypes.insert(requirement.componentId);
    const double setupMinutes = 2.0 * static_cast<double>(componentTypes.size());
    const double operationMinutes = product.baseAssemblyMinutes + setupMinutes;
    logger_.write("station assembling order " + std::to_string(order.sequence + 1) + " (" + product.productId +
                  "), base=" + std::to_string(product.baseAssemblyMinutes) + " min, setup=" +
                  std::to_string(setupMinutes) + " min");
    clock_.sleepMinutes(operationMinutes);
    busyMinutes_.store(busyMinutes_.load() + operationMinutes);
    logger_.write("station completed assembly for order " + std::to_string(order.sequence + 1) +
                  " (" + product.productId + ")");

    auto returnDelivery = fleet_.dispatch(TransportKind::FinishedProductToWarehouse, product.productId, order.sequence);
    returnDelivery.get();
    result.completed = true;
    result.completedAt = clock_.elapsedMinutes();
    result.reason = "completed and stored in warehouse";
    logger_.write("order " + std::to_string(order.sequence + 1) + " finished; lead-time endpoint recorded");
    std::lock_guard<std::mutex> lock(resultsMutex_);
    results_.push_back(result);
}

ControlCenter::ControlCenter(const SimulationClock& clock, EventLogger& logger, AssemblyStation& station,
                             std::vector<ProductionOrder> orders)
    : clock_(clock), logger_(logger), station_(station), orders_(std::move(orders)) {}

void ControlCenter::start() { worker_ = std::thread(&ControlCenter::run, this); }
void ControlCenter::join() { if (worker_.joinable()) worker_.join(); }

void ControlCenter::run() {
    // Il primo timestamp è il riferimento zero della timeline simulata; i successivi
    // rilasci rispettano gli scarti temporali indicati in orders.txt.
    const int firstMinute = orders_.empty() ? 8 * 60 : orders_.front().scheduledMinute;
    for (auto order : orders_) {
        const double releaseOffset = static_cast<double>(order.scheduledMinute - firstMinute);
        clock_.sleepUntilMinute(releaseOffset);
        order.releasedAt = clock_.elapsedMinutes();
        logger_.write("control center released order " + std::to_string(order.sequence + 1) +
                      " for product " + order.productId + " (FIFO)");
        station_.enqueue(order);
    }
    station_.finishInput();
    logger_.write("control center released all orders");
}

void writeKpiReport(const std::string& path, const std::vector<OrderResult>& results,
                    double simulationMinutes, double stationBusyMinutes,
                    double agvBusyMinutes, int agvCount) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot create KPI report: " + path);
    int completed = 0, rejected = 0;
    double leadTimeSum = 0.0;
    for (const auto& result : results) {
        if (result.completed) {
            ++completed;
            leadTimeSum += std::max(0.0, result.completedAt - result.order.releasedAt);
        } else {
            ++rejected;
        }
    }
    // Il lead time medio considera gli ordini completati, dal rilascio effettivo
    // fino al rientro del prodotto finito nel magazzino.
    const double averageLeadTime = completed ? leadTimeSum / completed : 0.0;
    const double horizon = std::max(simulationMinutes, 1e-9);
    const double stationUtilization = stationBusyMinutes / horizon;
    const double throughput = completed / (horizon / 60.0);
    const double agvUtilization = agvCount > 0 ? agvBusyMinutes / (agvCount * horizon) : 0.0;

    out << std::fixed << std::setprecision(3);
    out << "Flexible Assembly Cell - KPI Report\n"
        << "===================================\n"
        << "Orders received: " << results.size() << '\n'
        << "Orders completed: " << completed << '\n'
        << "Orders rejected: " << rejected << '\n'
        << "Simulation horizon (simulated minutes): " << simulationMinutes << '\n'
        << "Average lead time (minutes, successful orders): " << averageLeadTime << '\n'
        << "Assembly station utilization: " << stationUtilization << '\n'
        << "Throughput (completed orders/hour): " << throughput << '\n'
        << "AGV count: " << agvCount << '\n'
        << "Average AGV utilization: " << agvUtilization << '\n'
        << "\nOrder details\n"
        << "-------------\n";
    for (const auto& result : results) {
        out << "Order " << (result.order.sequence + 1) << " / product " << result.order.productId << ": "
            << (result.completed ? "COMPLETED" : "NOT COMPLETED") << " - " << result.reason;
        if (result.completed) out << " - lead time " << std::max(0.0, result.completedAt - result.order.releasedAt) << " min";
        out << '\n';
    }
}

} // Fine namespace fas
