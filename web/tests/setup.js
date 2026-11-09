import { vi } from 'vitest';
import jQuery from 'jquery';

const $ = jQuery;
$.fx.off = true;
window.$ = $;
window.jQuery = $;
globalThis.$ = $;
globalThis.jQuery = $;

$.fn.modal = vi.fn(function(action) {
  if (action === 'show') {
    $(this).addClass('show').css('display', 'block');
  } else if (action === 'hide') {
    $(this).removeClass('show').css('display', 'none');
  }
  return this;
});

window.bootstrap = {
  Modal: class {
    constructor(el) { this.el = el; }
    show() { $(this.el).addClass('show').css('display', 'block'); }
    hide() { $(this.el).removeClass('show').css('display', 'none'); }
  },
  Toast: class {
    constructor(el, opts) { this.el = el; this.opts = opts; }
    show() { $(this.el).addClass('show'); }
    hide() { $(this.el).removeClass('show'); }
  }
};
window.bootstrap.Modal.getInstance = (el) => new window.bootstrap.Modal(el);
globalThis.bootstrap = window.bootstrap;
