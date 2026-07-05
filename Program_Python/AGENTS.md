# 智能冰箱云服务 - 项目说明

本文档用于快速了解本项目架构、代码结构和运行方式，方便后续维护与扩展。

---

## 一、项目背景

这是一个**智能冰箱云端管理系统**，核心功能：
- 接收硬件板子（真实设备或模拟器）上报的食材库存数据
- 管理库存、检测低库存并生成补货提醒
- 调用智谱 AI（glm-4-flash）根据现有食材推荐食谱
- 提供移动端管理页面（`/mobile`）实时查看库存、提醒和食谱

---

## 二、技术栈

| 组件 | 技术 |
|------|------|
| 后端框架 | Flask + Flask-SQLAlchemy |
| 数据库 | SQLite（`instance/fridge.db`） |
| AI 食谱 | 智谱 AI API（`glm-4-flash`），失败时回退本地备选食谱 |
| 前端 | 纯 HTML + CSS + JS（Jinja2 模板渲染） |
| 配置管理 | `.env` 文件（`python-dotenv` 加载） |
| 运行环境 | Python 3.x，Windows（当前开发环境） |

---

## 三、文件结构

```
Program_Python/
├── app.py                 # Flask 主服务端（所有 API + 页面）
├── ai_recipe.py           # AI 食谱生成模块（调用智谱 API）
├── mock_device.py         # 模拟硬件设备（调试/测试用，真实部署时不需要）
├── templates/
│   └── mobile.html        # 移动端管理页面
├── instance/
│   └── fridge.db          # SQLite 数据库（运行后自动生成）
├── .env                   # 环境变量（API Key，不提交到 Git）
├── requirements.txt       # Python 依赖
├── .gitignore             # Git 忽略配置
└── .venv/                 # Python 虚拟环境
```

---

## 四、数据库模型

### Inventory（库存表）
| 字段 | 说明 |
|------|------|
| id | 主键 |
| name | 食材名称（如 egg, apple） |
| category | 分类（如 fruit, egg, meat） |
| count | 数量 |
| **device_id** | 所属设备 ID（支持多设备隔离） |

### Alert（提醒表）
| 字段 | 说明 |
|------|------|
| id | 主键 |
| item_name | 食材名称 |
| message | 提醒内容 |
| is_sent | 是否已推送（板子接口会标记为已读） |
| **device_id** | 所属设备 ID |

### Device（设备表）
| 字段 | 说明 |
|------|------|
| id | 主键 |
| device_id | 设备唯一标识（如 `fridge_01`） |
| name | 设备名称 |
| last_seen | 最后心跳时间 |
| status | online / offline |

### History（历史记录表）
| 字段 | 说明 |
|------|------|
| id | 主键 |
| timestamp | 记录时间 |
| snapshot | 上报的数据快照（JSON 字符串） |
| device_id | 设备 ID |

---

## 五、API 接口清单

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/update` | POST | 板子上报库存数据（核心接口） |
| `/api/inventory` | GET | 查看库存，支持 `?device_id=xxx` |
| `/api/reminders` | GET | 查看提醒+食谱，支持 `?device_id=xxx` |
| `/api/last_update` | GET | 查询设备最后更新时间（网页端轮询用） |
| `/api/devices` | GET | 查看所有设备状态 |
| `/api/device/register` | POST | 设备注册 |
| `/api/device/heartbeat` | POST | 设备心跳 |
| `/api/clear_alerts` | POST | 一键清除提醒 |
| `/mobile` | GET | 移动端管理页面 |
| `/` | GET | 服务状态提示 |

### 板子上报数据格式（`/api/update`）
```json
{
  "device_id": "fridge_01",
  "items": [
    {"category": "fruit", "name": "apple", "count": 3},
    {"category": "egg", "name": "egg", "count": 1}
  ]
}
```

---

## 六、关键业务逻辑

### 1. 补货阈值（`THRESHOLD`）
```python
THRESHOLD = {
    "egg": 3,
    "apple": 2,
    "milk": 1
}
```
- 默认阈值为 1（未在字典中的食材）
- 低于阈值时生成 Alert 提醒
- **前端颜色判断也基于这个字典**（不要写死）

### 2. 多设备隔离
- `Inventory` 和 `Alert` 都有 `device_id` 字段
- 各设备库存、提醒互不干扰
- 食谱缓存 key 格式：`"{device_id}:{ingredients}"`

### 3. 设备离线检测
- 心跳超时 5 分钟视为 offline
- 后台守护线程每 60 秒自动扫描一次
- 查询 `/api/devices` 时也会实时标记

### 4. 网页刷新机制（被动刷新）
- **不是定时刷新**
- 板子调用 `/api/update` 后，服务端记录 `device_last_update[device_id]`
- 网页端每 3 秒轮询 `/api/last_update`
- **只有时间戳变了才刷新页面**
- 如果服务端重启，会从 `History` 表兜底查询

### 5. AI 食谱生成
- 优先调用智谱 AI API
- API 失败/超时时回退到 `LOCAL_RECIPES` 本地备选
- 需要设置环境变量 `ZHIPU_API_KEY`（已在 `.env` 中配置）

---

## 七、运行方式

### 首次运行（或表结构变更后）
如果数据库表结构有变化，需要删除旧数据库重建：
```powershell
Remove-Item instance\fridge.db
```

### 启动服务端
```powershell
# 进入虚拟环境（如未激活）
.venv\Scripts\activate

# 启动服务
python app.py
```

服务启动后访问：
- 移动端页面：`http://127.0.0.1:5000/mobile`
- 带设备过滤：`http://127.0.0.1:5000/mobile?device_id=fridge_01`

### 模拟设备测试（无真实板子时）
```powershell
python mock_device.py
```
这会模拟一个 `fridge_01` 设备，执行：注册 → 心跳 → 上报库存 → 查库存 → 查提醒 → 再心跳 → 列设备。

> **注意**：真实部署时不需要 `mock_device.py`，真实板子会直接通过 HTTP 调用云端接口。

---

## 八、环境变量

在 `.env` 文件中配置：
```env
ZHIPU_API_KEY=你的智谱API密钥
```

> `.env` 已加入 `.gitignore`，不会提交到版本控制。

---

## 九、已知注意事项

1. **Flask debug=True 会启动 reloader 进程**，导致后台离线检测线程被创建两次。启动日志里看到两次"后台离线检测线程已启动"是正常现象。
2. **SQLite 不支持并发写**，如果未来并发量增大，需迁移到 PostgreSQL/MySQL。
3. **`recipe_cache` 是内存缓存**，服务端重启后失效，但下次请求会重新生成。
4. **API Key 不要硬编码**，已通过 `.env` + `python-dotenv` 管理。

---

## 十、最近修改记录

| 时间 | 修改内容 |
|------|---------|
| 2026-04-28 | 增加 `device_id` 隔离（Inventory、Alert） |
| 2026-04-28 | AI Key 改为 `.env` 环境变量管理 |
| 2026-04-28 | 移动端阈值颜色与后端 `THRESHOLD` 一致 |
| 2026-04-28 | 后台自动离线检测线程 |
| 2026-04-28 | 食谱缓存按 `device_id` 隔离 |
| 2026-04-28 | 网页改为被动刷新（板子更新后才刷新） |
| 2026-04-28 | 补充 `mock_device.py` 注册/心跳/设备列表测试 |
| 2026-04-28 | 添加 `requirements.txt` 和 `.gitignore` |

---

## 十一、待扩展方向（可选）

- 增加手动补货/删除库存的 API 和管理页面按钮
- 移动端增加设备切换下拉框
- 全页刷新改为局部 AJAX 更新（体验更丝滑）
- 引入 Flask-Migrate 管理数据库迁移
- 增加用户登录/设备绑定鉴权
- 历史数据图表展示
