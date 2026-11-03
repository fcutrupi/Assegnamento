// Modello dei dati del dominio: prodotti, distinte base, ordini e risultati.
// La funzione di caricamento legge i tre file di input definiti dalla consegna.

#pragma once

#include <map>
#include <string>
#include <vector>

namespace fas {

// Quantità di un singolo componente richiesto dalla distinta base.
struct ComponentRequirement {
    std::string componentId;
    int quantity{};
};

// Dati statici del prodotto: tempo base e componenti necessari.
struct ProductSpec {
    std::string productId;
    double baseAssemblyMinutes{};
    std::vector<ComponentRequirement> billOfMaterials;
};

// Ordine rilasciato dal centro di controllo; releasedAt è espresso in minuti simulati.
struct ProductionOrder {
    int sequence{};
    int scheduledMinute{};
    std::string productId;
    int priority{}; // Letta per compatibilità con il formato; la politica FIFO non la utilizza.
    double releasedAt{};
};

// Esito dell ordine, utilizzato per il report e per il calcolo dei KPI.
struct OrderResult {
    ProductionOrder order;
    bool completed{false};
    std::string reason;
    double completedAt{};
};

struct InputData {
    std::vector<ProductionOrder> orders;
    std::map<std::string, ProductSpec> products;
    std::map<std::string, int> initialInventory;
};

// Carica BOM, scorte iniziali e ordini, sollevando un errore se i file non sono validi.
InputData loadInputData(const std::string& directory);

} // Fine namespace fas
