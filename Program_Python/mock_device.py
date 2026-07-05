import requests
import time

SERVER = "http://8.133.22.44:8090"
DEVICE_ID = "fridge_01"


def register_device():
    try:
        r = requests.post(f"{SERVER}/api/device/register",
                          json={"device_id": DEVICE_ID, "name": "客厅冰箱"})
        print(f"✓ 设备注册: {r.json()}")
    except Exception as e:
        print(f"✗ 注册失败: {e}")


def send_heartbeat():
    try:
        r = requests.post(f"{SERVER}/api/device/heartbeat",
                          json={"device_id": DEVICE_ID})
        print(f"✓ 心跳: {r.json()['status']}")
    except Exception as e:
        print(f"✗ 心跳失败: {e}")


def send_data(items):
    data = {
        "device_id": DEVICE_ID,
        "items": items
    }
    try:
        r = requests.post(f"{SERVER}/api/update", json=data)
        result = r.json()
        print(f"✓ 上报成功: {result['count']} 种食材")
        if result.get('alerts'):
            print(f"  ⚠️ 触发提醒: {result['alerts']}")
        return result
    except Exception as e:
        print(f"✗ 上报失败: {e}")
        return None


def check_inventory():
    try:
        r = requests.get(f"{SERVER}/api/inventory", params={"device_id": DEVICE_ID})
        items = r.json()
        print(f"\n📦 当前库存 ({len(items)} 种):")
        for item in items:
            print(f"  - {item['name']}: {item['count']}个 ({item['category']})")
    except Exception as e:
        print(f"✗ 查询失败: {e}")


def check_reminders():
    try:
        r = requests.get(f"{SERVER}/api/reminders", params={"device_id": DEVICE_ID})
        data = r.json()

        print(f"\n🔔 提醒:")
        if data['alerts']:
            for a in data['alerts']:
                print(f"  ⚠️ {a['message']}")
        else:
            print("  ✅ 无提醒")

        print(f"\n👨‍🍳 AI 推荐食谱 ({len(data['recipes'])} 个):")
        if data['recipes']:
            for idx, recipe in enumerate(data['recipes'], 1):
                print(f"  {idx}. {recipe['name']}")
        else:
            print("  暂无推荐")

    except Exception as e:
        print(f"✗ 查询失败: {e}")


def check_mobile():
    try:
        r = requests.get(f"{SERVER}/mobile")
        if r.status_code == 200:
            print(f"\n📱 移动端页面: OK")
        else:
            print(f"✗ 移动端页面返回 {r.status_code}")
    except Exception as e:
        print(f"✗ 移动端访问失败: {e}")


if __name__ == "__main__":
    print("=" * 50)
    print("  智能冰箱 Mock 设备测试")
    print("=" * 50)

    register_device()
    time.sleep(0.3)
    send_heartbeat()

    print("\n" + "-" * 50)
    print("【第一轮】库存不足场景")
    print("-" * 50)
    items_round1 = [
        {"category": "egg", "name": "egg", "count": 1},
        {"category": "fruit", "name": "apple", "count": 1},
        {"category": "dairy", "name": "milk", "count": 0},
        {"category": "meat", "name": "beef", "count": 2},
        {"category": "vegetable", "name": "carrot", "count": 5},
        {"category": "vegetable", "name": "tomato", "count": 2},
        {"category": "staple", "name": "potato", "count": 8},
        {"category": "meat", "name": "chicken", "count": 3},
    ]
    send_data(items_round1)
    time.sleep(1)
    check_inventory()
    check_reminders()
    check_mobile()

    print("\n" + "-" * 50)
    print("【第二轮】补货后充足场景")
    print("-" * 50)
    items_round2 = [
        {"category": "egg", "name": "egg", "count": 10},
        {"category": "fruit", "name": "apple", "count": 5},
        {"category": "dairy", "name": "milk", "count": 2},
        {"category": "meat", "name": "beef", "count": 3},
        {"category": "vegetable", "name": "carrot", "count": 6},
        {"category": "vegetable", "name": "tomato", "count": 4},
        {"category": "staple", "name": "potato", "count": 10},
        {"category": "meat", "name": "chicken", "count": 4},
    ]
    send_data(items_round2)
    time.sleep(1)
    check_inventory()
    check_reminders()
    check_mobile()

    print("\n" + "=" * 50)
    print("  测试结束")
    print("=" * 50)
    print(f"\n📱 浏览器访问: {SERVER}/mobile")