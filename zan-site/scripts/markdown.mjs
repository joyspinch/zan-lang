export function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]));
}

function safeHref(value) {
  if (/^(?:https?:\/\/|mailto:)/i.test(value)) return value;
  if (/^[\w./#?-]/u.test(value) && !/^(?:\/\/|[^/#?]*:)/.test(value)) return value;
  return '#';
}

export function inline(text, { htmlLinks = true } = {}) {
  const tokens = [];
  const token = html => `\u0000${tokens.push(html) - 1}\u0000`;
  let result = text.replace(/`([^`]+)`/g, (_, code) => token(`<code>${escapeHtml(code)}</code>`));
  result = result.replace(/\[([^\]\n]+)\]\(([^\s)]+)\)/g, (_, label, href) => {
    const target = htmlLinks && !/^(?:https?:|mailto:)/.test(href) && !href.startsWith('../') ? href.replace(/\.md(?=#|$)/, '.html') : href;
    return token(`<a href="${escapeHtml(safeHref(target))}">${escapeHtml(label)}</a>`);
  });
  result = escapeHtml(result).replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>').replace(/\*([^*]+)\*/g, '<em>$1</em>');
  // A link label may itself contain inline code.
  return result.replace(/\u0000(\d+)\u0000/g, (_, index) => tokens[Number(index)])
    .replace(/\u0000(\d+)\u0000/g, (_, index) => tokens[Number(index)]);
}

export function rebaseMarkdownLinks(markdown, source) {
  let fenced = false;
  const base = new URL(source, 'https://docs.invalid/');
  return markdown.split('\n').map(line => {
    if (/^```/.test(line)) { fenced = !fenced; return line; }
    if (fenced) return line;
    return line.split(/(`[^`]*`)/g).map(part => part.startsWith('`') ? part : part.replace(/\[([^\]\n]+)\]\(([^\s)]+)\)/g, (link, label, href) => {
      if (/^(?:[a-z][a-z\d+.-]*:|\/\/)/i.test(href)) return link;
      const target = new URL(href, base);
      return `[${label}](${target.pathname.slice(1)}${target.search}${target.hash})`;
    })).join('');
  }).join('\n');
}

export function renderMarkdown(markdown) {
  const lines = markdown.replace(/\r\n/g, '\n').split('\n');
  const html = [];
  const headings = [];
  const slugs = new Map();
  let index = 0;
  const tableCells = line => line.trim().replace(/^\|/, '').replace(/\|$/, '').split('|').map(cell => cell.trim());
  const isTableRule = line => /^\s*\|?[\s:|-]+\|[\s:|-]*$/.test(line || '');
  const isBlock = line => /^(?:#{1,6} |```|> |\s*[-*] |\s*\d+\. |---\s*$)/.test(line || '');
  while (index < lines.length) {
    const line = lines[index];
    if (!line.trim()) { index++; continue; }
    const fence = line.match(/^```([\w-]*)\s*$/);
    if (fence) {
      const code = [];
      index++;
      while (index < lines.length && !/^```\s*$/.test(lines[index])) code.push(lines[index++]);
      if (index === lines.length) throw new Error('Unclosed Markdown code fence');
      index++;
      html.push(`<pre><code${fence[1] ? ` class="language-${escapeHtml(fence[1])}"` : ''}>${escapeHtml(code.join('\n'))}</code></pre>`);
      continue;
    }
    const heading = line.match(/^(#{1,6}) (.+)$/);
    if (heading) {
      const level = heading[1].length;
      const base = heading[2].replace(/[`*]/g, '').toLowerCase().replace(/[^\p{L}\p{N}]+/gu, '-').replace(/^-|-$/g, '') || 'section';
      const count = slugs.get(base) || 0;
      slugs.set(base, count + 1);
      const id = count ? `${base}-${count + 1}` : base;
      headings.push({ level, id, title: heading[2].replace(/[`*]/g, '') });
      html.push(`<h${level} id="${id}">${inline(heading[2])}</h${level}>`);
      index++;
      continue;
    }
    if (line.includes('|') && isTableRule(lines[index + 1])) {
      const header = tableCells(line);
      index += 2;
      const rows = [];
      while (index < lines.length && lines[index].trim() && lines[index].includes('|')) {
        rows.push(`<tr>${tableCells(lines[index++]).map(cell => `<td>${inline(cell)}</td>`).join('')}</tr>`);
      }
      html.push(`<div class="table-scroll" role="region" aria-label="参考表格" tabindex="0"><table><thead><tr>${header.map(cell => `<th scope="col">${inline(cell)}</th>`).join('')}</tr></thead><tbody>${rows.join('')}</tbody></table></div>`);
      continue;
    }
    const list = line.match(/^\s*(?:([-*]) |(\d+)\. )/);
    if (list) {
      const tag = list[2] ? 'ol' : 'ul';
      const pattern = list[2] ? /^\s*\d+\. (.*)$/ : /^\s*[-*] (.*)$/;
      const items = [];
      while (index < lines.length && pattern.test(lines[index])) {
        let item = lines[index++].replace(pattern, '$1');
        while (index < lines.length && /^\s{2,}\S/.test(lines[index]) && !isBlock(lines[index])) item += ` ${lines[index++].trim()}`;
        items.push(`<li>${inline(item)}</li>`);
      }
      html.push(`<${tag}>${items.join('')}</${tag}>`);
      continue;
    }
    if (line.startsWith('> ')) {
      const quote = [];
      while (index < lines.length && lines[index].startsWith('> ')) quote.push(lines[index++].slice(2));
      html.push(`<blockquote><p>${inline(quote.join(' '))}</p></blockquote>`);
      continue;
    }
    if (/^---\s*$/.test(line)) { html.push('<hr>'); index++; continue; }
    const paragraph = [line];
    index++;
    while (index < lines.length && lines[index].trim() && !isBlock(lines[index]) && !isTableRule(lines[index + 1])) paragraph.push(lines[index++]);
    html.push(`<p>${inline(paragraph.join(' '))}</p>`);
  }
  return { html: html.join('\n'), headings };
}
