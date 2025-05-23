from flask import Flask, request, render_template, jsonify, redirect, url_for
from flask_sqlalchemy import SQLAlchemy

app = Flask(__name__)
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///coordenadas.db'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
db = SQLAlchemy(app)

class Coordenada(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    latitud = db.Column(db.Float, nullable=False)
    longitud = db.Column(db.Float, nullable=False)

with app.app_context():
    db.create_all()

@app.route('/', methods=['GET', 'POST'])
def index():
    if request.method == 'POST':
        lat = request.form.get('latitud') or request.form.get('latitude')
        lon = request.form.get('longitud') or request.form.get('longitude')
        if lat and lon:
            nueva_coordenada = Coordenada(latitud=float(lat), longitud=float(lon))
            db.session.add(nueva_coordenada)
            db.session.commit()
        return 'OK', 200
    return render_template('index.html')

@app.route('/data', methods=['GET'])
def get_data():
    coordenadas = Coordenada.query.all()
    data = [{'latitud': c.latitud, 'longitud': c.longitud} for c in coordenadas]
    return jsonify(data)

if __name__ == '__main__':
    app.run(host='192.168.137.1', port=5000)