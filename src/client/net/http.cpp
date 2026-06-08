#include "network.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <optional>

using json = nlohmann::json;

namespace mag {

namespace {

httplib::Client make_client(const std::string& host, int port) {
    httplib::Client cli(host, port);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(10);
    return cli;
}

// Parse the JSON body from an httplib result.  Returns empty json on error.
json parse(const httplib::Result& res) {
    if (!res || res->status < 200 || res->status >= 300) return {};
    try {
        return json::parse(res->body);
    } catch (...) {
        return {};
    }
}

// Safe accessor: returns default when key is missing OR when value is null.
// nlohmann's j.value(key, def) only falls back for missing keys; it throws
// type_error.302 when the key exists but the value is JSON null.
template<typename T>
T jget(const json& j, const std::string& key, T def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    return it->get<T>();
}

} // namespace

std::optional<Student> HttpClient::register_student(
    const std::string& encrypted_id,
    const std::string& forename,
    const std::string& surname)
{
    auto cli = make_client(host, port);
    json body = {
        {"encrypted_id", encrypted_id},
        {"forename", forename},
        {"surname", surname},
    };
    auto res = cli.Post("/register", body.dump(), "application/json");
    json j = parse(res);
    if (j.is_null()) return std::nullopt;

    Student s;
    s.uuid        = jget<std::string>(j, "uuid", "");
    s.passphrase  = jget<std::string>(j, "passphrase", "");
    s.is_new      = jget<bool>(j, "is_new", true);
    s.forename    = jget<std::string>(j, "forename", forename);
    s.surname     = jget<std::string>(j, "surname", surname);
    return s;
}

bool HttpClient::update_name(
    const std::string& uuid,
    const std::string& forename,
    const std::string& surname)
{
    auto cli = make_client(host, port);
    json body = {{"forename", forename}, {"surname", surname}};
    auto res = cli.Put("/student/" + uuid, body.dump(), "application/json");
    json j = parse(res);
    return jget<bool>(j, "ok", false);
}

std::vector<Target> HttpClient::get_targets(const std::string& uuid) {
    auto cli = make_client(host, port);
    auto res = cli.Get("/targets/" + uuid);
    json j = parse(res);
    if (j.is_null() || !j.contains("targets") || !j["targets"].is_array()) return {};

    std::vector<Target> targets;
    for (const auto& t : j["targets"]) {
        targets.push_back({
            jget<std::string>(t, "uuid", ""),
            jget<std::string>(t, "forename", ""),
            jget<std::string>(t, "surname", ""),
            jget<std::string>(t, "passphrase_hint", ""),
        });
    }
    return targets;
}

std::vector<std::string> HttpClient::meet(
    const std::string& finder_uuid,
    const std::string& passphrase,
    std::string& out_target_uuid,
    std::string& out_target_forename,
    std::string& error_msg)
{
    auto cli = make_client(host, port);
    json body = {{"finder_uuid", finder_uuid}, {"passphrase", passphrase}};
    auto res = cli.Post("/meet", body.dump(), "application/json");
    json j = parse(res);
    if (j.is_null()) { error_msg = "Network error"; return {}; }
    if (!jget<bool>(j, "ok", false)) {
        error_msg = jget<std::string>(j, "reason", "unknown error");
        return {};
    }
    out_target_uuid     = jget<std::string>(j, "target_uuid", "");
    out_target_forename = jget<std::string>(j, "target_forename", "");
    std::vector<std::string> questions;
    auto qs = j.contains("questions") && j["questions"].is_array()
                  ? j["questions"] : json::array();
    for (const auto& q : qs)
        if (q.is_string()) questions.push_back(q.get<std::string>());
    return questions;
}

bool HttpClient::submit_answers(
    const std::string& finder_uuid,
    const std::string& target_uuid,
    const std::vector<Question>& answers,
    int& out_completed,
    int& out_total)
{
    auto cli = make_client(host, port);
    json ans_arr = json::array();
    for (const auto& q : answers)
        ans_arr.push_back({{"question", q.text}, {"answer", q.answer}});

    json body = {
        {"finder_uuid", finder_uuid},
        {"target_uuid", target_uuid},
        {"answers", ans_arr},
    };
    auto res = cli.Post("/answer", body.dump(), "application/json");
    json j = parse(res);
    if (j.is_null()) return false;
    out_completed = jget<int>(j, "meetings_completed", 0);
    out_total     = jget<int>(j, "total_targets", 0);
    return jget<bool>(j, "ok", false);
}

std::optional<HttpClient::Stats> HttpClient::get_stats(const std::string& uuid) {
    auto cli = make_client(host, port);
    auto res = cli.Get("/stats/" + uuid);
    json j = parse(res);
    if (j.is_null()) return std::nullopt;
    Stats s;
    s.completed = jget<int>(j, "meetings_completed", 0);
    s.total     = jget<int>(j, "total_targets", 0);
    s.place     = jget<int>(j, "finish_place", 0);
    s.ordinal   = jget<std::string>(j, "finish_ordinal", "");
    if (j.contains("met_target_uuids") && j["met_target_uuids"].is_array())
        for (const auto& u : j["met_target_uuids"])
            if (u.is_string()) s.met_uuids.push_back(u.get<std::string>());
    return s;
}

std::optional<HttpClient::TimeInfo> HttpClient::get_time() {
    auto cli = make_client(host, port);
    auto res = cli.Get("/time");
    json j = parse(res);
    if (j.is_null()) return std::nullopt;
    TimeInfo t;
    t.server_time = jget<double>(j, "server_time", 0.0);
    t.deadline    = jget<double>(j, "deadline", 0.0);
    t.remaining   = jget<double>(j, "remaining_seconds", -1.0);
    return t;
}

std::vector<HttpClient::Announcement> HttpClient::get_announcements(double since) {
    auto cli = make_client(host, port);
    auto res = cli.Get("/announcements?since=" + std::to_string(since));
    json j = parse(res);
    if (j.is_null() || !j.contains("announcements") || !j["announcements"].is_array()) return {};
    std::vector<Announcement> out;
    for (const auto& a : j["announcements"]) {
        out.push_back({
            jget<std::string>(a, "uuid", ""),
            jget<std::string>(a, "message", ""),
            jget<double>(a, "sent_at", 0.0),
        });
    }
    return out;
}

} // namespace mag
