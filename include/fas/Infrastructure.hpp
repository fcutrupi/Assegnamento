// Componenti infrastrutturali condivisi: orologio simulato, logger e coda bloccante.
// Le primitive di sincronizzazione proteggono le strutture usate da più thread.

#pragma once

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

namespace fas {

// Converte i tempi di attesa reali in minuti simulati tramite un fattore di scala.
class SimulationClock {
public:
    explicit SimulationClock(double millisecondsPerSimulatedMinute);
    void start();
    double elapsedMinutes() const;
    void sleepMinutes(double minutes) const;
    void sleepUntilMinute(double targetMinute) const;
    double scale() const noexcept { return scaleMsPerMinute_; }

private:
    double scaleMsPerMinute_;
    std::chrono::steady_clock::time_point start_;
    bool started_{false};
};

// Serializza timestamp e scrittura: più thread possono registrare eventi in sicurezza.
class EventLogger {
public:
    EventLogger(const SimulationClock& clock, const std::string& path, int baseMinuteOfDay = 8 * 60);
    void write(const std::string& message);

private:
    const SimulationClock& clock_;
    std::ofstream stream_;
    int baseMinuteOfDay_;
    std::mutex mutex_;
};

// Coda produttore-consumatore: pop attende finché arriva un elemento o la coda viene chiusa.
template <typename T>
class BlockingQueue {
public:
    // Inserisce un elemento e risveglia un consumatore in attesa.
    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            queue_.push(std::move(value));
        }
        ready_.notify_one();
    }

    // Restituisce nullopt solo quando la coda è chiusa e non contiene più elementi.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return std::nullopt;
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    // Impedisce nuovi inserimenti e risveglia tutti i consumatori.
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        ready_.notify_all();
    }

private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable ready_;
    bool closed_{false};
};

} // Fine namespace fas
