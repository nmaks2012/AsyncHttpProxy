#include "headers.h"

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/asio/write.hpp>
#include <cstddef>
#include <exception>
#include <iostream>
#include <print>
#include <string_view>
#include <thread>

namespace ba = boost::asio;
namespace bs = boost::system;
using ba::ip::tcp;

constexpr std::string_view delimiter = "\r\n\r\n";
constexpr size_t MAX_HEADERS_SIZE = 8192;
constexpr size_t MAX_BODY_SIZE = 10 * 1024 * 1024;

ba::awaitable<void> session(tcp::socket client_socket, ba::io_service &io_service) {

    std::optional<std::string> error_response;

    try {
        std::println("New session started with ThreadID = {}", std::this_thread::get_id());
        std::flush(std::cout); // Для проверки в тестах python

        // Читаем запрос от клиента
        // Максимальный размер буфера для предотвращения DoS-атак
        ba::streambuf request_buffer(MAX_HEADERS_SIZE);
        bs::error_code ec;
        auto bytes_read_request = co_await ba::async_read_until(client_socket, request_buffer, delimiter,
                                                                ba::redirect_error(ba::use_awaitable, ec));

        if (request_buffer.size() >= MAX_HEADERS_SIZE || ec == ba::error::not_found) {
            std::println(std::cerr, "Request headers size exceeds the limit.");
            co_return; 
        } else if (ec) {
            throw bs::system_error{ec};
        }

        std::string request{ba::buffers_begin(request_buffer.data()),
                            ba::buffers_begin(request_buffer.data()) + bytes_read_request};

        // Находим хост и порт
        const ConnectionPoint target = findHostPort(request);
        if (target.host.empty()) {
            std::println(std::cerr, "No Host header found");
            co_return;
        }

        // Устанавливаем соединение с целевым сервером
        tcp::resolver resolver(io_service);
        auto endpoints = co_await resolver.async_resolve(target.host, target.port, ba::use_awaitable);

        tcp::socket server_socket(io_service);
        co_await ba::async_connect(server_socket, endpoints, ba::use_awaitable);

        // Пересылаем запрос на сервер
        co_await ba::async_write(server_socket, request_buffer.data(), ba::use_awaitable);

        // Читаем заголовки ответа
        ba::streambuf response_headers_buffer(MAX_HEADERS_SIZE);
        auto bytes_read_response =
            co_await ba::async_read_until(server_socket, response_headers_buffer, delimiter, ba::use_awaitable);

        std::string headers_str{ba::buffers_begin(response_headers_buffer.data()),
                                ba::buffers_begin(response_headers_buffer.data()) + bytes_read_response};

        auto content_length = findContentLength(headers_str);
        if (content_length.has_value() && content_length.value() > MAX_BODY_SIZE) {
            std::println(std::cerr, "Content-Length is too large: {} MB", content_length.value() / 1024 / 1024);
            co_return;
        }

        // Моментальная передача заголовков клиенту, чтобы не дожидаясь можно было приступить к обработке,
        // особенно актуально для больших объемов данных
        co_await ba::async_write(client_socket, response_headers_buffer.data(), ba::use_awaitable);

        // Если указано значение Content-Length, то читаем тело
        if (content_length.has_value() && content_length.value() > 0) {
            // Буффер ограниченного размера
            std::array<char, 8192> body_buffer;
            bs::error_code ec;
            // Передача по чанкам, оптимизация для работы с большими объемами данных
            while (true) {
                // Читаем чанк от сервера
                auto bytes_read = co_await server_socket.async_read_some(ba::buffer(body_buffer),
                                                                         ba::redirect_error(ba::use_awaitable, ec));

                if (ec == ba::error::eof || bytes_read == 0) {
                    // Соединение закрыто сервером, передача завершена
                    break;
                } else if (ec) {
                    // Другая ошибка
                    throw bs::system_error{ec};
                }

                // Сразу же пишем этот чанк клиенту
                co_await ba::async_write(client_socket, ba::buffer(body_buffer, bytes_read), ba::use_awaitable);
            }
        }

        std::println("Session completed successfully");

    } catch (const std::exception &e) {
        std::println(std::cerr, "Session error: {}", e.what());
        error_response = "HTTP/1.1 502 Bad Gateway\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: 15\r\n"
                         "Connection: close\r\n\r\n"
                         "Bad Gateway\r\n";
    }

    if (error_response) {
        co_await ba::async_write(client_socket, ba::buffer(*error_response), ba::use_awaitable);
    }
}

class Server {
public:
    Server(ba::io_service &io_service, short port)
        : io_service_(io_service), acceptor_(io_service, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept([this](bs::error_code ec, tcp::socket socket) {
            if (!ec) {
                // Запускаем сопрограмму session
                co_spawn(io_service_, session(std::move(socket), io_service_), ba::detached);
            } else {
                std::println(std::cerr, "Accept error: {}", ec.message());
            }
            do_accept();  //  Продолжаем принимать соединения
        });
    }

    ba::io_service &io_service_;
    tcp::acceptor acceptor_;
};

int main(int argc, char *argv[]) {
    try {
        if (argc != 2) {
            std::println(std::cerr, "Usage: proxy_server <listen_port>");
            return 1;
        }
        ba::io_service io_service(1);
        Server server(io_service, std::atoi(argv[1]));

        std::println("Proxy server started on port {}, PID = {}", argv[1], std::this_thread::get_id());
        io_service.run();

    } catch (const std::exception &e) {
        std::println(std::cerr, "Exception: {}", e.what());
    }
}
