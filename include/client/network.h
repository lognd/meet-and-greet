#pragma once

#include "data.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>

// Forward declaration - httplib is included in the .cpp files only.
namespace mag {

// --- UDP Discovery ---

// Broadcast "MAG_WHO" and listen for "MAG_SERVER <ip> <port>" responses.
// Also sends periodic retries. Blocks until server is found or timeout_sec
// elapses (0 = block forever). Returns empty optional on timeout.
std::optional<ServerInfo> discover_server(int udp_port, int timeout_sec = 0);

// --- HTTP API client ---

struct HttpClient {
    std::string host;
    int port;

    // Register or re-register. Returns filled Student on success.
    // Returns empty optional on network error.
    std::optional<Student> register_student(
        const std::string& encrypted_id,
        const std::string& forename,
        const std::string& surname);

    // Update name (after reconnect with name_differs=true).
    bool update_name(const std::string& uuid,
                     const std::string& forename,
                     const std::string& surname);

    // Get assigned targets. Returns empty if not yet assigned.
    std::vector<Target> get_targets(const std::string& uuid);

    // Attempt to authenticate a meeting with a passphrase.
    // Returns questions on success, empty on failure (sets error_msg).
    std::vector<std::string> meet(const std::string& finder_uuid,
                                  const std::string& passphrase,
                                  std::string& out_target_uuid,
                                  std::string& out_target_forename,
                                  std::string& error_msg);

    // Submit answers for a meeting.
    bool submit_answers(const std::string& finder_uuid,
                        const std::string& target_uuid,
                        const std::vector<Question>& answers,
                        int& out_completed,
                        int& out_total);

    struct Stats {
        int completed{0};
        int total{0};
        int place{0};
        std::string ordinal;
        std::vector<std::string> met_uuids;  // target UUIDs we have a recorded meeting with
    };
    std::optional<Stats> get_stats(const std::string& uuid);

    struct TimeInfo {
        double server_time{0};
        double deadline{0};
        double remaining{-1};
    };
    std::optional<TimeInfo> get_time();

    // Returns new announcements since the given timestamp.
    struct Announcement {
        std::string uuid;
        std::string message;
        double sent_at{0};
    };
    std::vector<Announcement> get_announcements(double since);

    // Returns a pending meeting initiated by another student entering our
    // passphrase.  The server pops and returns at most one entry per call,
    // so the client should poll periodically.  Returns empty when none.
    struct PendingMeet {
        std::string              finder_uuid;
        std::string              finder_forename;
        std::vector<std::string> questions;
    };
    std::optional<PendingMeet> get_pending_meet(const std::string& uuid);
};

} // namespace mag
