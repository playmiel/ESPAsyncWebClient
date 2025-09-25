import pytest
import re
from pathlib import Path


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


def load_cases():
    header = Path(__file__).parent / "url_test_cases.h"
    if not header.exists():
        return []
    content = header.read_text()
    pattern = re.compile(r'X\("([^"\\]+)","([^"\\]+)","([^"\\]+)",(\d+),(true|false)\)')
    cases = []
    for m in pattern.finditer(content):
        url, host, path, port, secure = m.groups()
        cases.append((url, host, path, int(port), secure == 'true'))
    return cases


@pytest.mark.parametrize("url,exp_host,exp_path,exp_port,exp_secure", load_cases())
def test_parse_url_table(url, exp_host, exp_path, exp_port, exp_secure):
    host, path, port, secure = parse_url(url)
    assert (host, path, port, secure) == (exp_host, exp_path, exp_port, exp_secure)
