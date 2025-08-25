#include "headers.h"
#include <cstddef>
#include <gtest/gtest.h>
#include <string_view>

TEST(iterHeaders, Empty) {
    size_t count = 0;
    iterHeaders("", [&](std::string_view, std::string_view) { count++; });
    EXPECT_EQ(count, 0);
}

TEST(iterHeaders, SkipRequestLine) {
    std::string request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    std::vector<std::string> headers;

    iterHeaders(request, [&](std::string_view name, std::string_view value) {
        headers.push_back(std::string(name) + ": " + std::string(value));
    });

    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers[0], "Host: example.com");
}

TEST(iterHeaders, SingleHeader) {
    std::string_view req = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    std::map<std::string_view, std::string_view> headers;
    iterHeaders(req, [&](std::string_view name, std::string_view value) { headers[name] = value; });
    ASSERT_EQ(headers.size(), 1);
    ASSERT_EQ(headers["Host"], "example.com");
}

TEST(iterHeaders, MultipleHeaders) {
    std::string_view req = "GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: my-test-client\r\nAccept: */*\r\n\r\n";
    std::map<std::string_view, std::string_view> headers;
    iterHeaders(req, [&](std::string_view name, std::string_view value) { headers[name] = value; });
    ASSERT_EQ(headers.size(), 3);
    ASSERT_EQ(headers["Host"], "example.com");
    ASSERT_EQ(headers["User-Agent"], "my-test-client");
    ASSERT_EQ(headers["Accept"], "*/*");
}

TEST(iterHeaders, MultipleSameHeaders) {
    std::string_view req = "GET / HTTP/1.1\r\nAccept: text/html\r\nAccept: application/xml\r\n\r\n";
    std::vector<std::pair<std::string_view, std::string_view>> headers;
    iterHeaders(req, [&](std::string_view name, std::string_view value) { headers.emplace_back(name, value); });
    ASSERT_EQ(headers.size(), 2);
    ASSERT_EQ(headers[0].first, "Accept");
    ASSERT_EQ(headers[0].second, "text/html");
    ASSERT_EQ(headers[1].first, "Accept");
    ASSERT_EQ(headers[1].second, "application/xml");
}

TEST(findHostPort, Simple) {
    std::string_view req = "GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: test\r\n\r\n";
    const auto hp = findHostPort(req);
    ASSERT_EQ(hp.host, "example.com");
    ASSERT_EQ(hp.port, "80");
}

TEST(findHostPort, NoHost) {
    std::string_view req = "GET / HTTP/1.1\r\nUser-Agent: test\r\n\r\n";
    const auto hp = findHostPort(req);
    ASSERT_TRUE(hp.host.empty());
    ASSERT_TRUE(hp.port.empty());
}

TEST(findContentLength, Simple) {
    std::string_view rsp = "HTTP/1.1 200 OK\r\nContent-Length: 123\r\n\r\n";
    auto cl = findContentLength(rsp);
    ASSERT_TRUE(cl.has_value());
    ASSERT_EQ(cl.value(), 123);
}

TEST(findContentLength, NoContentLength) {
    std::string_view rsp = "HTTP/1.1 200 OK\r\nServer: test\r\n\r\n";
    auto cl = findContentLength(rsp);
    ASSERT_FALSE(cl.has_value());
}

