#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include <indx_head/nozzle_presence.hpp>

namespace buddy::puppies {

/// Nozzle presence reported by the INDX head, trusted only for the current invalidation token.
///
/// invalidate() and get() are meant for the Marlin task, publish() for the puppy task.
/// Each report is stored in one atomic together with the token the head acknowledged, and get()
/// compares that token with the current one. A report validated before an invalidation can thus
/// never be returned after it, however the tasks interleave.
class NozzlePresenceCache {
public:
    using NozzlePresence = indx_head::NozzlePresence;

    /// Token the head has to acknowledge before its reports are trusted.
    [[nodiscard]] uint16_t token() const {
        return token_.load();
    }

    /// Stores a report of \p presence from the head that acknowledged token \p ack.
    void publish(NozzlePresence presence, uint16_t ack) {
        report.store(pack(presence, ack));
    }

    /// Distrusts all reports until the head acknowledges \p new_token.
    void invalidate(uint16_t new_token) {
        token_.store(new_token);
        report.store(pack(NozzlePresence::unknown, new_token));
    }

    /// @returns nullopt until the head reports a definitive presence for the current token,
    /// true if the nozzle is present, false otherwise
    [[nodiscard]] std::optional<bool> get() const {
        const uint32_t packed = report.load();
        const auto presence = static_cast<NozzlePresence>(packed & 0xffff);
        const auto ack = static_cast<uint16_t>(packed >> 16);
        if (ack != token_.load() || presence == NozzlePresence::unknown) {
            return std::nullopt;
        }
        return presence == NozzlePresence::present;
    }

private:
    static constexpr uint32_t pack(NozzlePresence presence, uint16_t ack) {
        return (static_cast<uint32_t>(ack) << 16) | static_cast<uint16_t>(presence);
    }

    std::atomic<uint16_t> token_ { 0 };
    std::atomic<uint32_t> report { pack(NozzlePresence::unknown, 0) };
    static_assert(std::atomic<uint16_t>::is_always_lock_free);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
};

} // namespace buddy::puppies
