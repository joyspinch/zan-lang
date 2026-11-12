const sidebar = document.querySelector('.doc-sidebar');
if (sidebar) {
  const narrow = window.matchMedia('(max-width:760px)');
  const updateSidebar = () => { sidebar.open = !narrow.matches; };
  updateSidebar();
  narrow.addEventListener('change', updateSidebar);
}

const query = document.getElementById('docs-query');
const results = document.getElementById('docs-results');
const count = document.getElementById('docs-search-count');

if (query && results && count) {
  let corpus;
  let request;
  function render() {
    if (!corpus) return;
    const terms = query.value.trim().toLocaleLowerCase().split(/\s+/).filter(Boolean);
    results.replaceChildren();
    if (!terms.length) { count.textContent = '输入关键字，检索全部文档正文。'; return; }
    const matches = corpus.filter(item => terms.every(term => `${item.title} ${item.description} ${item.keywords || ''} ${item.text}`.toLocaleLowerCase().includes(term)));
    count.textContent = `找到 ${matches.length} 篇相关文档`;
    for (const item of matches) {
      const row = document.createElement('li');
      const link = document.createElement('a');
      link.href = `./${item.id}.html`;
      link.textContent = item.title;
      const excerpt = document.createElement('small');
      const text = item.text.replace(/[#*`|]/g, '').replace(/\s+/g, ' ');
      const position = text.toLocaleLowerCase().indexOf(terms[0]);
      excerpt.textContent = position >= 0 ? `…${text.slice(Math.max(0, position - 35), position + 120)}…` : item.description;
      row.append(link, excerpt);
      results.append(row);
    }
  }
  async function load() {
    if (!request) request = fetch(new URL('../docs/search.json', import.meta.url)).then(response => {
      if (!response.ok) throw new Error(`Search unavailable: ${response.status}`);
      return response.json();
    }).then(data => { corpus = data; render(); }).catch(() => {
      count.textContent = '全文检索暂时不可用。可用上方目录或完整文本继续阅读。';
      request = null;
    });
    await request;
  }
  count.textContent = '输入关键字，检索全部文档正文。';
  query.addEventListener('input', () => { if (corpus) render(); else load(); });
  query.addEventListener('focus', load, { once: true });
}
