/* admin SPA：Vue 3 全局构建（无构建步骤，vendor 提供的 dist 直接跑）。
   结构：api() 封装 fetch（错误码→异常）→ 三个页面组件（登录/仪表盘/文章）
   → hash 路由 → 根组件按 route 挂载。服务端是唯一事实：菜单来自
   /api/admin/menu（权限过滤后），写操作失败即弹 toast 不改本地状态。 */

const { createApp, ref, computed, onMounted } = Vue;

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
    const user = ref("admin");
    const pass = ref("");
    const busy = ref(false);
    const err = ref("");
    async function submit() {
      busy.value = true; err.value = "";
      try {
        await api("/api/auth/login", { method: "POST", body: formBody({ user: user.value, pass: pass.value }) });
        dispatchEvent(new Event("zan:login"));   // 通知根组件重新 boot
      } catch (e) {
        err.value = e.message;
        busy.value = false;
      }
    }
    return { user, pass, busy, err, submit };
  },
  template: `
  <div class="ad-login-wrap">
    <form class="ad-login" @submit.prevent="submit">
      <h1>管理后台</h1>
      <p class="lede">登录以继续</p>
      <div v-if="err" class="flash bad">{{ err }}</div>
      <div class="field">
        <label>账号</label>
        <input class="input" v-model="user" autocomplete="username">
      </div>
      <div class="field">
        <label>密码</label>
        <input class="input" type="password" v-model="pass" autocomplete="current-password">
      </div>
      <button class="btn btn-primary" style="width:100%" :disabled="busy">
        {{ busy ? "登录中…" : "登录" }}
      </button>
    </form>
  </div>`
};

/* ---- 仪表盘 ---- */
const DashboardView = {
  setup() {
    const s = ref(null);
    const err = ref("");
    onMounted(async () => {
      try { s.value = await api("/api/admin/summary"); }
      catch (e) { err.value = e.message; }
    });
    return { s, err };
  },
  template: `
  <div>
    <div v-if="err" class="flash bad">{{ err }}</div>
    <div v-if="s" class="ad-card">
      <h2>运行状态 · {{ s.uptime }}</h2>
      <div class="ad-stats">
        <div class="ad-stat"><b>{{ s.requests }}</b><span>请求总数</span></div>
        <div class="ad-stat"><b>{{ s.errors }}</b><span>错误</span></div>
        <div class="ad-stat"><b>{{ (s.avgUs / 1000).toFixed(1) }} ms</b><span>平均延迟</span></div>
      </div>
    </div>
    <div v-if="s" class="ad-card">
      <h2>内容</h2>
      <div class="ad-stats">
        <div class="ad-stat"><b>{{ s.users }}</b><span>账号</span></div>
        <div class="ad-stat"><b>{{ s.posts }}</b><span>文章</span></div>
        <div class="ad-stat"><b>{{ s.published }}</b><span>已发布</span></div>
        <div class="ad-stat"><b>{{ s.comments }}</b><span>评论</span></div>
        <div class="ad-stat"><b>{{ s.pending }}</b><span>待审</span></div>
      </div>
    </div>
  </div>`
};

/* ---- 文章管理 ---- */
const PostsView = {
  setup() {
    const rows = ref([]);
    const total = ref(0);
    const page = ref(1);
    const pages = ref(1);
    const kw = ref("");
    const loading = ref(false);
    const err = ref("");
    const editing = ref(null);   // 正在编辑的表单对象
    const busy = ref(false);
    const formErr = ref("");

    async function load(p) {
      loading.value = true; err.value = "";
      try {
        const data = await api("/api/admin/posts/list?page=" + p + "&kw=" + encodeURIComponent(kw.value));
        rows.value = data.items;
        total.value = data.total;
        page.value = data.page;
        pages.value = data.pages;
      } catch (e) {
        err.value = e.message;
      } finally {
        loading.value = false;
      }
    }

    async function open(id) {
      formErr.value = "";
      try {
        const p = await api("/api/admin/posts/get?id=" + id);
        editing.value = {
          id: p.id, title: p.title, summary: p.summary, body: p.body,
          tags: p.tags, cover: p.cover, author: p.author,
          categoryId: p.categoryId, published: String(p.published),
          version: p.version, categories: p.categories
        };
      } catch (e) { formErr.value = e.message; }
    }

    function create() {
      formErr.value = "";
      // 分类列表与第一行文章共用：借 get 端点的 categories 数组
      //（取列表中任意一行的 id；空表用 id=0 的 404 前最后一条已知行）。
      const first = rows.value.length > 0 ? rows.value[0].id : 0;
      if (first > 0) {
        api("/api/admin/posts/get?id=" + first).then(p => {
          editing.value = {
            id: 0, title: "", summary: "", body: "", tags: "", cover: "",
            author: "", categoryId: 0, published: "1", version: -1,
            categories: p.categories
          };
        }).catch(e => { formErr.value = e.message; });
      } else {
        editing.value = {
          id: 0, title: "", summary: "", body: "", tags: "", cover: "",
          author: "", categoryId: 0, published: "1", version: -1, categories: []
        };
      }
    }

    async function save() {
      busy.value = true; formErr.value = "";
      try {
        const isNew = editing.value.id === 0;
        const body = formBody(Object.assign({}, editing.value, {
          categories: undefined
        }));
        await api(isNew ? "/api/admin/posts/create" : "/api/admin/posts/update", {
          method: "POST", body: body
        });
        editing.value = null;
        await load(page.value);
      } catch (e) {
        // 409 = 乐观锁冲突：明确提示刷新，不覆盖他人改动
        formErr.value = e.status === 409 ? e.message : e.message;
      } finally {
        busy.value = false;
      }
    }

    async function togglePublish(row) {
      try {
        await api("/api/admin/posts/publish", {
          method: "POST",
          body: formBody({ id: row.id, published: row.published === 1 ? 0 : 1 })
        });
        await load(page.value);
      } catch (e) { err.value = e.message; }
    }

    async function remove(row) {
      if (!confirm("删除「" + row.title + "」？")) { return; }
      try {
        await api("/api/admin/posts/delete", { method: "POST", body: formBody({ id: row.id }) });
        await load(page.value);
      } catch (e) { err.value = e.message; }
    }

    onMounted(() => load(1));
    return { rows, total, page, pages, kw, loading, err, editing, busy, formErr,
             load, open, create, save, togglePublish, remove };
  },
  template: `
  <div>
    <div v-if="err" class="flash bad">{{ err }}</div>
    <div class="ad-card">
      <div class="btn-row" style="justify-content:space-between;align-items:center">
        <form class="btn-row" @submit.prevent="load(1)">
          <input class="input input-sm" style="width:220px" v-model="kw" placeholder="按标题搜索">
          <button class="btn btn-sm" type="submit">搜索</button>
        </form>
        <button class="btn btn-primary btn-sm" @click="create">写文章</button>
      </div>
      <table class="ad-table" style="margin-top:12px">
        <thead>
          <tr><th>标题</th><th>分类</th><th>作者</th><th>状态</th><th style="width:220px"></th></tr>
        </thead>
        <tbody>
          <tr v-if="loading"><td colspan="5" style="color:var(--muted)">加载中…</td></tr>
          <tr v-for="r in rows" :key="r.id">
            <td>{{ r.title }}</td>
            <td>{{ r.category }}</td>
            <td>{{ r.author }}</td>
            <td>{{ r.published === 1 ? "已发布" : "草稿" }}</td>
            <td>
              <button class="btn btn-sm" @click="open(r.id)">编辑</button>
              <button class="btn btn-sm" @click="togglePublish(r)">
                {{ r.published === 1 ? "下架" : "发布" }}
              </button>
              <button class="btn btn-sm btn-danger" @click="remove(r)">删除</button>
            </td>
          </tr>
          <tr v-if="!loading && rows.length === 0"><td colspan="5" style="color:var(--muted)">还没有文章。</td></tr>
        </tbody>
      </table>
      <div class="ad-pager">
        <button class="btn btn-sm" :disabled="page <= 1" @click="load(page - 1)">上一页</button>
        <span>第 {{ page }} / {{ pages }} 页 · 共 {{ total }} 篇</span>
        <button class="btn btn-sm" :disabled="page >= pages" @click="load(page + 1)">下一页</button>
      </div>
    </div>

    <div v-if="editing" class="ad-mask" @click.self="editing = null">
      <div class="ad-modal">
        <h2>{{ editing.id === 0 ? "写文章" : "编辑文章" }}</h2>
        <div v-if="formErr" class="flash bad">{{ formErr }}</div>
        <form class="ad-form" @submit.prevent="save">
          <label>标题</label>
          <input class="input" v-model="editing.title" required maxlength="200">
          <label>分类</label>
          <select class="input" v-model.number="editing.categoryId">
            <option :value="0">未分类</option>
            <option v-for="c in editing.categories" :key="c.id" :value="c.id">{{ c.name }}</option>
          </select>
          <label>作者</label>
          <input class="input" v-model="editing.author" maxlength="64">
          <label>标签</label>
          <input class="input" v-model="editing.tags" maxlength="255" placeholder="逗号分隔">
          <label>封面</label>
          <input class="input" v-model="editing.cover" maxlength="255">
          <label>状态</label>
          <select class="input" v-model="editing.published">
            <option value="1">发布</option>
            <option value="0">草稿</option>
          </select>
          <label>摘要</label>
          <textarea class="input" v-model="editing.summary" rows="2"></textarea>
          <label>正文</label>
          <textarea class="input" v-model="editing.body" rows="12" required></textarea>
          <div></div>
          <div class="full" style="display:flex;justify-content:flex-end;gap:8px;margin-top:8px">
            <button class="btn" type="button" @click="editing = null">取消</button>
            <button class="btn btn-primary" type="submit" :disabled="busy">
              {{ busy ? "保存中…" : "保存" }}
            </button>
          </div>
        </form>
      </div>
    </div>
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

    onMounted(boot);
    return { me, menu, page, logout, go, active };
  },
  template: `
  <component v-if="page === 'login'" :is="'login-view'"></component>
  <div v-else class="ad-shell">
    <aside class="ad-side">
      <div class="ad-brand">管理后台</div>
      <nav class="ad-nav">
        <a class="ad-link" :class="{ active: page === 'dashboard' }" href="#/">仪表盘</a>
        <template v-for="m in menu" :key="m.path">
          <a class="ad-link" href="javascript:void(0)" @click="go(m.path)">{{ m.title }}</a>
        </template>
      </nav>
      <div class="ad-foot">
        <span>{{ me ? me.uid : "" }}</span>
        <button class="btn btn-ghost btn-sm" @click="logout">退出</button>
      </div>
    </aside>
    <main class="ad-main">
      <header class="ad-head"><h1>{{ page === 'posts' ? "文章管理" : "仪表盘" }}</h1></header>
      <div class="ad-body">
        <dashboard-view v-if="page === 'dashboard'"></dashboard-view>
        <posts-view v-else-if="page === 'posts'"></posts-view>
      </div>
    </main>
  </div>`,
  components: {
    "login-view": LoginView,
    "dashboard-view": DashboardView,
    "posts-view": PostsView
  }
};

createApp(App).mount("#app");
