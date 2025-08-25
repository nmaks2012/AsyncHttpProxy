#pragma once

#include <string>
#include <functional>
#include <optional>

using Callback = std::function<void(std::string_view, std::string_view)>;

struct ConnectionPoint {
    std::string host;
    std::string port;
};

void iterHeaders(std::string_view req, Callback&& callback);

ConnectionPoint findHostPort(std::string_view req);

std::optional<size_t> findContentLength(std::string_view rsp);
