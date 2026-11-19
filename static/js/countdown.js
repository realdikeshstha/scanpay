// Visual countdown for the 2-minute authorization. Expiry is enforced on the
// server; this only tells the user.
(function () {
  'use strict';
  var el = document.getElementById('seconds-left');
  if (!el) return;
  var remaining = parseInt(el.getAttribute('data-seconds'), 10) || 0;
  var button = document.querySelector('form[action="/pay/confirm"] button');

  var timer = window.setInterval(function () {
    remaining = Math.max(0, remaining - 1);
    el.textContent = String(remaining);
    if (remaining === 0) {
      window.clearInterval(timer);
      if (button) {
        button.disabled = true;
        button.textContent = 'Authorization expired';
      }
    }
  }, 1000);
})();
