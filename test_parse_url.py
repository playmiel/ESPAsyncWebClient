import pytest


def parse_url(url: str):
    url_copy = url
    secure = False
    port = 80
    if url_copy.startswith("https://"):
        secure = True
        port = 443
        url_copy = url_copy[8:]
    elif url_copy.startswith("http://"):
        secure = False
        port = 80
        url_copy = url_copy[7:]
    else:
        secure = False
        port = 80

    path_index = url_copy.find('/')
    query_index = url_copy.find('?')

    if path_index == -1:
        if query_index == -1:
            host = url_copy
            path = "/"
        else:
            host = url_copy[:query_index]
            path = "/" + url_copy[query_index:]
    elif query_index != -1 and query_index < path_index:
        host = url_copy[:query_index]
        path = "/" + url_copy[query_index:]
    else:
        host = url_copy[:path_index]
        path = url_copy[path_index:]

    port_index = host.find(':')
    if port_index != -1:
        port = int(host[port_index + 1:])
        host = host[:port_index]

    return host, path, port, secure


def test_parse_url_query_without_path():
    host, path, port, secure = parse_url("http://example.com?foo=bar")
    assert host == "example.com"
    assert path == "/?foo=bar"
    assert port == 80
    assert not secure


def test_parse_url_https_with_path_and_query():
    host, path, port, secure = parse_url("https://example.com/path?foo=bar")
    assert host == "example.com"
    assert path == "/path?foo=bar"
    assert port == 443
    assert secure


def test_parse_url_simple():
    host, path, port, secure = parse_url("http://example.com")
    assert host == "example.com"
    assert path == "/"
    assert port == 80
    assert not secure
