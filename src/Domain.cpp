// Implementazione del caricamento e della validazione dei dati di input.

#include "fas/Domain.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <set>
#include <stdexcept>

namespace fas {
namespace {

// Riconosce righe vuote o commenti anche quando iniziano con spazi o tabulazioni.
bool isBlankOrComment(const std::string& line) {
    const auto first = line.find_first_not_of(" \t\r\n");
    return first == std::string::npos || line[first] == '#';
}

} // Fine namespace anonimo

InputData loadInputData(const std::string& directory) {
    InputData data;
    const auto path = [&](const std::string& name) { return directory + "/" + name; };

    std::set<std::string> productsWithBaseTime;
    {
        std::ifstream file(path("bom.txt"));
        if (!file) throw std::runtime_error("Cannot open input file: " + path("bom.txt"));
        std::string line;
        while (std::getline(file, line)) {
            if (isBlankOrComment(line)) continue;
            std::istringstream row(line);
            std::string productId, second, third;
            if (!(row >> productId >> second)) continue;
            if (!(row >> third)) {
                double baseTime = 0.0;
                try {
                    std::size_t parsed = 0;
                    baseTime = std::stod(second, &parsed);
                    if (parsed != second.size() || !std::isfinite(baseTime))
                        throw std::runtime_error("Invalid assembly time in bom.txt: " + line);
                } catch (const std::exception&) {
                    throw std::runtime_error("Invalid assembly time in bom.txt: " + line);
                }
                if (baseTime < 0.0) throw std::runtime_error("Negative assembly time in bom.txt: " + line);
                auto& product = data.products[productId];
                product.productId = productId;
                product.baseAssemblyMinutes = baseTime;
                productsWithBaseTime.insert(productId);
            } else {
                int quantity = 0;
                try {
                    std::size_t parsed = 0;
                    quantity = std::stoi(third, &parsed);
                    if (parsed != third.size())
                        throw std::runtime_error("Invalid component quantity in bom.txt: " + line);
                } catch (const std::exception&) {
                    throw std::runtime_error("Invalid component quantity in bom.txt: " + line);
                }
                if (quantity < 0) throw std::runtime_error("Negative component quantity in bom.txt: " + line);
                auto& product = data.products[productId];
                product.productId = productId;
                product.billOfMaterials.push_back({second, quantity});
            }
        }
    }

    {
        std::ifstream file(path("warehouse.txt"));
        if (!file) throw std::runtime_error("Cannot open input file: " + path("warehouse.txt"));
        std::string line;
        while (std::getline(file, line)) {
            if (isBlankOrComment(line)) continue;
            std::istringstream row(line);
            std::string item, extra;
            int quantity = 0;
            if (!(row >> item >> quantity) || (row >> extra))
                throw std::runtime_error("Invalid inventory row in warehouse.txt: " + line);
            if (quantity < 0) throw std::runtime_error("Negative inventory quantity for " + item);
            if (data.initialInventory.find(item) != data.initialInventory.end())
                throw std::runtime_error("Duplicate inventory entry for component: " + item);
            data.initialInventory[item] = quantity;
        }
    }

    {
        std::ifstream file(path("orders.txt"));
        if (!file) throw std::runtime_error("Cannot open input file: " + path("orders.txt"));
        std::string line;
        int sequence = 0;
        while (std::getline(file, line)) {
            if (isBlankOrComment(line)) continue;
            std::istringstream row(line);
            int hour = 0, minute = 0, priority = 0;
            std::string productId, priorityText, extra;
            if (!(row >> hour >> minute >> productId))
                throw std::runtime_error("Invalid order row in orders.txt: " + line);
            if (row >> priorityText) {
                try {
                    std::size_t parsed = 0;
                    priority = std::stoi(priorityText, &parsed);
                    if (parsed != priorityText.size())
                        throw std::runtime_error("Invalid priority in orders.txt: " + line);
                } catch (const std::exception&) {
                    throw std::runtime_error("Invalid priority in orders.txt: " + line);
                }
            }
            if (row >> extra)
                throw std::runtime_error("Unexpected extra field in orders.txt: " + line);
            if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
                throw std::runtime_error("Invalid time in orders.txt: " + line);
            data.orders.push_back({sequence++, hour * 60 + minute, productId, priority, 0.0});
        }
    }

    // Una definizione BOM incompleta (componenti senza tempo base) non deve
    // trasformarsi in un assemblaggio di durata zero.
    for (const auto& entry : data.products) {
        if (productsWithBaseTime.find(entry.first) == productsWithBaseTime.end())
            throw std::runtime_error("Missing assembly base time for product: " + entry.first);
    }

    // Ogni prodotto usato dagli ordini deve essere definito e avere un tempo base.
    for (const auto& order : data.orders) {
        if (data.products.find(order.productId) == data.products.end())
            throw std::runtime_error("Order references unknown product: " + order.productId);
    }
    for (const auto& entry : data.products) {
        if (entry.second.baseAssemblyMinutes < 0.0)
            throw std::runtime_error("Negative assembly time for product: " + entry.first);
    }
    // L'ordinamento è cronologico; stable_sort conserva l'ordine del file quando
    // due ordini hanno lo stesso timestamp, come richiesto dalla politica FIFO.
    std::stable_sort(data.orders.begin(), data.orders.end(), [](const auto& a, const auto& b) {
        return a.scheduledMinute < b.scheduledMinute;
    });
    return data;
}

} // Fine namespace fas
