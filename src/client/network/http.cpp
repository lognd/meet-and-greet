#include "network.h"

// cpp-httplib is header-only; including it here keeps compile times fast.
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT 0
#endif
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
    s.uuid        = j.value("uuid", "");
    s.passphrase  = j.value("passphrase", "");
    s.is_new      = j.value("is_new", true);
    s.forename    = j.value("forename", forename);
    s.surname     = j.value("surname", surname);
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
    return j.value("ok", false);
}

std::vector<Target> HttpClient::get_targets(const std::string& uuid) {
    auto cli = make_client(host, port);
    auto res = cli.Get("/targets/" + uuid);
    json j = parse(res);
    if (j.is_null() || !j.contains("targets")) return {};

    std::vector<Target> targets;
    for (const auto& t : j["targets"]) {
        targets.push_back({
            t.value("uuid", ""),
            t.value("forename", ""),
            t.value("surname", ""),
            t.value("passphrase_hint", ""),
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
    if (!j.value("ok", false)) {
        error_msg = j.value("reason", "unknown error");
        return {};
    }
    out_target_uuid     = j.value("target_uuid", "");
    out_target_forename = j.value("target_forename", "");
    std::vector<std::string> questions;
    for (const auto& q : j.value("questions", json::array()))
        questions.push_back(q.get<std::string>());
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
    out_completed = j.value("meetings_completed", 0);
    out_total     = j.value("total_targets", 0);
    return j.value("ok", false);
}

std::optional<HttpClient::Stats> HttpClient::get_stats(const std::string& uuid) {
    auto cli = make_client(host, port);
    auto res = cli.Get("/stats/" + uuid);
    json j = parse(res);
    if (j.is_null()) return std::nullopt;
    Stats s;
    s.completed = j.value("meetings_completed", 0);
    s.total     = j.value("total_targets", 0);
    s.place     = j.value("finish_place", 0);
    s.ordinal   = j.value("finish_ordinal", "");
    return s;
}

std::optional<HttpClient::TimeInfo> HttpClient::get_time() {
    auto cli = make_client(host, port);
    auto res = cli.Get("/time");
    json j = parse(res);
    if (j.is_null()) return std::nullopt;
    TimeInfo t;
    t.server_time = j.value("server_time", 0.0);
    t.deadline    = j.value("deadline", 0.0);
    t.remaining   = j.value("remaining_seconds", -1.0);
    return t;
}

std::vector<HttpClient::Announcement> HttpClient::get_announcements(double since) {
    auto cli = make_client(host, port);
    auto res = cli.Get("/announcements?since=" + std::to_string(since));
    json j = parse(res);
    if (j.is_null() || !j.contains("announcements")) return {};
    std::vector<Announcement> out;
    for (const auto& a : j["announcements"]) {
        out.push_back({
            a.value("uuid", ""),
            a.value("message", ""),
            a.value("sent_at", 0.0),
        });
    }
    return out;
}

} // namespace mag
