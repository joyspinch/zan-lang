(function(){
'use strict';
/* admin SPA 外壳：Naive UI 出深色侧栏 + 分组菜单 + 头部，内容区是 iframe
   承载服务端渲染的管理屏（views/Admin/*）。选择 iframe 而非逐屏重写：
   服务端页面已带完整的表格/表单/标签页交互（admin.css + admin.js），
   外壳只做导航；每个菜单项都真实可达，页内一切功能开箱即用。
   hash 路由：#/ = 仪表盘（壳内自绘 + zan-charts），#r=<path> = iframe 打开
   服务端屏；未登录时整壳换成登录卡片。菜单来自 /api/admin/menu（服务端
   权限过滤后的唯一事实），分组标题用 MenuBuilder 注册的中文标签。 */

const { createApp, ref, computed, onMounted } = Vue;
const { createDiscreteApi } = naive;

/* 离散 API：不依赖 app 上下文，任意 setup 里可直接调用。
   必须延迟到 body 就绪后创建——同步执行时 body 可能尚未解析完成，
   createDiscreteApi 内部挂载 provider 会因 body 为 null 抛错，导致整个脚本失效。 */
let message;

/* 菜单分组标签：与服务端 MenuBuilder.Section 注册保持同序（壳内兜底，
   服务端注册后 group 字段即中文，此处映射仅给未注册段兜底）。 */
const GROUP_LABELS = {
  "": "概览", content: "内容管理", monitor: "运行监控",
  system: "系统管理", dev: "开发工具"
};

/* ---- fetch 封装：统一 {code,msg,data} 信封 ---- */
async function api(path, opts = {}) {
  const headers = Object.assign({ "Accept": "application/json" }, opts.headers || {});
  if (opts.body && !(opts.body instanceof FormData)) {
    headers["Content-Type"] = "application/x-www-form-urlencoded";
  }
  const res = await fetch(path, Object.assign({}, opts, { headers, credentials: "same-origin" }));
  let envelope = null;
  try { envelope = await res.json(); } catch (e) { /* 非 JSON（如登录页 HTML）*/ }
  if (!res.ok) {
    const err = new Error(envelope && envelope.msg ? envelope.msg : res.status + " " + res.statusText);
    err.code = envelope ? envelope.code : "";
    err.status = res.status;
    throw err;
  }
  if (envelope && envelope.code !== "0000") {
    const err = new Error(envelope.msg || envelope.code);
    err.code = envelope.code;
    err.status = res.status;
    throw err;
  }
  return envelope ? envelope.data : null;
}

function formBody(obj) {
  const p = new URLSearchParams();
  Object.keys(obj).forEach(k => {
    if (obj[k] !== undefined && obj[k] !== null) { p.append(k, obj[k]); }
  });
  return p.toString();
}

/* ---- 登录页 ---- */
const LoginView = {
  setup() {
    const form = reactive({ user: "admin", pass: "" });
    const busy = ref(false);
    async function submit() {
      if (!form.pass) { message.warning("请输入密码"); return; }
      busy.value = true;
      try {
        await api("/api/auth/login", { method: "POST", body: formBody(form) });
        window.dispatchEvent(new Event("zan:login"));
      } catch (e) {
        message.error(e.message);
      } finally { busy.value = false; }
    }
    return { form, busy, submit };
  },
  template: `
  <div style="height:100%;display:flex;align-items:center;justify-content:center;background:#1d2430">
    <n-card title="管理后台" style="width:360px" :bordered="true" size="large">
      <n-form @keyup.enter="submit">
        <n-form-item label="账号">
          <n-input v-model:value="form.user" placeholder="" :input-props="{autocomplete:'username'}" size="large"></n-input>
        </n-form-item>
        <n-form-item label="密码">
          <n-input v-model:value="form.pass" type="password" show-password-on="click"
                   placeholder="" :input-props="{autocomplete:'current-password'}" size="large"></n-input>
        </n-form-item>
        <n-button type="primary" block size="large" :loading="busy" @click="submit">登 录</n-button>
      </n-form>
    </n-card>
  </div>`
};

/* ---- 仪表盘（壳内自绘的唯一一屏：摘要卡 + zan-charts 请求/错误时序） ---- */
const DashboardView = {
  setup() {
    const sum = ref(null);
    onMounted(async () => {
      try { sum.value = await api("/api/admin/summary"); }
      catch (e) { message.error(e.message); }
    });
    const cards = computed(() => sum.value ? [
      { label: "请求总数", value: sum.value.requests },
      { label: "错误数", value: sum.value.errors },
      { label: "平均延迟", value: (sum.value.avgUs / 1000).toFixed(1) + " ms" }
    ] : []);
    const blogCards = computed(() => sum.value ? [
      { label: "账号", value: sum.value.users },
      { label: "文章", value: sum.value.posts },
      { label: "已发布", value: sum.value.published },
      { label: "评论", value: sum.value.comments },
      { label: "待审", value: sum.value.pending }
    ] : []);
    const uptime = computed(() => sum.value ? ("运行时长：" + sum.value.uptime) : "");
    return { cards, blogCards, uptime };
  },
  mounted() {
    this.$nextTick(() => this.drawChart());
  },
  methods: {
    async drawChart() {
      if (!window.ZanCharts || !window.ZanCharts.createChart) { return; }
      const host = document.getElementById("chart-traffic");
      if (!host) { return; }
      let data = [];
      try {
        const s = await api("/api/admin/monitor/series?sec=60");
        data = (s.points || []).map(p => ({
          t: (p.t <= 0 ? (p.t + s.count) + "s" : "now"),
          requests: p.req, errors: p.err
        }));
      } catch (e) { /* 无端点时退化为空图 */ }
      if (data.length === 0) {
        data = [{ t: "当前", requests: 0, errors: 0 }];
      }
      window.ZanCharts.createChart(host, {
        type: "combo",
        title: "",
        categoryField: "t",
        series: [
          { field: "requests", label: "请求", type: "bar" },
          { field: "errors", label: "错误", type: "line", yAxisIndex: 1 }
        ],
        data,
        yAxes: [{ name: "请求" }, { name: "错误" }],
        tooltip: { shared: true }
      });
    }
  },
  template: `
  <div>
    <n-grid :cols="3" :x-gap="14">
      <n-gi v-for="c in cards" :key="c.label">
        <n-card size="small">
          <n-statistic :label="c.label" :value="c.value"></n-statistic>
        </n-card>
      </n-gi>
    </n-grid>
    <n-grid :cols="5" :x-gap="14" style="margin-top:14px">
      <n-gi v-for="c in blogCards" :key="c.label">
        <n-card size="small">
          <n-statistic :label="c.label" :value="c.value"></n-statistic>
        </n-card>
      </n-gi>
    </n-grid>
    <n-card size="small" title="实时流量（近 1 分钟）" style="margin-top:14px">
      <template #header-extra><n-text depth="3">{{ uptime }}</n-text></template>
      <div id="chart-traffic" style="width:100%;height:280px"></div>
    </n-card>
  </div>`
};

/* ---- 根组件：深色侧栏 + 分组菜单 + iframe 内容区 ---- */
const App = {
  setup() {
    const route = ref(location.hash || "#/");
    const me = ref(null);
    const menu = ref([]);
    const iframeSrc = ref("");
    const contentTitle = ref("仪表盘");

    window.addEventListener("hashchange", () => {
      route.value = location.hash || "#/";
      sync();
    });
    // 登录页与外壳是两个组件，登录成功后由 LoginView 广播重进
    window.addEventListener("zan:login", () => { boot(); });

    async function boot() {
      try {
        me.value = await api("/api/auth/me");
        await loadMenu();
        sync();
      } catch (e) {
        me.value = null;   // 未登录 → 登录页
      }
    }

    async function loadMenu() {
      try { menu.value = await api("/api/admin/menu"); }
      catch (e) { menu.value = []; }
    }

    async function logout() {
      // 服务端只有 GET /admin/logout（重定向语义），SPA 用 GET 调它清 cookie。
      try { await api("/admin/logout"); } catch (e) { /* 302 非 JSON，忽略 */ }
      me.value = null;
      menu.value = [];
      iframeSrc.value = "";
      location.hash = "#/";
    }

    /* hash → { page, path }：#/ 是壳内仪表盘，#r=<path> 是 iframe 屏 */
    const current = computed(() => {
      const h = route.value;
      if (h.indexOf("#r=") === 0) { return { page: "frame", path: h.slice(3) }; }
      return { page: "dashboard", path: "" };
    });

    /* 把当前 hash 同步到 iframe/标题/高亮。菜单项一个不落：菜单里的每条
       path 都直接作为 iframe 的 src——服务端屏自己带全套交互。 */
    function sync() {
      const c = current.value;
      if (c.page !== "frame") {
        iframeSrc.value = "";
        contentTitle.value = "仪表盘";
        return;
      }
      const hit = menu.value.find(m => m.path === c.path);
      contentTitle.value = hit ? hit.title : c.path;
      // 同址不去重赋值：服务端屏无外部状态，重赋即刷新面板
      iframeSrc.value = c.path;
    }

    /* 菜单分组：服务端 group 字段（URL 首段）→ 中文标签；段内保持服务端
       给出的顺序。无组的项（仪表盘、个人资料）排在最前。 */
    const menuOptions = computed(() => {
      const groups = [];
      const byKey = {};
      for (let i = 0; i < menu.value.length; i = i + 1) {
        const m = menu.value[i];
        const g = m.group || "";
        if (!byKey.hasOwnProperty("g:" + g)) {
          byKey["g:" + g] = {
            type: "group", label: GROUP_LABELS[g] || g || "概览",
            key: "g:" + g, children: []
          };
          groups.push(byKey["g:" + g]);
        }
        byKey["g:" + g].children.push({
          label: m.title, key: "r:" + m.path
        });
      }
      return groups;
    });

    const activeKey = computed(() => {
      const c = current.value;
      return c.page === "frame" ? "r:" + c.path : "";
    });

    function onMenuSelect(key) {
      if (key.indexOf("r:") === 0) { location.hash = "#r=" + key.slice(2); }
    }

    onMounted(boot);
    return { me, page: computed(() => me.value ? current.value.page : "login"),
             iframeSrc, contentTitle, menuOptions, activeKey, onMenuSelect, logout };
  },
  template: `
  <n-config-provider>
    <login-view v-if="page === 'login'"></login-view>
    <n-layout v-else has-sider style="height:100%">
      <n-layout-sider bordered :width="224" content-style="display:flex;flex-direction:column;height:100%"
                      style="background:#1d2430">
        <div style="height:56px;display:flex;align-items:center;justify-content:center;gap:8px;color:#fff;font-weight:700;letter-spacing:.06em">
          <span style="width:22px;height:22px;border-radius:5px;background:#18a058;display:inline-flex;align-items:center;justify-content:center;font-size:12px">Z</span>
          管理后台
        </div>
        <n-menu :options="menuOptions" :value="activeKey" @update:value="onMenuSelect"
                style="flex:1" dark></n-menu>
        <div style="padding:12px 16px;display:flex;align-items:center;justify-content:space-between;border-top:1px solid #2a3342;color:#c8cedb">
          <span>{{ me ? me.uid : "" }}</span>
          <n-button quaternary size="small" style="color:#c8cedb" @click="logout">退出</n-button>
        </div>
      </n-layout-sider>
      <n-layout content-style="display:flex;flex-direction:column;height:100%">
        <n-layout-header bordered style="height:52px;display:flex;align-items:center;justify-content:space-between;padding:0 20px">
          <n-breadcrumb>
            <n-breadcrumb-item>{{ contentTitle }}</n-breadcrumb-item>
          </n-breadcrumb>
        </n-layout-header>
        <n-layout-content content-style="height:calc(100% - 52px);position:relative">
          <dashboard-view v-if="page === 'dashboard'"
                          style="padding:16px 20px;display:block;height:100%;overflow:auto"></dashboard-view>
          <iframe v-else :src="iframeSrc" frameborder="0"
                  style="display:block;width:100%;height:100%"></iframe>
        </n-layout-content>
      </n-layout>
    </n-layout>
  </n-config-provider>`
};

/* ---- bootstrap ---- */
function mountAdmin() {
  // body 就绪后再建离散 API（内部要往 body 挂 provider 容器）
  const discrete = createDiscreteApi(["message"]);
  message = discrete.message;
  const app = createApp(App);
  app.use(naive);
  app.component("login-view", LoginView);
  app.component("dashboard-view", DashboardView);
  app.mount("#app");
}

/* 兼容两种执行时机：同步执行（脚本位于 #app 之后）与文档未就绪时等待 */
if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", mountAdmin);
} else {
  mountAdmin();
}

})();
