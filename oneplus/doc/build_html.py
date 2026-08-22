# -*- coding: utf-8 -*-
"""把 page_data.json 注入 template.html，输出单文件 HTML"""
import io, json

tpl = io.open("template.html", encoding="utf-8").read()
stats = json.load(io.open("page_data.json", encoding="utf-8"))["stats"]
data = io.open("page_data.json", encoding="utf-8").read()
assert "/*__DATA__*/" in tpl, "模板缺少数据占位符"
data = data.replace("</", "<\\/")  # 防止 JSON 内出现 </script>
tpl = tpl.replace("__N_DEFINED__", str(stats["defined"])).replace("__N_USED__", str(stats["inUse"]))
out = tpl.replace("/*__DATA__*/", data)
io.open("淘宝接口脉络.html", "w", encoding="utf-8").write(out)
print("输出: 淘宝接口脉络.html  大小: %.0f KB" % (len(out.encode('utf-8')) / 1024))
