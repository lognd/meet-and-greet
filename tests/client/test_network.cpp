// Integration tests for the HTTP client layer.
// Spins up a real httplib server on a loopback port, exercises HttpClient
// methods against it, and verifies the JSON parsing and struct population.

#include <catch2/catch_all.hpp>
#include "network.h"
#include "data.h"

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT 0
#endif
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

using json = nlohmann::json;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Mock server fixture
// ---------------------------------------------------------------------------

struct MockServer {
    httplib::Server svr;
    std::thread     thr;
    int             port{0};

    void start(int p = 19999) {
        port = p;
        thr = std::thread([this] { svr.listen("127.0.0.1", port); });
        std::this_thread::sleep_for(100ms);  // let it bind
    }

    void stop() {
        svr.stop();
        if (thr.joinable()) thr.join();
    }

    mag::HttpClient client() const {
        return mag::HttpClient{"127.0.0.1", port};
    }

    ~MockServer() { stop(); }
};

// ---------------------------------------------------------------------------
// register_student
// ---------------------------------------------------------------------------

TEST_CASE("register_student new student", "[network][http]") {
    MockServer mock;
    mock.svr.Post("/register", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content(
            R"({"uuid":"test-uuid","passphrase":"eagle-has-landed","is_new":true})",
            "application/json");
    });
    mock.start(19901);

    auto cli = mock.client();
    auto result = cli.register_student("enc123", "Jane", "Smith");

    REQUIRE(result.has_value());
    CHECK(result->uuid       == "test-uuid");
    CHECK(result->passphrase == "eagle-has-landed");
    CHECK(result->is_new     == true);
    CHECK(result->forename   == "Jane");
    CHECK(result->surname    == "Smith");
}

TEST_CASE("register_student reconnect (is_new false)", "[network][http]") {
    MockServer mock;
    mock.svr.Post("/register", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            R"({"uuid":"u2","passphrase":"fox","is_new":false,
                "forename":"Jane","surname":"Smith","name_differs":false})",
            "application/json");
    });
    mock.start(19902);

    auto result = mock.client().register_student("enc", "Jane", "Smith");
    REQUIRE(result.has_value());
    CHECK(result->is_new == false);
}

TEST_CASE("register_student returns nullopt on server error", "[network][http]") {
    MockServer mock;
    mock.svr.Post("/register", [](const httplib::Request&, httplib::Response& res) {
        res.status = 500;
        res.set_content("{}", "application/json");
    });
    mock.start(19903);

    auto result = mock.client().register_student("enc", "A", "B");
    CHECK(!result.has_value());
}

// ---------------------------------------------------------------------------
// update_name
// ---------------------------------------------------------------------------

TEST_CASE("update_name returns true on success", "[network][http]") {
    MockServer mock;
    mock.svr.Put("/student/u1", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"ok":true})", "application/json");
    });
    mock.start(19904);

    CHECK(mock.client().update_name("u1", "Jane", "Doe") == true);
}

// ---------------------------------------------------------------------------
// get_targets
// ---------------------------------------------------------------------------

TEST_CASE("get_targets parses list", "[network][http]") {
    MockServer mock;
    mock.svr.Get("/targets/u1", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            R"({"targets":[
                {"uuid":"t1","forename":"Bob","surname":"Jones","passphrase_hint":"fox"},
                {"uuid":"t2","forename":"Alice","surname":"Lee","passphrase_hint":"eagle"}
               ],"assigned":true})",
            "application/json");
    });
    mock.start(19905);

    auto targets = mock.client().get_targets("u1");
    REQUIRE(targets.size() == 2);
    CHECK(targets[0].forename        == "Bob");
    CHECK(targets[0].passphrase_hint == "fox");
    CHECK(targets[1].uuid            == "t2");
}

TEST_CASE("get_targets returns empty on 404", "[network][http]") {
    MockServer mock;
    mock.svr.Get("/targets/u1", [](const httplib::Request&, httplib::Response& res) {
        res.status = 404;
        res.set_content(R"({"detail":"not found"})", "application/json");
    });
    mock.start(19906);

    auto targets = mock.client().get_targets("u1");
    CHECK(targets.empty());
}

// ---------------------------------------------------------------------------
// meet
// ---------------------------------------------------------------------------

TEST_CASE("meet success returns questions", "[network][http]") {
    MockServer mock;
    mock.svr.Post("/meet", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            R"({"ok":true,"target_uuid":"tu","target_forename":"Bob",
                "questions":["Q1?","Q2?"]})",
            "application/json");
    });
    mock.start(19907);

    std::string t_uuid, t_name, error;
    auto qs = mock.client().meet("finder", "eagle-has-landed", t_uuid, t_name, error);
    REQUIRE(qs.size() == 2);
    CHECK(qs[0]  == "Q1?");
    CHECK(t_uuid == "tu");
    CHECK(t_name == "Bob");
    CHECK(error.empty());
}

TEST_CASE("meet failure returns empty questions and sets error", "[network][http]") {
    MockServer mock;
    mock.svr.Post("/meet", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"ok":false,"reason":"passphrase not recognized"})",
                        "application/json");
    });
    mock.start(19908);

    std::string t_uuid, t_name, error;
    auto qs = mock.client().meet("finder", "wrong", t_uuid, t_name, error);
    CHECK(qs.empty());
    CHECK(error == "passphrase not recognized");
}

// ---------------------------------------------------------------------------
// submit_answers
// ---------------------------------------------------------------------------

TEST_CASE("submit_answers records completion count", "[network][http]") {
    MockServer mock;
    mock.svr.Post("/answer", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"ok":true,"meetings_completed":3,"total_targets":5})",
                        "application/json");
    });
    mock.start(19909);

    std::vector<mag::Question> answers = {{"Q1?", "A1"}, {"Q2?", "A2"}};
    int completed = 0, total = 0;
    bool ok = mock.client().submit_answers("f", "t", answers, completed, total);
    CHECK(ok        == true);
    CHECK(completed == 3);
    CHECK(total     == 5);
}

// ---------------------------------------------------------------------------
// get_stats
// ---------------------------------------------------------------------------

TEST_CASE("get_stats parses place and ordinal", "[network][http]") {
    MockServer mock;
    mock.svr.Get("/stats/u1", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            R"({"meetings_completed":5,"total_targets":5,
                "finish_place":2,"finish_ordinal":"2nd","finished_at":1.0})",
            "application/json");
    });
    mock.start(19910);

    auto stats = mock.client().get_stats("u1");
    REQUIRE(stats.has_value());
    CHECK(stats->completed == 5);
    CHECK(stats->place     == 2);
    CHECK(stats->ordinal   == "2nd");
}

TEST_CASE("get_stats returns nullopt on network failure", "[network][http]") {
    // Nothing listening on 19911
    mag::HttpClient cli{"127.0.0.1", 19911};
    auto stats = cli.get_stats("u1");
    CHECK(!stats.has_value());
}

// ---------------------------------------------------------------------------
// get_time
// ---------------------------------------------------------------------------

TEST_CASE("get_time parses remaining seconds", "[network][http]") {
    MockServer mock;
    mock.svr.Get("/time", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            R"({"server_time":1000.0,"deadline":1600.0,"remaining_seconds":600.0})",
            "application/json");
    });
    mock.start(19912);

    auto t = mock.client().get_time();
    REQUIRE(t.has_value());
    CHECK(t->remaining == Catch::Approx(600.0));
}

// ---------------------------------------------------------------------------
// get_announcements
// ---------------------------------------------------------------------------

TEST_CASE("get_announcements returns new messages", "[network][http]") {
    MockServer mock;
    mock.svr.Get("/announcements", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            R"({"announcements":[{"uuid":"a1","message":"wrap up!","sent_at":500.0}]})",
            "application/json");
    });
    mock.start(19913);

    auto anns = mock.client().get_announcements(0.0);
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].message == "wrap up!");
    CHECK(anns[0].sent_at == Catch::Approx(500.0));
}
