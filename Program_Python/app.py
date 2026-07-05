from flask import Flask, request, jsonify, render_template
from flask_sqlalchemy import SQLAlchemy
from datetime import datetime, timedelta
import os
import threading
import time
from ai_recipe import generate_by_ai

app = Flask(__name__)

# ========== 数据库配置（改MySQL）==========
basedir = os.path.abspath(os.path.dirname(__file__))
# 优先从环境变量读取，默认连接新服务器的MySQL
DB_URI = os.getenv('DATABASE_URL', 'mysql+pymysql://fridge_user:Fridge2024!@localhost:3306/embedded_fridge')
app.config['SQLALCHEMY_DATABASE_URI'] = DB_URI
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
db = SQLAlchemy(app)

# ========== 全局缓存（简单方案）==========
recipe_cache = {
    'data': None,
    'timestamp': None,
    'ingredients': None
}

device_last_update = {}

# ========== 数据表 ==========
class Inventory(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    name = db.Column(db.String(50), nullable=False)
    category = db.Column(db.String(20))
    count = db.Column(db.Integer, default=0)
    device_id = db.Column(db.String(50), default='')

    def to_dict(self):
        return {"name": self.name, "category": self.category, "count": self.count, "device_id": self.device_id}


class Alert(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    item_name = db.Column(db.String(50))
    message = db.Column(db.String(200))
    is_sent = db.Column(db.Boolean, default=False)
    device_id = db.Column(db.String(50), default='')


class Device(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    device_id = db.Column(db.String(50), unique=True, nullable=False)
    name = db.Column(db.String(50))
    last_seen = db.Column(db.DateTime, default=datetime.now)
    status = db.Column(db.String(20), default='online')

    def to_dict(self):
        return {
            "device_id": self.device_id,
            "name": self.name,
            "last_seen": self.last_seen.strftime('%Y-%m-%d %H:%M:%S'),
            "status": self.status
        }


class History(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    timestamp = db.Column(db.DateTime, default=datetime.now)
    snapshot = db.Column(db.Text)
    device_id = db.Column(db.String(50))


THRESHOLD = {
    "banana": 3,
    "egg": 3,
    "tomato": 2,
    "green_vegetable": 2,
    "fanta_orange": 1,
    "ice_cream": 2
}


# ========== 接口 1：接收板子数据 ==========
@app.route('/api/update', methods=['POST'])
def receive_data():
    data = request.json

    if not isinstance(data, dict):
        return jsonify({"status": "error", "message": "请求体必须是 JSON 对象"}), 400

    device_id = data.get('device_id')
    if not device_id or not isinstance(device_id, str):
        return jsonify({"status": "error", "message": "缺少 device_id 或格式错误"}), 400

    items = data.get('items')
    if not isinstance(items, list):
        return jsonify({"status": "error", "message": "items 必须是数组"}), 400

    for idx, item in enumerate(items):
        if not isinstance(item, dict):
            return jsonify({"status": "error", "message": f"items[{idx}] 必须是对象"}), 400
        if not all(k in item for k in ('name', 'count', 'category')):
            return jsonify({"status": "error", "message": f"items[{idx}] 缺少 name/count/category 字段"}), 400
        if not isinstance(item['count'], int) or item['count'] < 0:
            return jsonify({"status": "error", "message": f"items[{idx}].count 必须是非负整数"}), 400
        if not isinstance(item['name'], str) or not isinstance(item['category'], str):
            return jsonify({"status": "error", "message": f"items[{idx}] name/category 必须是字符串"}), 400

    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        device = Device(device_id=device_id, name=device_id)
        db.session.add(device)
    device.last_seen = datetime.now()
    device.status = 'online'

    alerts_created = []
    for item in items:
        name = item['name']
        new_count = item['count']

        inv = Inventory.query.filter_by(name=name, device_id=device_id).first()

        if inv:
            old_count = inv.count
            inv.count = new_count
        else:
            old_count = 999
            inv = Inventory(name=name, category=item['category'], count=new_count, device_id=device_id)
            db.session.add(inv)

        limit = THRESHOLD.get(name, 1)
        if new_count < limit and old_count >= limit:
            msg = f"{name}库存不足（仅剩{new_count}个）"
            alert = Alert(item_name=name, message=msg, device_id=device_id)
            db.session.add(alert)
            alerts_created.append(msg)

    history = History(snapshot=str(items), device_id=device_id)
    db.session.add(history)

    db.session.commit()

    global recipe_cache
    recipe_cache['timestamp'] = None

    global device_last_update
    device_last_update[device_id] = datetime.now().isoformat()

    return jsonify({
        "status": "ok",
        "device_id": device_id,
        "count": len(items),
        "alerts": alerts_created,
        "low_stock": [a for a in alerts_created]
    })


# ========== 接口 2：查询补货提醒（不含食谱，避免阻塞板子） ==========
@app.route('/api/reminders', methods=['GET'])
def get_reminders():
    device_id = request.args.get('device_id', '')
    if not device_id:
        first_device = Device.query.first()
        if first_device:
            device_id = first_device.device_id

    if device_id:
        items_all = Inventory.query.filter_by(device_id=device_id).all()
    else:
        items_all = Inventory.query.all()

    alert_list = []
    for item in items_all:
        limit = THRESHOLD.get(item.name, 1)
        if item.count < limit:
            alert_list.append({
                "item": item.name,
                "message": f"{item.name}库存不足（仅剩{item.count}个）"
            })

    return jsonify({"alerts": alert_list})


# ========== 工具函数：按 device_id 获取食谱（带缓存） ==========
def _get_recipes_for_device(device_id):
    if device_id:
        items = Inventory.query.filter(Inventory.count > 0, Inventory.device_id == device_id).all()
    else:
        items = Inventory.query.filter(Inventory.count > 0).all()

    ingredient_names = sorted([i.name for i in items])
    cache_key = ','.join(ingredient_names)

    global recipe_cache
    cache_valid = (
            recipe_cache['data'] and
            recipe_cache['timestamp'] and
            datetime.now() - recipe_cache['timestamp'] < timedelta(minutes=5) and
            recipe_cache['ingredients'] == cache_key
    )

    if cache_valid:
        recipes = recipe_cache['data']
    else:
        recipes = generate_by_ai(ingredient_names[:3]) if ingredient_names else []
        recipe_cache['data'] = recipes
        recipe_cache['timestamp'] = datetime.now()
        recipe_cache['ingredients'] = cache_key

    return recipes


# ========== 接口：查询 AI 食谱（单独接口，不阻塞提醒） ==========
@app.route('/api/recipes', methods=['GET'])
def get_recipes():
    device_id = request.args.get('device_id', '')
    if not device_id:
        first_device = Device.query.first()
        if first_device:
            device_id = first_device.device_id

    recipes = _get_recipes_for_device(device_id)
    return jsonify({
        "recipes": [
            {
                "name": r.get("name", "未知菜谱"),
                "image_url": r.get("image_url")
            } for r in recipes
        ] if recipes else []
    })


# ========== 接口：查询最后更新时间 ==========
@app.route('/api/last_update', methods=['GET'])
def last_update():
    device_id = request.args.get('device_id', '')
    if device_id:
        last_history = History.query.filter_by(device_id=device_id).order_by(History.timestamp.desc()).first()
    else:
        last_history = History.query.order_by(History.timestamp.desc()).first()
    if last_history:
        update_time = last_history.timestamp.strftime('%Y-%m-%d %H:%M:%S')
    else:
        update_time = None
    return jsonify({
        "last_update": update_time
    })


# ========== 接口 3：查看库存 ==========
@app.route('/api/inventory', methods=['GET'])
def get_inventory():
    device_id = request.args.get('device_id', '')
    if device_id:
        items = Inventory.query.filter_by(device_id=device_id).all()
    else:
        items = Inventory.query.all()
    return jsonify([i.to_dict() for i in items])


@app.route('/')
def index():
    return "智能冰箱云服务运行中（含AI食谱）"


# ========== 移动端页面 ==========
@app.route('/mobile')
def mobile_dashboard():
    device_id = request.args.get('device_id', '')
    if not device_id:
        first_device = Device.query.first()
        if first_device:
            device_id = first_device.device_id

    if device_id:
        items = Inventory.query.filter_by(device_id=device_id).all()
    else:
        items = Inventory.query.all()

    alerts = []
    for item in items:
        limit = THRESHOLD.get(item.name, 1)
        if item.count < limit:
            alerts.append({
                'item_name': item.name,
                'count': item.count,
                'message': f"{item.name}库存不足（仅剩{item.count}个）"
            })

    recipes = _get_recipes_for_device(device_id)

    if device_id:
        last_history = History.query.filter_by(device_id=device_id).order_by(History.timestamp.desc()).first()
    else:
        last_history = History.query.order_by(History.timestamp.desc()).first()
    latest_update_time = None
    if last_history:
        latest_update_time = last_history.timestamp.strftime('%H:%M:%S')

    return render_template('mobile.html',
                           items=items,
                           alerts=alerts,
                           recipes=recipes,
                           thresholds=THRESHOLD,
                           device_id=device_id,
                           timestamp=latest_update_time or '')


# ========== 设备管理接口 ==========
@app.route('/api/device/register', methods=['POST'])
def register_device():
    data = request.json or {}
    device_id = data.get('device_id')
    if not device_id:
        return jsonify({"status": "error", "message": "缺少 device_id"}), 400

    device = Device.query.filter_by(device_id=device_id).first()
    if device:
        device.status = 'online'
        device.last_seen = datetime.now()
    else:
        device = Device(device_id=device_id, name=data.get('name', device_id))
        db.session.add(device)

    db.session.commit()
    return jsonify({"status": "ok", "device_id": device_id, "message": "设备已注册"})


@app.route('/api/device/heartbeat', methods=['POST'])
def device_heartbeat():
    data = request.json or {}
    device_id = data.get('device_id')
    if not device_id:
        return jsonify({"status": "error", "message": "缺少 device_id"}), 400

    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        return jsonify({"status": "error", "message": "设备未注册"}), 404

    device.last_seen = datetime.now()
    device.status = 'online'
    db.session.commit()
    return jsonify({"status": "ok", "device_id": device_id})


@app.route('/api/devices', methods=['GET'])
def get_devices():
    devices = Device.query.all()
    timeout = datetime.now() - timedelta(minutes=5)
    for d in devices:
        if d.last_seen < timeout and d.status == 'online':
            d.status = 'offline'
    db.session.commit()
    return jsonify([d.to_dict() for d in devices])


@app.route('/api/clear_alerts', methods=['POST'])
def clear_alerts():
    Alert.query.update({Alert.is_sent: True})
    db.session.commit()
    return jsonify({"status": "ok"})


def auto_check_offline():
    while True:
        time.sleep(60)
        try:
            with app.app_context():
                timeout = datetime.now() - timedelta(minutes=5)
                devices = Device.query.filter(Device.status == 'online').all()
                offline_count = 0
                for d in devices:
                    if d.last_seen < timeout:
                        d.status = 'offline'
                        offline_count += 1
                db.session.commit()
                if offline_count:
                    print(f"[{datetime.now().strftime('%H:%M:%S')}] {offline_count} 个设备已标记为离线")
        except Exception as e:
            print(f"离线检测异常: {e}")


if __name__ == '__main__':
    if not os.path.exists('instance'):
        os.makedirs('instance')
    with app.app_context():
        db.create_all()
        print("数据库已初始化")

    offline_thread = threading.Thread(target=auto_check_offline, daemon=True)
    offline_thread.start()
    print("后台离线检测线程已启动")

    port = int(os.getenv('PORT', 8080))
    app.run(host='0.0.0.0', port=port, debug=False)