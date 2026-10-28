/*
 * OpenTTD Underground player guide: hash routing, contents and search.
 * Pages are <template id="..."> elements in index.html, so this works from
 * GitHub Pages and from a local file, with no network requests.
 */
(function () {
  'use strict';

  var DEFAULT_PAGE = 'overview';
  var templates = Array.prototype.slice.call(document.querySelectorAll('template[id]'));
  var pages = templates.map(function (tpl) {
    return {
      id: tpl.id,
      title: tpl.getAttribute('data-title') || tpl.id,
      group: tpl.getAttribute('data-group') || 'Guide',
      template: tpl,
      text: null
    };
  });
  var byId = {};
  pages.forEach(function (p) { byId[p.id] = p; });

  var content = document.getElementById('content');
  var toc = document.getElementById('toc');
  var sidebar = document.getElementById('sidebar');
  var menuButton = document.getElementById('menu-button');
  var search = document.getElementById('search');
  var results = document.getElementById('search-results');

  /* Build the contents, grouped in the order the groups first appear. */
  function buildToc() {
    var groups = [];
    var lists = {};
    pages.forEach(function (p) {
      if (!lists[p.group]) {
        lists[p.group] = document.createElement('ul');
        groups.push(p.group);
      }
      var li = document.createElement('li');
      var a = document.createElement('a');
      a.href = '#/' + p.id;
      a.textContent = p.title;
      a.setAttribute('data-page', p.id);
      li.appendChild(a);
      lists[p.group].appendChild(li);
    });
    groups.forEach(function (name) {
      var label = document.createElement('div');
      label.className = 'group';
      label.textContent = name;
      toc.appendChild(label);
      toc.appendChild(lists[name]);
    });
  }

  function currentId() {
    var id = (window.location.hash || '').replace(/^#\/?/, '');
    return byId[id] ? id : DEFAULT_PAGE;
  }

  function closeMenu() {
    document.body.classList.remove('nav-open');
    menuButton.setAttribute('aria-expanded', 'false');
  }

  function render() {
    var id = currentId();
    var page = byId[id];

    content.textContent = '';
    content.appendChild(document.importNode(page.template.content, true));
    document.title = page.title + ' | OpenTTD Underground';

    Array.prototype.forEach.call(toc.querySelectorAll('a[data-page]'), function (a) {
      if (a.getAttribute('data-page') === id) {
        a.setAttribute('aria-current', 'page');
      } else {
        a.removeAttribute('aria-current');
      }
    });

    window.scrollTo(0, 0);
    content.focus({ preventScroll: true });
    closeMenu();
  }

  /* Plain text of each page, read once from its template. A space goes after
     each block or cell, so words of neighbouring cells do not run together. */
  function textOf(page) {
    if (page.text === null) {
      var holder = document.createElement('div');
      holder.appendChild(document.importNode(page.template.content, true));
      holder.querySelectorAll('h1,h2,h3,p,li,dt,dd,td,th,pre,figcaption').forEach(function (el) {
        el.insertAdjacentText('afterend', ' ');
      });
      page.text = holder.textContent.replace(/\s+/g, ' ').trim();
    }
    return page.text;
  }

  function snippetAround(text, index, length) {
    var start = Math.max(0, index - 60);
    var end = Math.min(text.length, index + length + 90);
    return (start > 0 ? '...' : '') + text.slice(start, end).trim() + (end < text.length ? '...' : '');
  }

  function runSearch(query) {
    results.textContent = '';
    var terms = query.toLowerCase().split(/\s+/).filter(Boolean);
    if (terms.length === 0) {
      results.hidden = true;
      return;
    }
    results.hidden = false;

    var found = 0;
    pages.forEach(function (page) {
      var text = textOf(page);
      var lower = text.toLowerCase();
      var first = -1;
      for (var i = 0; i < terms.length; i++) {
        var at = lower.indexOf(terms[i]);
        if (at === -1) return;
        if (first === -1 || at < first) first = at;
      }
      found++;
      var link = document.createElement('a');
      link.href = '#/' + page.id;
      link.textContent = page.title;
      var snip = document.createElement('span');
      snip.className = 'snippet';
      snip.textContent = snippetAround(text, first, terms[0].length);
      link.appendChild(snip);
      results.appendChild(link);
    });

    if (found === 0) {
      var none = document.createElement('div');
      none.className = 'none';
      none.textContent = 'No page contains all of these words.';
      results.appendChild(none);
    }
  }

  search.addEventListener('input', function () { runSearch(search.value); });
  search.addEventListener('keydown', function (event) {
    if (event.key === 'Escape') {
      search.value = '';
      runSearch('');
    }
  });

  /* Clicking a result or a contents link closes the phone menu and opens the page. */
  results.addEventListener('click', function (event) {
    if (event.target.closest('a')) {
      search.value = '';
      runSearch('');
    }
  });

  menuButton.addEventListener('click', function () {
    var open = !document.body.classList.contains('nav-open');
    document.body.classList.toggle('nav-open', open);
    menuButton.setAttribute('aria-expanded', open ? 'true' : 'false');
    if (open) search.focus({ preventScroll: true });
  });

  /* A link to another page inside the guide uses the same routing. */
  content.addEventListener('click', function (event) {
    var a = event.target.closest('a[href^="#/"]');
    if (a && a.getAttribute('href') === window.location.hash) {
      event.preventDefault();
      render();
    }
  });

  window.addEventListener('hashchange', render);
  buildToc();
  render();
})();
