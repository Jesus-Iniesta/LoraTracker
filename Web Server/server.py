from http.server import BaseHTTPRequestHandler, HTTPServer
import urllib.parse

class GPSHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed_path = urllib.parse.urlparse(self.path)
        if parsed_path.path == '/coords':
            query = urllib.parse.parse_qs(parsed_path.query)
            lat = query.get('lat', [''])[0]
            lon = query.get('lon', [''])[0]
            print(f"Coordenadas recibidas: Lat={lat}, Lon={lon}")
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"OK")
        else:
            self.send_response(404)
            self.end_headers()

ip = "192.168.137.1"
port = 5000
server = HTTPServer((ip, port), GPSHandler)
print(f"Servidor web corriendo en http://{ip}:{port}")
server.serve_forever()
