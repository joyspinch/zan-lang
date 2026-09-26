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
