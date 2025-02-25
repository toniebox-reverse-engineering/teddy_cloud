import http.client
import time
import struct
import json

def is_json(data):
    try:
        json.loads(data.decode('utf-8'))
        return True
    except (json.JSONDecodeError, UnicodeDecodeError):
        return False

def is_png(data):
    return data[:8] == b'\x89PNG\r\n\x1a\n'

def send_requests(host, port, request_pairs, iterations):
    first = True
    connection = http.client.HTTPConnection(host, port)
    stats = {"success": 0, "fail": 0, "min_delay": float('inf'), "max_delay": 0, "total_delay": 0, "min_rate": float('inf'), "max_rate": 0, "total_rate": 0, "total_requests": 0}
    start_time = time.time()
    
    for count in range(iterations):
        for path, check_response in request_pairs:
            try:
                request_start = time.time()
                connection.putrequest("GET", path, skip_host=True, skip_accept_encoding=True)
                connection.putheader("Connection", "Keep-Alive")
                connection.putheader("Host", f"{host}:{port}")
                connection.putheader("User-Agent", "ApacheBench/2.3")
                connection.putheader("Accept", "*/*")
                connection.endheaders()
                
                response = connection.getresponse()
                response_time = (time.time() - request_start) * 1e6  # Convert to microseconds
                body = response.read()
                body_size = len(body)
                transfer_rate = (body_size / (response_time / 1e6)) / (1024 * 1024) if response_time > 0 else 0  # Convert to MiB/s
                
                stats["min_delay"] = min(stats["min_delay"], response_time)
                stats["max_delay"] = max(stats["max_delay"], response_time)
                stats["total_delay"] += response_time
                
                stats["min_rate"] = min(stats["min_rate"], transfer_rate)
                stats["max_rate"] = max(stats["max_rate"], transfer_rate)
                stats["total_rate"] += transfer_rate
                
                stats["total_requests"] += 1
                
                if check_response(body):
                    stats["success"] += 1
                else:
                    stats["fail"] += 1
                
                if time.time() - start_time >= 1:
                    avg_delay = stats["total_delay"] / stats["total_requests"] if stats["total_requests"] > 0 else 0
                    avg_rate = stats["total_rate"] / stats["total_requests"] if stats["total_requests"] > 0 else 0
                    if not first:
                        print("\033[3A\033[K", end="")  # Move up 3 lines and clear them
                    first = False
                    print(f"Success: {stats['success']}, Failed: {stats['fail']}")
                    print(f"  [[ Delay (avg/min/max) [us]:   {avg_delay:.2f} / {stats['min_delay']:.2f} / {stats['max_delay']:.2f} ]]")
                    print(f"  [[ Rate (avg/min/max) [MiB/s]: {avg_rate:.2f} / {stats['min_rate']:.2f} / {stats['max_rate']:.2f} ]]")
                    start_time = time.time()
                
            except Exception as e:
                print(f"Error on request to {path}: {e}")
                break
    
    connection.close()

# Usage
request_pairs = [
    ("/api/settings/get/frontend.split_model_content", lambda response: response.strip() == b"true"),
    ("/web/assets/logo-aw46LCqE.png", is_png),
    ("/img_unknown.png", is_png),
    #("/api/tonieboxesJson", is_json),
    ("/api/getBoxes", is_json),
    ("/api/getTagIndex", is_json),
    ("/api/stats", is_json)
]
send_requests("localhost", 8080, request_pairs, 100000)
