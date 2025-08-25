#include "headers.h"
#include <boost/algorithm/string.hpp>
#include <charconv>
#include <cstddef>
#include <ranges>
#include <string_view>

using Callback = std::function<void(std::string_view, std::string_view)>;

std::string_view trim(std::string_view sv) {

    while (!sv.empty() && std::isspace(sv.front())) {
        sv.remove_prefix(1);
    }

    while (!sv.empty() && std::isspace(sv.back())) {
        sv.remove_suffix(1);
    }
    return sv;
};

void iterHeaders(std::string_view req, Callback &&callback) {
    // Находим начало блока заголовков
    const auto headers_start_pos = req.find("\r\n");
    if (headers_start_pos == std::string_view::npos) {
        return;
    }
    auto headers_sv = req.substr(headers_start_pos + 2);

    // Находим конец блока заголовков (двойное \r\n\r\n)
    const auto body_start_pos = headers_sv.find("\r\n\r\n");
    if (body_start_pos != std::string_view::npos) {
        headers_sv = headers_sv.substr(0, body_start_pos);
    }

    for (const auto &line_range : std::views::split(headers_sv, std::string_view("\r\n"))) {
        std::string_view line(line_range.begin(), line_range.end());

        // Пустая строка может появиться в конце, пропускаем ее
        if (line.empty()) {
            continue;
        }

        // Разделяем строку на имя и значение
        const auto colon_pos = line.find(':');
        if (colon_pos == std::string_view::npos) {
            continue;  // Некорректный заголовок
        }

        auto name = line.substr(0, colon_pos);
        auto value = line.substr(colon_pos + 1);

        callback(trim(name), trim(value));
    }
}

ConnectionPoint findHostPort(std::string_view req) {
    std::string host, port;
    iterHeaders(req, [&](std::string_view name, std::string_view value) {
        if (boost::iequals(name, "Host")) {
            // Разделяем хост и порт
            const auto colon_pos = value.find(':');
            if (colon_pos != std::string_view::npos) {
                host = value.substr(0, colon_pos);
                port = value.substr(colon_pos + 1);
            } else {
                host = value;
                port = "80";
            }
        }
    });
    return {host, port};
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    std::optional<size_t> result;

    iterHeaders(rsp, [&](std::string_view name, std::string_view value) {
        if (boost::iequals(name, "Content-length")) {
            size_t num;
            auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), num);
            if (ec == std::errc() && ptr == value.data() + value.size()) {
                result = num;
            }
        }
    });

    return result;
}
