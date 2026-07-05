import requests
import json
import os
from dotenv import load_dotenv

import concurrent.futures

# 加载 .env 文件（如果存在）
load_dotenv()

# ========== 智谱AI配置 ==========
ZHIPU_API_KEY = os.getenv("ZHIPU_API_KEY", "")
if not ZHIPU_API_KEY:
    print("警告: 未设置 ZHIPU_API_KEY 环境变量，AI 食谱将回退到本地备选")
ZHIPU_URL = "https://open.bigmodel.cn/api/paas/v4/chat/completions"
ZHIPU_MODEL = "glm-4-flash"

# ========== CogView 配置 ==========
COGVIEW_URL = "https://open.bigmodel.cn/api/paas/v4/images/generations"
COGVIEW_MODEL = "cogview-3-flash"

# 本地备选食谱（API失败时用）
LOCAL_RECIPES = {
    "banana": [{"name": "香蕉煎饼", "ingredients": ["banana"], "steps": "香蕉捣泥，加面粉和鸡蛋调成糊，平底锅刷油，小火煎至两面金黄"}],
    "egg": [{"name": "香煎荷包蛋", "ingredients": ["egg"], "steps": "热锅凉油，打入鸡蛋，小火煎至蛋白凝固、蛋黄微流，撒少许盐出锅"}],
    "tomato": [{"name": "糖拌番茄", "ingredients": ["tomato"], "steps": "番茄洗净切块，撒上白糖，冷藏10分钟后食用，酸甜开胃"}],
    "green_vegetable": [{"name": "蒜蓉炒时蔬", "ingredients": ["green_vegetable"], "steps": "蒜切末，热锅凉油爆香蒜末，下蔬菜大火快炒，加盐调味，炒至断生出锅"}],
    "fanta_orange": [{"name": "橙香鸡翅", "ingredients": ["fanta_orange"], "steps": "鸡翅划刀，用芬达橙味汽水加生抽腌制30分钟，入锅小火收汁至浓稠"}],
    "ice_cream": [{"name": "炸冰淇淋", "ingredients": ["ice_cream"], "steps": "冰淇淋切块，裹上面包糠，冰箱冷冻定型后，油温180度快炸10秒，外热内冷"}],
    "banana,egg": [{"name": "香蕉鸡蛋饼", "ingredients": ["banana", "egg"], "steps": "香蕉捣泥，加入鸡蛋和少许面粉搅匀，平底锅小火煎至两面金黄"}],
    "egg,tomato": [{"name": "番茄炒蛋", "ingredients": ["egg", "tomato"], "steps": "番茄切块，鸡蛋打散炒熟盛出；再炒番茄出汁，倒入鸡蛋，加盐糖调味，撒葱花出锅"}],
    "default": [{"name": "家常什锦小炒", "ingredients": ["vegetables"], "steps": "食材洗净切块，热锅凉油，大火快炒，加盐和少许生抽调味，炒至断生出锅"}]
}


def generate_image_by_cogview(recipe_name, ingredients):
    """
    调用 CogView 生成菜品图片，失败返回 None
    """
    if not ZHIPU_API_KEY:
        return None

    ingredients_str = "、".join(ingredients)
    prompt = f"{recipe_name}，包含食材：{ingredients_str}，中式家常菜，美食摄影，高清"

    try:
        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {ZHIPU_API_KEY}"
        }
        payload = {
            "model": COGVIEW_MODEL,
            "prompt": prompt
        }

        r = requests.post(COGVIEW_URL, headers=headers, json=payload, timeout=60)
        result = r.json()

        if "data" in result and len(result["data"]) > 0:
            image_url = result["data"][0].get("url")
            if image_url:
                print(f"CogView 生成图片: {recipe_name} -> {image_url}")
                return image_url
        if "error" in result:
            print(f"CogView 错误: {result['error']}")
        return None
    except Exception as e:
        print(f"图片生成失败: {e}")
        return None


# ========== 关键：函数名必须是 generate_by_ai（与app.py一致）==========
def generate_by_ai(ingredients):
    """
    调用智谱AI生成食谱，失败时回退到本地备选
    """
    if not ingredients:
        return LOCAL_RECIPES["default"]

    prompt = f"""你是一位擅长中式家常菜的大厨。请基于以下食材推荐最多3道适合中国家庭日常制作的菜品：{', '.join(ingredients)}。

【食材分类与搭配规则】
1. 蔬菜类（如番茄、青菜）：可热炒、炖汤、凉拌
2. 蛋类（如鸡蛋）：可煎、炒、蒸、做汤
3. 水果类（如香蕉、橙子）：仅限做甜品、冷盘、果汁、煎饼，禁止入热炒菜或热汤
4. 饮料/饮品（如芬达橙味汽水）：禁止直接作为烹饪食材，除非明确是"橙香鸡翅"类特殊菜（需注明是橙味汽水腌制，不是直接炒）
5. 搭配必须符合中式烹饪常识：水果不炒蔬菜，饮料不炖肉，香蕉不做汤

【禁止生成的菜品类型】
- 水果入热菜：如"橙汁炒青菜"、"香蕉炒蛋"
- 水果入热汤：如"香蕉蛋花汤"
- 饮料直接做菜：如"芬达炖肉"
- 西式冷盘混搭：如"蔬菜沙拉配芬达酱"

【允许的搭配示例】
- 番茄+鸡蛋 → 番茄炒蛋
- 鸡蛋+青菜 → 蒜蓉炒蛋、蛋花汤
- 香蕉 → 香蕉煎饼、拔丝香蕉（甜品）
- 番茄+青菜 → 番茄炒时蔬
- 橙味汽水 → 橙香鸡翅（腌制用，非直接烹饪）

【要求】
1. 必须是中式家常菜（炒、煎、炖、蒸、凉拌）
2. 菜名使用中文，口味偏家常
3. 步骤使用中式烹饪术语（热锅凉油、大火爆炒、小火慢炖、出锅撒葱花）
4. ingredients 字段必须列出全部食材，包括为丰富菜品补充的库存外食材
5. 返回严格JSON数组格式

输出格式：[{{"name": "中文菜名", "ingredients": ["材料1", "材料2"], "steps": "中式烹饪步骤"}},...]"""

    try:
        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {ZHIPU_API_KEY}"
        }
        payload = {
            "model": ZHIPU_MODEL,
            "messages": [{"role": "user", "content": prompt}],
            "temperature": 0.7
        }

        r = requests.post(ZHIPU_URL, headers=headers, json=payload, timeout=20)
        result = r.json()

        if "error" in result:
            raise Exception(result['error'].get('message', 'API error'))

        if "choices" in result and len(result["choices"]) > 0:
            ai_text = result["choices"][0]["message"]["content"]

            # 清理markdown
            if "```json" in ai_text:
                ai_text = ai_text.split("```json")[1].split("```")[0]
            elif "```" in ai_text:
                ai_text = ai_text.split("```")[1].split("```")[0]

            recipes = json.loads(ai_text.strip())
            if isinstance(recipes, dict):
                recipes = [recipes]
            elif not isinstance(recipes, list):
                recipes = []
            if recipes:
                # 为每个食谱并行生成配图
                with concurrent.futures.ThreadPoolExecutor(max_workers=3) as executor:
                    future_map = {}
                    for recipe in recipes:
                        if isinstance(recipe, dict) and "name" in recipe and "ingredients" in recipe:
                            future = executor.submit(generate_image_by_cogview, recipe["name"], recipe["ingredients"])
                            future_map[future] = recipe

                    for future in concurrent.futures.as_completed(future_map):
                        recipe = future_map[future]
                        try:
                            image_url = future.result(timeout=65)
                            recipe["image_url"] = image_url
                        except Exception as e:
                            print(f"图片生成失败 ({recipe.get('name')}): {e}")
                            recipe["image_url"] = None
                print(f"智谱AI生成食谱: {recipes[0]['name']} 等 {len(recipes)} 个")
            return recipes[:3]  # 上限3个
        else:
            raise Exception("Format error")

    except Exception as e:
        print(f"AI调用失败，使用本地备选: {e}")
        # 降级到本地
        key = ",".join(sorted(ingredients[:2]))
        if key in LOCAL_RECIPES:
            return LOCAL_RECIPES[key]
        elif ingredients[0] in LOCAL_RECIPES:
            return LOCAL_RECIPES[ingredients[0]]
        else:
            return LOCAL_RECIPES["default"]


if __name__ == "__main__":
    print("测试中式AI食谱...")
    print(generate_by_ai(["egg", "tomato", "green_vegetable"]))
