/* Double-submit token transport for framework forms, htmx and same-origin fetch. */
(function () {
  'use strict';

  function token() {
    var parts = document.cookie.split(';');
    for (var i = 0; i < parts.length; i++) {
      var item = parts[i].trim();
      if (item.indexOf('csrf_token=') === 0) {
        return item.substring(11);
      }
    }
    return '';
  }

  function unsafe(method) {
    return !/^(GET|HEAD|OPTIONS)$/i.test(method || 'GET');
  }

  function sameOrigin(target) {
    return new URL(target || location.href, location.href).origin === location.origin;
  }

  function prepare(form) {
    var method = form.getAttribute('method') || (form.hasAttribute('data-submit') ? 'POST' : 'GET');
    if (!unsafe(method) || !sameOrigin(form.action)) { return; }
    var value = token();
    if (!value) { return; }
    var input = form.querySelector('input[name="_csrf"]');
    if (!input) {
      input = document.createElement('input');
      input.type = 'hidden';
      input.name = '_csrf';
      form.appendChild(input);
    }
    input.value = value;
  }

  // Capture runs before framework submit handlers construct FormData; also handles new fragments.
  document.addEventListener('submit', function (event) {
    if (event.target && event.target.tagName === 'FORM') { prepare(event.target); }
  }, true);

  document.addEventListener('htmx:configRequest', function (event) {
    if (unsafe(event.detail.verb) && sameOrigin(event.detail.path)) {
      var value = token();
      if (value) { event.detail.headers['X-CSRF-Token'] = value; }
    }
  });

  // Admin actions, uploads and application fetch calls share one transport rule.
  var originalFetch = window.fetch;
  if (originalFetch) {
    window.fetch = function (input, init) {
      var request = typeof Request !== 'undefined' && input instanceof Request ? input : null;
      var options = init || {};
      var method = options.method || (request ? request.method : 'GET');
      var target = request ? request.url : input;
      var credentials = options.credentials || (request ? request.credentials : 'same-origin');
      if (unsafe(method) && credentials !== 'omit' && sameOrigin(target)) {
        var value = token();
        if (value) {
          options = Object.assign({}, options);
          options.headers = new Headers(options.headers || (request ? request.headers : undefined));
          options.headers.set('X-CSRF-Token', value);
        }
      }
      return originalFetch.call(this, input, options);
    };
  }
})();
