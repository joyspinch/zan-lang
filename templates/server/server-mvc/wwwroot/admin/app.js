(function(){
'use strict';
/* admin SPA：Vue 3 + Naive UI + 自有 zan 系组件（全部浏览器 bundle，无构建步骤）。
   组合：Naive UI 出布局/表单/按钮等通用件；zan-grid 出文章列表（企业级表格）；
   zan-charts 出仪表盘图表；zan-layer 出确认框/消息。
   结构：api() 封装 fetch（错误码→异常）→ 登录/仪表盘/文章三屏 → hash 路由。
   服务端是唯一事实：菜单来自 /api/admin/menu（权限过滤后），写操作失败
   弹 layer.msg 不改本地状态；保存冲突 409 明确提示刷新。 */

const { createApp, ref, reactive, computed, onMounted, h, nextTick } = Vue;
const { NTag, NButton, NSpace } = naive;
const { createDiscreteApi } = naive;

/* 离散 API：不依赖 app 上下文，任意 setup 里可直接调用。
   必须延迟到 body 就绪后创建——同步执行时 body 可能尚未解析完成，
   createDiscreteApi 内部挂载 provider 会因 body 为 null 抛错，导致整个脚本失效。 */
let message, dialog;
const layer = window.ZanLayer && window.ZanLayer.layer ? window.ZanLayer.layer : null;

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
  <div style="height:100%;display:flex;align-items:center;justify-content:center;background:#f5f7fa">
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

/* ---- 仪表盘 ---- */
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
    return { sum, cards, blogCards, uptime };
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
    <n-card size="small" title="内容概览" style="margin-top:14px">
      <template #header-extra><n-text depth="3">{{ uptime }}</n-text></template>
      <div id="chart-traffic" style="width:100%;height:280px"></div>
    </n-card>
  </div>`,
  mounted() {
    // 仪表盘图表：请求/错误数时序来自 /api/admin/metrics/history（若可用），
    // 摘要数字来自 summary。图表容器在首次 summary 渲染后才有，故 nextTick 接入。
    this.$nextTick(() => this.drawChart());
  },
  methods: {
    async drawChart() {
      if (!window.ZanCharts || !window.ZanCharts.createChart) { return; }
      const host = document.getElementById("chart-traffic");
      if (!host) { return; }
      let data = [];
      try {
        const hist = await api("/api/admin/metrics/history?limit=30");
        data = (hist.points || hist.rows || hist.items || []).map(p => ({
          t: p.time || p.ts || p.createdAt, requests: p.requests, errors: p.errors
        }));
      } catch (e) { /* 无历史端点时退化为空图 */ }
      if (data.length === 0) {
        // 没有历史端点就用 summary 现值画单点，保证容器有内容
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
  }
};

/* ---- 文章管理（zan-grid） ---- */
const PostsView = {
  setup() {
    const rows = ref([]);
    const total = ref(0);
    const page = ref(1);
    const limit = ref(20);
    const kw = ref("");
    const loading = ref(false);
    const editing = ref(null);
    const saving = ref(false);
    const gridRef = ref(null);

    async function load(p) {
      loading.value = true;
      try {
        const data = await api("/api/admin/posts/list?page=" + p + "&limit=" + limit.value
          + "&kw=" + encodeURIComponent(kw.value));
        rows.value = data.items;
        total.value = data.total;
        page.value = data.page;
      } catch (e) {
        message.error(e.message);
      } finally { loading.value = false; }
    }

    async function open(id) {
      try {
        const p = await api("/api/admin/posts/get?id=" + id);
        editing.value = {
          id: p.id, title: p.title, summary: p.summary, body: p.body,
          tags: p.tags, cover: p.cover, author: p.author,
          categoryId: p.categoryId, published: String(p.published),
          version: p.version, categories: p.categories
        };
      } catch (e) { message.error(e.message); }
    }

    async function create() {
      try {
        let cats = [];
        if (rows.value.length > 0) {
          const p = await api("/api/admin/posts/get?id=" + rows.value[0].id);
          cats = p.categories;
        }
        editing.value = {
          id: 0, title: "", summary: "", body: "", tags: "", cover: "",
          author: "", categoryId: 0, published: "1", version: -1, categories: cats
        };
      } catch (e) { message.error(e.message); }
    }

    async function save() {
      saving.value = true;
      try {
        const isNew = editing.value.id === 0;
        await api(isNew ? "/api/admin/posts/create" : "/api/admin/posts/update", {
          method: "POST",
          body: formBody(Object.assign({}, editing.value))
        });
        message.success("已保存");
        editing.value = null;
        await load(page.value);
      } catch (e) {
        if (e.status === 409) {
          // 乐观锁冲突：明确引导刷新，不覆盖他人改动
          dialog.warning({
            title: "保存冲突",
            content: e.message,
            positiveText: "刷新重试",
            negativeText: "留在本页",
            onPositiveClick: () => { editing.value = null; load(page.value); }
          });
        } else {
          message.error(e.message);
        }
      } finally { saving.value = false; }
    }

    async function togglePublish(row) {
      try {
        await api("/api/admin/posts/publish", {
          method: "POST",
          body: formBody({ id: row.id, published: row.published === 1 ? 0 : 1 })
        });
        message.success(row.published === 1 ? "已下架" : "已发布");
        await load(page.value);
      } catch (e) { message.error(e.message); }
    }

    async function remove(row) {
      const d = dialog.warning({
        title: "删除确认",
        content: "删除「" + row.title + "」？删除后不可恢复。",
        positiveText: "删除",
        negativeText: "取消",
        onPositiveClick: async () => {
          try {
            await api("/api/admin/posts/delete", { method: "POST", body: formBody({ id: row.id }) });
            message.success("已删除");
            await load(page.value);
          } catch (e) { message.error(e.message); }
        }
      });
    }

    onMounted(() => load(1));

    /* 列表用 n-data-table：zan-grid 1.2.x 在 Vue 3.4 下 setup 栈溢出（已挂账 TASKS.md），
       等库修复后切回 <Grid> 获得类 Excel 交互。这里保持服务端分页 + 行操作。 */
    const nColumns = [
      { title: "标题", key: "title", minWidth: 260, ellipsis: { tooltip: true } },
      { title: "分类", key: "category", width: 100 },
      { title: "作者", key: "author", width: 100 },
      {
        title: "状态", key: "published", width: 90,
        render: row => h(NTag, { size: "small", type: row.published === 1 ? "success" : "default" },
          { default: () => row.published === 1 ? "已发布" : "草稿" })
      },
      {
        title: "创建时间", key: "createdAt", width: 170,
        render: row => { const d = new Date(row.createdAt * 1000); return d.toLocaleString("zh-CN", { hour12: false }); }
      },
      {
        title: "操作", key: "op", width: 190,
        render: row => h(NSpace, { size: "small" }, {
          default: () => [
            h(NButton, { size: "tiny", type: "primary", quaternary: true, onClick: () => open(row.id) }, { default: () => "编辑" }),
            h(NButton, { size: "tiny", quaternary: true, onClick: () => togglePublish(row) },
              { default: () => row.published === 1 ? "下架" : "发布" }),
            h(NButton, { size: "tiny", type: "error", quaternary: true, onClick: () => remove(row) }, { default: () => "删除" })
          ]
        })
      }
    ];

    return { rows, total, page, limit, kw, loading, editing, saving,
             load, open, create, save, togglePublish, remove, nColumns };
  },
  template: `
  <div>
    <n-card size="small">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:12px">
        <n-input v-model:value="kw" placeholder="按标题搜索" style="width:240px" clearable
                 @keyup.enter="load(1)" @clear="load(1)"></n-input>
        <n-button type="primary" @click="create">写文章</n-button>
      </div>
      <n-data-table :columns="nColumns" :data="rows" :loading="loading" size="small" striped></n-data-table>
      <div style="margin-top:10px;text-align:right">
        <n-pagination :page="page" :page-size="limit" :item-count="total"
                      :page-sizes="[10,20,50]" show-size-picker
                      @update:page="p => load(p)"
                      @update:page-size="s => { limit = s; load(1); }"></n-pagination>
      </div>
    </n-card>

    <n-modal :show="!!editing" style="width:640px" preset="card"
             :title="editing && editing.id === 0 ? '写文章' : '编辑文章'"
             :mask-closable="false" @update:show="v => { if (!v) editing = null; }">
      <n-form v-if="editing" label-width="72">
        <n-form-item label="标题" required>
          <n-input v-model:value="editing.title" maxlength="200"></n-input>
        </n-form-item>
        <n-form-item label="分类">
          <n-select v-model:value="editing.categoryId" :options="(editing.categories || []).map(c => ({ label: c.name, value: c.id }))"></n-select>
        </n-form-item>
        <n-grid :cols="2" :x-gap="12">
          <n-gi><n-form-item label="作者"><n-input v-model:value="editing.author" maxlength="64"></n-input></n-form-item></n-gi>
          <n-gi><n-form-item label="状态">
            <n-radio-group v-model:value="editing.published">
              <n-radio value="0">草稿</n-radio>
              <n-radio value="1">发布</n-radio>
            </n-radio-group>
          </n-form-item></n-gi>
        </n-grid>
        <n-grid :cols="2" :x-gap="12">
          <n-gi><n-form-item label="标签"><n-input v-model:value="editing.tags" maxlength="255" placeholder="逗号分隔"></n-input></n-form-item></n-gi>
          <n-gi><n-form-item label="封面"><n-input v-model:value="editing.cover" maxlength="255"></n-input></n-form-item></n-gi>
        </n-grid>
        <n-form-item label="摘要">
          <n-input v-model:value="editing.summary" type="textarea" :rows="2"></n-input>
        </n-form-item>
        <n-form-item label="正文" required>
          <n-input v-model:value="editing.body" type="textarea" :rows="10"></n-input>
        </n-form-item>
      </n-form>
      <template #footer>
        <div style="display:flex;justify-content:flex-end;gap:10px">
          <n-button @click="editing = null">取消</n-button>
          <n-button type="primary" :loading="saving" @click="save">保存</n-button>
        </div>
      </template>
    </n-modal>
  </div>`
};

/* ---- 根组件：hash 路由 + 布局 ---- */
const App = {
  setup() {
    const route = ref(location.hash || "#/");
    const me = ref(null);
    const menu = ref([]);
    window.addEventListener("hashchange", () => {
      route.value = location.hash || "#/";
      loadMenu();
    });
    // 登录页与外壳是两个组件，登录成功后由 LoginView 广播重进
    window.addEventListener("zan:login", () => { boot(); });

    async function boot() {
      try {
        me.value = await api("/api/auth/me");
        await loadMenu();
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
      location.hash = "#/";
    }

    // 菜单路径 → hash 路由："#r=/admin/content/posts"
    function go(path) { location.hash = "#r=" + path; }
    function active(path) {
      return ("#r=" + path) === route.value || route.value.indexOf("#r=" + path) === 0;
    }

    const page = computed(() => {
      if (!me.value) { return "login"; }
      if (route.value.indexOf("#r=/admin/content/posts") === 0) { return "posts"; }
      return "dashboard";
    });

    const menuOptions = computed(() => [
      { label: "仪表盘", key: "#/" },
      ...menu.value.map(m => ({ label: m.title, key: "#r=" + m.path }))
    ]);
    const activeKey = computed(() => page.value === "posts" ? "#r=/admin/content/posts" : "#/");
    function onMenuSelect(key) {
      if (key === "#/") { location.hash = "#/"; } else { location.hash = key; }
    }

    onMounted(boot);
    return { me, menu, page, logout, go, active, menuOptions, activeKey, onMenuSelect };
  },
  template: `
  <n-config-provider>
    <login-view v-if="page === 'login'"></login-view>
    <n-layout v-else has-sider style="height:100%">
      <n-layout-sider bordered content-style="display:flex;flex-direction:column;height:100%" :width="220">
        <div style="height:56px;display:flex;align-items:center;justify-content:center;font-weight:700;letter-spacing:.06em">管理后台</div>
        <n-menu :options="menuOptions" :value="activeKey" @update:value="onMenuSelect"
                style="flex:1"></n-menu>
        <div style="padding:12px 16px;display:flex;align-items:center;justify-content:space-between;border-top:1px solid #efeff5">
          <span>{{ me ? me.uid : "" }}</span>
          <n-button quaternary size="small" @click="logout">退出</n-button>
        </div>
      </n-layout-sider>
      <n-layout content-style="display:flex;flex-direction:column;height:100%">
        <n-layout-header bordered style="height:56px;display:flex;align-items:center;padding:0 24px">
          <h1 style="font-size:16px;font-weight:600;margin:0">{{ page === 'posts' ? "文章管理" : "仪表盘" }}</h1>
        </n-layout-header>
        <n-layout-content content-style="padding:20px 24px;flex:1;overflow:auto">
          <dashboard-view v-if="page === 'dashboard'"></dashboard-view>
          <posts-view v-else-if="page === 'posts'"></posts-view>
        </n-layout-content>
      </n-layout>
    </n-layout>
  </n-config-provider>`
};

/* ---- bootstrap ---- */
function mountAdmin() {
  // body 就绪后再建离散 API（内部要往 body 挂 provider 容器）
  const discrete = createDiscreteApi(["message", "dialog"]);
  message = discrete.message;
  dialog = discrete.dialog;
  const app = createApp(App);
  app.use(naive);
  // 业务子组件：根组件模板里 <login-view>/<dashboard-view>/<posts-view>
  app.component("login-view", LoginView);
  app.component("dashboard-view", DashboardView);
  app.component("posts-view", PostsView);
  // zan-grid UMD 全局是 ZanGrid（不提供插件安装，直接注册组件）
  if (window.ZanGrid && window.ZanGrid.Grid) {
    app.component("Grid", window.ZanGrid.Grid);
  } else if (window.ZanGrid) {
    const g = window.ZanGrid.default || window.ZanGrid.Grid || window.ZanGrid;
    app.component("Grid", g);
  }
  // zan-layer：全局安装注册 <zan-layer> 容器与 $layer
  if (window.ZanLayer && window.ZanLayer.default) {
    app.use(window.ZanLayer.default);
  }
  app.mount("#app");
}

/* 兼容两种执行时机：同步执行（脚本位于 #app 之后）与文档未就绪时等待 */
if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", mountAdmin);
} else {
  mountAdmin();
}

})();
