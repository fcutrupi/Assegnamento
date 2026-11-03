// Implementazione dell'orologio accelerato e del logger thread-safe.
// Il tempo simulato deriva dal tempo reale trascorso e da un fattore di scala.

#include "fas/Infrastructure.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace fas {

SimulationClock::SimulationClock(double millisecondsPerSimulatedMinute)
    : scaleMsPerMinute_(millisecondsPerSimulatedMinute) {
    if (!std::isfinite(scaleMsPerMinute_) || scaleMsPerMinute_ <= 0.0)
        throw std::invalid_argument("Simulation time scale must be a finite positive value");
}

// Fissa l'origine temporale prima di avviare i thread della simulazione.
void SimulationClock::start() {
    start_ = std::chrono::steady_clock::now();
    started_ = true;
}

// Restituisce il tempo reale trascorso convertito in minuti simulati.
double SimulationClock::elapsedMinutes() const {
    if (!started_) return 0.0;
    const auto elapsed = std::chrono::steady_clock::now() - start_;
    const double elapsedMs = std::chrono::duration<double, std::milli>(elapsed).count();
    return elapsedMs / scaleMsPerMinute_;
}

void SimulationClock::sleepMinutes(double minutes) const {
    if (minutes <= 0.0) return;
    const auto duration = std::chrono::duration<double, std::milli>(minutes * scaleMsPerMinute_);
    std::this_thread::sleep_for(duration);
}

// Attende solo il tempo residuo, senza accumulare ritardi già trascorsi.
void SimulationClock::sleepUntilMinute(double targetMinute) const {
    const double remaining = targetMinute - elapsedMinutes();
    sleepMinutes(remaining);
}

EventLogger::EventLogger(const SimulationClock& clock, const std::string& path, int baseMinuteOfDay)
    : clock_(clock), stream_(path), baseMinuteOfDay_(baseMinuteOfDay) {
    if (!stream_) throw std::runtime_error("Cannot create simulation log: " + path);
}

// Timestamp e scrittura sono protetti dallo stesso mutex per mantenere coerenti le righe.
void EventLogger::write(const std::string& message) {
    // L acquisizione del timestamp e la scrittura avvengono insieme sotto mutex,
    // così un thread non può scrivere un evento più vecchio dopo uno più recente.
    std::lock_guard<std::mutex> lock(mutex_);
    const double elapsed = clock_.elapsedMinutes();
    const int minuteOfDay = (baseMinuteOfDay_ + static_cast<int>(std::floor(elapsed))) % (24 * 60);
    const int hour = minuteOfDay / 60;
    const int minute = minuteOfDay % 60;
    stream_ << std::setfill('0') << std::setw(2) << hour << ':' << std::setw(2) << minute
            << std::setfill(' ') << "  " << message << '\n';
    stream_.flush();
}

} // Fine namespace fas
