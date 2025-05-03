from flask import Flask, request, render_template, jsonify

app = Flask(__name__)

# Variable global para almacenar los datos
received_data = {}

# Ruta para manejar solicitudes GET y POST
@app.route('/', methods=['GET', 'POST'])
def index():
    global received_data
    if request.method == 'POST':
        # Procesar datos enviados por POST
        received_data = request.form.to_dict()
        print(f"Datos recibidos por POST: {received_data}")
    elif request.method == 'GET':
        print(f"Datos recibidos por GET: {request.args.to_dict()}")
    return render_template('index.html', data=received_data)

# Ruta para obtener los datos en formato JSON
@app.route('/data', methods=['GET'])
def get_data():
    return jsonify(received_data)
  
if __name__ == '__main__':
    # Ejecutar el servidor en la IP estática y puerto 5000
    app.run(host='192.168.137.1', port=5000)