let currentView = 'dashboard';

async function fetchStats() {
  try {
    const res = await fetch('/api/monitor/stat/summary');
    const data = (await res.json()).data || {};
    document.getElementById('stReq').innerText = data.totalRequests ?? 0;
    document.getElementById('stAvg').innerText = (data.avgLatencyMs ?? 0) + ' ms';
    document.getElementById('stSlowReq').innerText = data.slowRequests ?? 0;
    document.getElementById('stSlowSql').innerText = data.slowQueries ?? 0;
  } catch (err) {
    console.error('Failed to fetch stats:', err);
  }
}

async function loadView(name, event) {
  currentView = name;
  document.querySelectorAll('.nav-item').forEach(el => el.classList.remove('active'));
  if (event && event.currentTarget) {
    event.currentTarget.classList.add('active');
  }

  const title = document.getElementById('pageTitle');
  const tableTitle = document.getElementById('tableTitle');
  const box = document.getElementById('tableBox');
  box.innerHTML = '<p style="color:#64748b;padding:20px 0;">正在加载数据...</p>';

  if (name === 'dashboard') {
    title.innerText = '服务运行指标与慢SQL监控';
    tableTitle.innerText = '慢 SQL 追踪记录 (耗时 > 200ms)';
    try {
      const res = await fetch('/api/monitor/stat/slowsql');
      const list = (await res.json()).data || [];
      if (!list || list.length === 0) {
        box.innerHTML = '<p style="color:#64748b;padding:20px 0;">暂无慢 SQL 记录，服务运行健康稳定。</p>';
        return;
      }
      let html = '<table><thead><tr><th>SQL 语句</th><th style="width:140px;">耗时</th><th style="width:180px;">捕获时间</th></tr></thead><tbody>';
      list.forEach(q => {
        html += `<tr>
          <td class="code-cell">${escapeHtml(q.sql)}</td>
          <td><span class="badge badge-danger">${q.elapsedMs} ms</span></td>
          <td>${new Date(q.timestamp * 1000).toLocaleString()}</td>
        </tr>`;
      });
      box.innerHTML = html + '</tbody></table>';
    } catch (e) {
      box.innerHTML = '<p style="color:#dc2626;padding:20px 0;">加载慢 SQL 数据失败</p>';
    }
  } else if (name === 'users') {
    title.innerText = '组织用户体系管理';
    tableTitle.innerText = '系统用户列表';
    try {
      const res = await fetch('/api/system/user/page?page=1&pageSize=20');
      const data = (await res.json()).data || {};
      const list = data.records || [];
      let html = '<table><thead><tr><th>ID</th><th>用户名</th><th>昵称</th><th>归属部门</th><th>所属角色</th><th>手机号</th><th>状态</th></tr></thead><tbody>';
      list.forEach(u => {
        const statusBadge = u.status === 1
          ? '<span class="badge badge-success">正常</span>'
          : '<span class="badge badge-danger">禁用</span>';
        html += `<tr>
          <td>${u.id}</td>
          <td><strong>${escapeHtml(u.username)}</strong></td>
          <td>${escapeHtml(u.nickname || '-')}</td>
          <td>${escapeHtml(u.departmentName || '-')}</td>
          <td>${escapeHtml(u.roleName || '-')}</td>
          <td>${escapeHtml(u.mobile || '-')}</td>
          <td>${statusBadge}</td>
        </tr>`;
      });
      box.innerHTML = html + '</tbody></table>';
    } catch (e) {
      box.innerHTML = '<p style="color:#dc2626;padding:20px 0;">加载用户列表失败</p>';
    }
  } else if (name === 'depts') {
    title.innerText = '组织架构与部门管理';
    tableTitle.innerText = '系统部门架构列表';
    try {
      const res = await fetch('/api/system/dept/all');
      const list = (await res.json()).data || [];
      let html = '<table><thead><tr><th>ID</th><th>部门名称</th><th>部门编码</th><th>上级部门ID</th><th>排序号</th><th>状态</th></tr></thead><tbody>';
      list.forEach(d => {
        const statusBadge = d.status === 1
          ? '<span class="badge badge-success">正常</span>'
          : '<span class="badge">停用</span>';
        html += `<tr>
          <td>${d.id}</td>
          <td><strong>${escapeHtml(d.name)}</strong></td>
          <td class="code-cell">${escapeHtml(d.code)}</td>
          <td>${d.parentId}</td>
          <td>${d.sortOrder}</td>
          <td>${statusBadge}</td>
        </tr>`;
      });
      box.innerHTML = html + '</tbody></table>';
    } catch (e) {
      box.innerHTML = '<p style="color:#dc2626;padding:20px 0;">加载部门架构失败</p>';
    }
  } else if (name === 'roles') {
    title.innerText = '角色与权限矩阵';
    tableTitle.innerText = '系统角色列表';
    try {
      const res = await fetch('/api/system/role/all');
      const list = (await res.json()).data || [];
      let html = '<table><thead><tr><th>ID</th><th>角色名称</th><th>角色说明</th><th>状态</th></tr></thead><tbody>';
      list.forEach(r => {
        const statusBadge = r.status === 1
          ? '<span class="badge badge-success">正常</span>'
          : '<span class="badge">停用</span>';
        html += `<tr>
          <td>${r.id}</td>
          <td><strong>${escapeHtml(r.name)}</strong></td>
          <td>${escapeHtml(r.intro || '-')}</td>
          <td>${statusBadge}</td>
        </tr>`;
      });
      box.innerHTML = html + '</tbody></table>';
    } catch (e) {
      box.innerHTML = '<p style="color:#dc2626;padding:20px 0;">加载角色列表失败</p>';
    }
  } else if (name === 'docs') {
    title.innerText = '接口文档与 OpenAPI 规范';
    tableTitle.innerText = '已注册 API 端点清单 (自动反射自 [Route] / [HttpGet] / [HttpPost])';
    try {
      const res = await fetch('/api/system/docs/page');
      const data = (await res.json()).data || {};
      const list = data.records || [];
      let html = `
        <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:14px;gap:12px;flex-wrap:wrap;">
          <div style="display:flex;gap:10px;align-items:center;">
            <input type="text" id="docKw" placeholder="搜索接口路径或名称..." style="padding:6px 10px;border:1px solid #cbd5e1;border-radius:4px;font-size:13px;width:240px;" oninput="filterDocs()" />
            <span style="font-size:12px;color:#64748b;">共找到 <strong id="docCount">${list.length}</strong> 个端点</span>
          </div>
          <div style="display:flex;gap:8px;">
            <a href="/api/docs" target="_blank" class="btn btn-primary" style="text-decoration:none;display:inline-flex;align-items:center;gap:4px;">📖 打开交互式文档 (Swagger UI)</a>
            <a href="/api/docs.json" target="_blank" class="btn" style="text-decoration:none;background:#f1f5f9;color:#334155;border:1px solid #cbd5e1;">📥 OpenAPI 3.0 JSON</a>
          </div>
        </div>
        <table id="docsTable">
          <thead>
            <tr>
              <th style="width:80px;">方法</th>
              <th style="width:240px;">接口路径</th>
              <th style="width:160px;">接口名称</th>
              <th style="width:200px;">后端动作</th>
              <th style="width:90px;">权限</th>
              <th>参数声明 (由代码调用点自动推导)</th>
            </tr>
          </thead>
          <tbody>`;

      list.forEach(item => {
        let mClass = 'badge-method-get';
        if (item.method === 'POST') mClass = 'badge-method-post';
        else if (item.method === 'PUT') mClass = 'badge-method-put';
        else if (item.method === 'DELETE') mClass = 'badge-method-delete';

        let authClass = item.authText === '公开' ? 'badge-success' : (item.authText === '需登录' ? 'badge' : 'badge-danger');

        html += `<tr class="doc-row" data-search="${escapeHtml((item.pattern + ' ' + item.title + ' ' + item.action).toLowerCase())}">
          <td><span class="badge ${mClass}">${item.method}</span></td>
          <td class="code-cell"><strong>${escapeHtml(item.pattern)}</strong></td>
          <td>${escapeHtml(item.title)}</td>
          <td style="font-size:12px;color:#64748b;font-family:monospace;">${escapeHtml(item.action)}</td>
          <td><span class="badge ${authClass}">${escapeHtml(item.authText)}</span></td>
          <td style="font-size:12px;color:#475569;">${escapeHtml(item.paramsText)}</td>
        </tr>`;
      });
      html += '</tbody></table>';
      box.innerHTML = html;

      window.filterDocs = function() {
        const val = document.getElementById('docKw').value.toLowerCase().trim();
        const rows = document.querySelectorAll('.doc-row');
        let count = 0;
        rows.forEach(r => {
          const text = r.getAttribute('data-search') || '';
          if (!val || text.indexOf(val) >= 0) {
            r.style.display = '';
            count++;
          } else {
            r.style.display = 'none';
          }
        });
        document.getElementById('docCount').innerText = count;
      };
    } catch (e) {
      box.innerHTML = '<p style="color:#dc2626;padding:20px 0;">加载接口文档失败</p>';
    }
  } else if (name === 'curd') {
    title.innerText = '低代码代码生成器 (CURD)';
    tableTitle.innerText = '数据库表反向工程与代码生成';
    box.innerHTML = `
      <div style="padding:16px 0;">
        <p style="color:#475569;margin-bottom:16px;">基于 Zan.Web 的数据表架构元数据，一键逆向生成对应的 Model 实体、Dao 数据存取层、Controller 控制器以及前端 ZanTable 配置。</p>
        <div style="display:flex;gap:10px;align-items:center;">
          <input type="text" id="curdTable" placeholder="输入数据库表名，如 order_info..." style="padding:8px 12px;border:1px solid #cbd5e1;border-radius:6px;width:300px;font-size:14px;" />
          <button class="btn btn-primary" onclick="generateCode()">🚀 生成代码并预览</button>
        </div>
        <div id="curdPreview" style="margin-top:20px;"></div>
      </div>`;

    window.generateCode = async function() {
      const table = document.getElementById('curdTable').value.trim();
      const prev = document.getElementById('curdPreview');
      if (!table) { alert('请输入表名'); return; }
      prev.innerHTML = '<p style="color:#64748b;">正在解析表结构并生成代码...</p>';
      try {
        const res = await fetch('/api/dev/curd/generate?table=' + encodeURIComponent(table));
        const resJson = await res.json();
        if (resJson.code !== 0) {
          prev.innerHTML = `<p style="color:#dc2626;">生成失败: ${escapeHtml(resJson.msg)}</p>`;
          return;
        }
        const data = resJson.data || {};
        prev.innerHTML = `
          <div style="display:flex;flex-direction:column;gap:12px;">
            <h4>Model 实体代码预览:</h4>
            <pre style="background:#0f172a;color:#f8fafc;padding:14px;border-radius:6px;font-size:12px;overflow-x:auto;">${escapeHtml(data.modelCode || '// 无 Model 代码')}</pre>
          </div>`;
      } catch (e) {
        prev.innerHTML = '<p style="color:#dc2626;">请求失败，请检查网络或后端接口</p>';
      }
    };
  }
}

function refreshCurrent() {
  fetchStats();
  loadView(currentView);
}

function escapeHtml(str) {
  if (!str) return '';
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

document.addEventListener('DOMContentLoaded', () => {
  fetchStats();
  loadView('dashboard');
  setInterval(fetchStats, 3000);
});
