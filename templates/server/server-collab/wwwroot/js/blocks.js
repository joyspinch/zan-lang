/* T10 块编辑器（自研 Alpine + contenteditable 最小集）。
 * 硬性纪律（IME 对策，不可妥协）：
 *   1) 文本块 DOM 是唯一事实源——blocks 数组只存结构（类型/done/url），
 *      文本一律在 [data-be-text] 的 contenteditable DOM 里；
 *   2) 输入期间零 DOM 重写——@input 只 mark()（状态位 + 草稿节流 +
 *      防抖计时），不写任何 Alpine 渲染状态，不触发 x-for patch；
 *   3) Alpine 只管块结构——增删/上下移/类型切换动数组，x-for :key
 *      稳定，节点移动时 contenteditable 连同文本一起被搬移；
 *   4) collect() 保存时读 textContent（title 同样 DOM 读取，不用
 *      x-model 绑任何文本输入）；
 *   5) 粘贴拦截转纯文本，DOM 内只有文本节点与浏览器原生行为。
 * 保存：2s 防抖自动保存 + 手动保存；409 乐观锁冲突 → Layer 提示 +
 * localStorage 草稿保留（双标签对敲场景），冲突未处理前停自动保存。
 * 图片块：行内文件输入直传 /admin/oa/attachments/upload（octet-stream，
 * attach.js 同款协议），成功后引用 /admin/oa/attachments/download?id=N。 */
(function () {
  function draftKey(docId) { return 'oa_doc_draft_' + docId; }

  document.addEventListener('alpine:init', function () {
    Alpine.data('blockEditor', function () {
      return {
        blocks: [],
        seq: 1,
        composing: false,
        dirty: false,
        conflict: false,
        timer: null,
        draftTimer: null,

        /* ---- 载入 ---- */
        init() {
          var root = this.$el;
          this.docId = parseInt(root.getAttribute('data-doc-id') || '0', 10);
          this.spaceId = parseInt(root.getAttribute('data-space-id') || '0', 10);
          this.baseVersion = parseInt(root.getAttribute('data-base-version') || '0', 10);
          this.blocks = this.parse(root.getAttribute('data-src') || '');
          if (this.blocks.length === 0) { this.blocks = [this.mk('p')]; }
          var self = this;
          this.$nextTick(function () { self.fill(); self.status(''); });
          var raw = localStorage.getItem(draftKey(this.docId));
          if (raw) {
            try {
              var d = JSON.parse(raw);
              if (d && typeof d.content === 'string' && window.Layer) {
                Layer.confirm('发现 ' + (d.at || '本地') + ' 的未保存草稿，恢复吗？',
                  function () { self.applyDraft(d); },
                  function () { localStorage.removeItem(draftKey(self.docId)); });
              }
            } catch (e) { localStorage.removeItem(draftKey(this.docId)); }
          }
        },

        mk(t) {
          return { id: this.seq++, t: t || 'p', done: false, url: '', name: '', text0: '' };
        },

        /* T9 行式语法 → 块列表（仅初始/恢复草稿时执行，非输入路径）。 */
        parse(src) {
          var lines = String(src).replace(/\r\n/g, '\n').replace(/\r/g, '\n').split('\n');
          var out = [];
          for (var i = 0; i < lines.length; i++) {
            var line = lines[i];
            if (/^```/.test(line)) {
              var body = [];
              i++;
              while (i < lines.length && !/^```/.test(lines[i])) { body.push(lines[i]); i++; }
              var b = this.mk('code'); b.text0 = body.join('\n'); out.push(b);
              continue;
            }
            var t = line.replace(/^\s+|\s+$/g, '');
            if (t === '---') { out.push(this.mk('hr')); continue; }
            if (t.length === 0) { continue; }
            var m;
            if ((m = line.match(/^###\s+(.*)$/))) { var h3 = this.mk('h3'); h3.text0 = m[1]; out.push(h3); continue; }
            if ((m = line.match(/^##\s+(.*)$/))) { var h2 = this.mk('h2'); h2.text0 = m[1]; out.push(h2); continue; }
            if ((m = line.match(/^#\s+(.*)$/))) { var h1 = this.mk('h1'); h1.text0 = m[1]; out.push(h1); continue; }
            if ((m = line.match(/^- \[x\]\s+(.*)$/))) { var td = this.mk('todo'); td.done = true; td.text0 = m[1]; out.push(td); continue; }
            if ((m = line.match(/^- \[\s*\]\s+(.*)$/))) { var td2 = this.mk('todo'); td2.text0 = m[1]; out.push(td2); continue; }
            if ((m = line.match(/^-\s+(.*)$/))) { var ul = this.mk('ul'); ul.text0 = m[1]; out.push(ul); continue; }
            if ((m = line.match(/^\d+\.\s+(.*)$/))) { var ol = this.mk('ol'); ol.text0 = m[1]; out.push(ol); continue; }
            if ((m = line.match(/^>\s+(.*)$/))) { var q = this.mk('quote'); q.text0 = m[1]; out.push(q); continue; }
            if ((m = line.match(/^!\[(.*)\]\((.+)\)$/))) {
              var im = this.mk('img'); im.url = m[2]; im.name = m[1]; out.push(im); continue;
            }
            var p = this.mk('p'); p.text0 = line; out.push(p);
          }
          return out;
        },

        /* 初始文本写入 DOM（结构加载期一次性动作；text0 已填过的跳过）。 */
        fill() {
          var rows = this.$el.querySelectorAll('.be-row');
          for (var i = 0; i < rows.length; i++) {
            var el = rows[i].querySelector('[data-be-text]');
            if (!el || el.getAttribute('data-filled') === '1') { continue; }
            var b = this.blocks[i];
            if (b && typeof b.text0 === 'string') { el.textContent = b.text0; }
            el.setAttribute('data-filled', '1');
          }
        },

        applyDraft(d) {
          this.baseVersion = d.baseVersion || this.baseVersion;
          var t = document.getElementById('be-title');
          if (t && typeof d.title === 'string') { t.value = d.title; }
          this.blocks = this.parse(d.content);
          if (this.blocks.length === 0) { this.blocks = [this.mk('p')]; }
          var self = this;
          this.$nextTick(function () {
            var rows = self.$el.querySelectorAll('.be-row');
            for (var i = 0; i < rows.length; i++) {
              var el = rows[i].querySelector('[data-be-text]');
              if (el) { el.setAttribute('data-filled', '0'); }
            }
            self.fill();
          });
          this.status('已恢复草稿');
        },

        /* ---- 输入路径：只 mark，零状态重写 ---- */
        mark() {
          this.dirty = true;
          if (this.composing) { return; }
          var self = this;
          if (this.draftTimer) { clearTimeout(this.draftTimer); }
          this.draftTimer = setTimeout(function () { self.saveDraft(); }, 1000);
          if (this.conflict) { return; }
          if (this.timer) { clearTimeout(this.timer); }
          this.timer = setTimeout(function () { self.save(false); }, 2000);
        },

        saveDraft() {
          try {
            localStorage.setItem(draftKey(this.docId), JSON.stringify({
              title: this.titleEl() ? this.titleEl().value : '',
              content: this.collect(),
              baseVersion: this.baseVersion,
              at: new Date().toLocaleTimeString()
            }));
          } catch (e) { /* 存储满等异常不拖累编辑 */ }
        },

        titleEl() { return document.getElementById('be-title'); },

        status(text) {
          var el = document.getElementById('be-status');
          if (el) { el.textContent = text || ''; }
        },

        /* ---- collect：按块类型拼回行式语法（读 textContent） ---- */
        collect() {
          var out = [];
          var olN = 0;
          var rows = this.$el.querySelectorAll('.be-row');
          for (var i = 0; i < rows.length && i < this.blocks.length; i++) {
            var b = this.blocks[i];
            var t = b.t;
            if (t === 'img') {
              olN = 0;
              out.push('![' + (b.name || 'img') + '](' + (b.url || '') + ')');
              continue;
            }
            if (t === 'hr') { olN = 0; out.push('---'); continue; }
            var el = rows[i].querySelector('[data-be-text]');
            var text = el ? el.textContent : '';
            if (t === 'code') { olN = 0; out.push('```\n' + text + '\n```'); continue; }
            if (t === 'h1') { olN = 0; out.push('# ' + text); continue; }
            if (t === 'h2') { olN = 0; out.push('## ' + text); continue; }
            if (t === 'h3') { olN = 0; out.push('### ' + text); continue; }
            if (t === 'ul') { olN = 0; out.push('- ' + text); continue; }
            if (t === 'ol') { olN = olN + 1; out.push(olN + '. ' + text); continue; }
            if (t === 'todo') { olN = 0; out.push('- [' + (b.done ? 'x' : ' ') + '] ' + text); continue; }
            if (t === 'quote') { olN = 0; out.push('> ' + text); continue; }
            olN = 0; out.push(text);
          }
          return out.join('\n');
        },

        /* ---- 保存：乐观锁，409 保留草稿 ---- */
        save(manual) {
          if (this.composing) { return; }
          if (this.timer) { clearTimeout(this.timer); }
          var self = this;
          var content = this.collect();
          var flat = content.replace(/\s+/g, '');
          var title = this.titleEl() ? this.titleEl().value.replace(/^\s+|\s+$/g, '') : '';
          if (title.length === 0) { if (manual) { this.say('请填写文档标题', true); } return; }
          if (flat.length === 0) { if (manual) { this.say('内容不能为空', true); } return; }
          this.saveDraft();
          var body = 'id=' + this.docId + '&spaceId=' + this.spaceId
            + '&baseVersion=' + this.baseVersion
            + '&title=' + encodeURIComponent(title)
            + '&content=' + encodeURIComponent(content);
          fetch('/admin/oa/docs/save', {
            method: 'POST', credentials: 'same-origin',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8' },
            body: body
          }).then(function (r) {
            return r.json().catch(function () { return null; }).then(function (j) { return { ok: r.ok, st: r.status, j: j }; });
          }).then(function (res) {
            if (res && res.ok && res.j && res.j.code === '0000') {
              self.dirty = false; self.conflict = false;
              self.baseVersion = self.baseVersion + 1;
              localStorage.removeItem(draftKey(self.docId));
              if (self.docId > 0) {
                self.status('已保存 v' + self.baseVersion + ' ' + new Date().toLocaleTimeString());
                if (manual) { self.say('已保存 v' + self.baseVersion); }
              } else {
                self.say('文档已创建');
                window.location.href = '/admin/oa/docs?spaceId=' + self.spaceId;
              }
            } else if (res && res.st === 409) {
              self.conflict = true;
              self.say('文档已被他人修改，你的内容已保留为本地草稿，请刷新对比后再保存', true);
              self.status('版本冲突（v' + self.baseVersion + ' 提交被拒）');
            } else {
              self.say((res && res.j && res.j.msg) || '保存失败', true);
            }
          }).catch(function () { self.say('保存失败（网络）', true); });
        },

        say(msg, bad) {
          if (window.Layer) { Layer.msg(msg, bad ? 'bad' : 'ok'); }
        },

        /* ---- 块结构操作（Alpine 管结构，文本随节点搬移） ---- */
        add(t, afterId) {
          var b = this.mk(t || 'p');
          var at = this.blocks.length;
          for (var i = 0; i < this.blocks.length; i++) {
            if (this.blocks[i].id === afterId) { at = i + 1; break; }
          }
          this.blocks.splice(at, 0, b);
          var self = this;
          this.$nextTick(function () { self.fill(); self.focus(b.id); });
          return b;
        },

        lastId() { return this.blocks.length ? this.blocks[this.blocks.length - 1].id : 0; },

        del(b) {
          if (this.blocks.length <= 1) { this.say('至少保留一个块', true); return; }
          var at = -1;
          for (var i = 0; i < this.blocks.length; i++) { if (this.blocks[i].id === b.id) { at = i; break; } }
          if (at >= 0) { this.blocks.splice(at, 1); this.mark(); }
        },

        move(b, dir) {
          var at = -1;
          for (var i = 0; i < this.blocks.length; i++) { if (this.blocks[i].id === b.id) { at = i; break; } }
          var to = at + dir;
          if (at < 0 || to < 0 || to >= this.blocks.length) { return; }
          var tmp = this.blocks[at];
          this.blocks[at] = this.blocks[to];
          this.blocks[to] = tmp;
          this.mark();
          var self = this;
          this.$nextTick(function () { self.focus(b.id); });
        },

        row(bid) {
          var rows = this.$el.querySelectorAll('.be-row');
          for (var i = 0; i < rows.length && i < this.blocks.length; i++) {
            if (this.blocks[i].id === bid) { return rows[i]; }
          }
          return null;
        },

        focus(bid) {
          var r = this.row(bid);
          if (!r) { return; }
          var el = r.querySelector('[data-be-text]');
          if (el) { el.focus(); }
        },

        /* ---- 键盘/粘贴 ---- */
        key(e, b) {
          if (e.keyCode === 229) { return; }
          if (b.t === 'code') {
            if (e.key === 'Enter') {
              e.preventDefault();
              document.execCommand('insertText', false, '\n');
              this.mark();
            }
            return;
          }
          if (e.key === 'Enter' && !e.shiftKey) {
            e.preventDefault();
            var t = 'p';
            if (b.t === 'ul' || b.t === 'ol' || b.t === 'todo' || b.t === 'quote') { t = b.t; }
            this.add(t, b.id);
          }
        },

        paste(e) {
          e.preventDefault();
          var text = (e.clipboardData || window.clipboardData).getData('text/plain') || '';
          document.execCommand('insertText', false, text);
          this.mark();
        },

        /* ---- 图片块：octet-stream 直传（attach.js 同款协议） ---- */
        upload(b, ev) {
          var input = ev.target;
          var f = input.files && input.files[0];
          input.value = '';
          if (!f) { return; }
          var self = this;
          this.status('图片上传中…');
          fetch('/admin/oa/attachments/upload?module=oa_doc&relatedId=' + this.docId
            + '&name=' + encodeURIComponent(f.name)
            + '&mime=' + encodeURIComponent(f.type || ''), {
            method: 'POST', credentials: 'same-origin',
            headers: { 'Content-Type': 'application/octet-stream' },
            body: f
          }).then(function (r) { return r.json(); }).then(function (j) {
            if (j && j.code === '0000') {
              b.url = '/admin/oa/attachments/download?id=' + j.id;
              b.name = f.name.replace(/\.[^.]*$/, '');
              self.status('图片已上传');
              self.mark();
            } else { self.say((j && j.msg) || '图片上传失败', true); }
          }).catch(function () { self.say('图片上传失败', true); });
        }
      };
    });
  });
})();
