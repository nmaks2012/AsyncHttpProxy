import sys
import subprocess
import time
import requests
import threading
import socket
from http.server import BaseHTTPRequestHandler, HTTPServer
import pytest
import os
import re

# --- Конфигурация ---
PROXY_HOST = "127.0.0.1"
PROXY_PORT = 5555
SERVER_HOST = "127.0.0.1"
SERVER_PORT = 8000
MAX_HEADERS_SIZE = 8192
MAX_BODY_SIZE = 10 * 1024 * 1024

class SimpleHTTPRequestHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/large_response':
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(MAX_BODY_SIZE + 1))
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"OK")
    def do_POST(self):
        content_length = int(self.headers.get('Content-Length', 0))
        post_data = self.rfile.read(content_length)
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(post_data)))
        self.end_headers()
        self.wfile.write(post_data)
    def log_message(self, format, *args):
        return

# --- ФИКСТУРЫ PYTEST ---

@pytest.fixture(scope="session")
def proxy_executable_path():
    """
    Фикстура для определения пути к исполняемому файлу прокси.
    Читает путь из переменной окружения PROXY_EXECUTABLE.
    """
    path = os.environ.get("PROXY_EXECUTABLE")
    if not path:
        pytest.fail("Environment variable PROXY_EXECUTABLE is not set!")
    if not os.path.exists(path):
        pytest.fail(f"Executable not found at path specified by PROXY_EXECUTABLE: '{path}'")
    return path

@pytest.fixture(scope="session")
def http_server():
    """Фикстура, которая запускает тестовый HTTP-сервер один раз на всю сессию."""
    server = HTTPServer((SERVER_HOST, SERVER_PORT), SimpleHTTPRequestHandler)
    server_thread = threading.Thread(target=server.serve_forever)
    server_thread.daemon = True
    server_thread.start()
    time.sleep(0.1)
    yield server  # Предоставляем объект сервера тестам
    # Код после yield выполнится в конце сессии
    server.shutdown()
    server.server_close()
    server_thread.join()

@pytest.fixture(scope="function")
def proxy_process(proxy_executable_path, http_server, request):
    """
    Фикстура, которая запускает и останавливает прокси-сервер для каждого теста.
    Зависит от фикстур proxy_executable_path и http_server.
    """
    capture_output = "single_threaded_execution" in request.node.name
    
    cmd = [proxy_executable_path, str(PROXY_PORT)]
    stdout_pipe = subprocess.PIPE if capture_output else subprocess.DEVNULL
    stderr_pipe = subprocess.STDOUT if capture_output else subprocess.DEVNULL
    
    process = subprocess.Popen(cmd, stdout=stdout_pipe, stderr=stderr_pipe, text=capture_output)
    time.sleep(0.5)
    yield process  # Предоставляем объект процесса тестам
    # Код после yield выполнится после каждого теста
    process.terminate()
    process.wait()

def test_basic_request(proxy_process):
    proxies = {"http": f"http://{PROXY_HOST}:{PROXY_PORT}"}
    response = requests.get(f"http://{SERVER_HOST}:{SERVER_PORT}", proxies=proxies)
    assert response.status_code == 200
    assert response.text == "OK"

def test_max_headers_size(proxy_process):
    long_header_value = 'a' * MAX_HEADERS_SIZE
    request = (
        f"GET / HTTP/1.1\r\n"
        f"Host: {SERVER_HOST}:{SERVER_PORT}\r\n"
        f"X-Long-Header: {long_header_value}\r\n\r\n"
    ).encode('utf-8')
    
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((PROXY_HOST, PROXY_PORT))
        s.sendall(request)
        try:
            response_data = s.recv(1024)
            assert response_data == b''
        except ConnectionResetError:
            pass

def test_single_threaded_execution(proxy_process):
    proxies = {"http": f"http://{PROXY_HOST}:{PROXY_PORT}"}
    for _ in range(3):
        try:
            requests.get(f"http://{SERVER_HOST}:{SERVER_PORT}", proxies=proxies, timeout=1)
        except requests.RequestException:
            pass

    proxy_process.terminate()
            
    # Процесс будет остановлен фикстурой, получаем вывод здесь
    output, _ = proxy_process.communicate(timeout=2)
    
    main_thread_match = re.search(r"Proxy server started on port \d+, PID = (\S+)", output)
    assert main_thread_match is not None, "Could not find main thread/PID info"
    
    main_tid = main_thread_match.group(1)
    session_tids = re.findall(r"New session started with ThreadID = (\S+)", output)
    
    assert len(session_tids) >= 1, "No sessions were logged"
    for session_tid in session_tids:
        assert session_tid == main_tid

def test_post_request_with_body(proxy_process):
    proxies = {"http": f"http://{PROXY_HOST}:{PROXY_PORT}"}
    payload = b"Test POST payload."
    response = requests.post(f"http://{SERVER_HOST}:{SERVER_PORT}", data=payload, proxies=proxies)
    assert response.status_code == 200
    assert response.content == payload

def test_bad_gateway_error(proxy_process):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((PROXY_HOST, PROXY_PORT))
        request = b"GET http://127.0.0.1:9999/ HTTP/1.1\r\nHost: 127.0.0.1:9999\r\n\r\n"
        s.sendall(request)
        response_data = s.recv(1024)
        assert response_data.startswith(b"HTTP/1.1 502 Bad Gateway")

def test_no_host_header(proxy_process):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((PROXY_HOST, PROXY_PORT))
        request = b"GET / HTTP/1.1\r\n\r\n"
        s.sendall(request)
        response_data = s.recv(1024)
        assert response_data == b''

def test_oversized_response_body(proxy_process):
    request = (
        f"GET /large_response HTTP/1.1\r\n"
        f"Host: {SERVER_HOST}:{SERVER_PORT}\r\n"
        f"Connection: close\r\n\r\n"
    ).encode('utf-8')
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((PROXY_HOST, PROXY_PORT))
        s.sendall(request)
        try:
            response_data = s.recv(1024)
            assert response_data == b''
        except ConnectionResetError:
            pass

def test_concurrent_requests(proxy_process):
    proxies = {"http": f"http://{PROXY_HOST}:{PROXY_PORT}"}
    num_threads = 5
    results = [False] * num_threads
    def worker(i):
        try:
            r = requests.get(f"http://{SERVER_HOST}:{SERVER_PORT}", proxies=proxies, timeout=4)
            if r.status_code == 200 and r.text == "OK":
                results[i] = True
        except requests.RequestException:
            pass
    threads = [threading.Thread(target=worker, args=(i,)) for i in range(num_threads)]
    for t in threads: t.start()
    for t in threads: t.join()
    assert all(results)