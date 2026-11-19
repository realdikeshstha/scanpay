// Camera / image QR scanning with jsQR. Decoded text is only ever placed in a
// form field (never into HTML), and the server re-verifies everything.
(function () {
  'use strict';

  var MAX_PAYLOAD = 128;
  var PREFIX = 'scanpay:v1:';
  var MAX_FRAME_WIDTH = 640;

  var video = document.getElementById('scan-video');
  var canvas = document.getElementById('scan-canvas');
  var statusEl = document.getElementById('scan-status');
  var startBtn = document.getElementById('scan-start');
  var fileInput = document.getElementById('scan-file');
  var form = document.getElementById('scan-form');
  var qrField = document.getElementById('scan-qr');
  var ctx = canvas.getContext('2d', { willReadFrequently: true });

  var stream = null;
  var done = false;

  function setStatus(text) {
    statusEl.textContent = text;
  }

  function stopCamera() {
    if (stream) {
      stream.getTracks().forEach(function (t) { t.stop(); });
      stream = null;
    }
  }

  // Client-side check is only for a friendlier message; the server decides.
  function accept(text) {
    if (done) return;
    if (typeof text !== 'string' || text.length > MAX_PAYLOAD || text.indexOf(PREFIX) !== 0) {
      setStatus('That QR code is not a ScanPay merchant code. Keep scanning.');
      return;
    }
    done = true;
    stopCamera();
    qrField.value = text;
    setStatus('QR code found. Verifying merchant...');
    form.submit();
  }

  function decodeCanvas(width, height, inversion) {
    var image = ctx.getImageData(0, 0, width, height);
    var code = window.jsQR(image.data, width, height, { inversionAttempts: inversion });
    return code ? code.data : null;
  }

  function tick() {
    if (done || !stream) return;
    if (video.readyState === video.HAVE_ENOUGH_DATA) {
      var scale = Math.min(1, MAX_FRAME_WIDTH / video.videoWidth);
      var w = Math.floor(video.videoWidth * scale);
      var h = Math.floor(video.videoHeight * scale);
      canvas.width = w;
      canvas.height = h;
      ctx.drawImage(video, 0, 0, w, h);
      var text = decodeCanvas(w, h, 'dontInvert');
      if (text) accept(text);
    }
    window.requestAnimationFrame(tick);
  }

  function startCamera() {
    if (stream) return;
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      setStatus('Camera not available here (HTTPS is required). Upload an image or paste the code instead.');
      return;
    }
    setStatus('Starting camera...');
    navigator.mediaDevices.getUserMedia({ video: { facingMode: 'environment' }, audio: false })
      .then(function (s) {
        stream = s;
        video.srcObject = s;
        return video.play();
      })
      .then(function () {
        setStatus('Scanning... hold the QR code steady inside the frame.');
        window.requestAnimationFrame(tick);
      })
      .catch(function () {
        setStatus('Could not open the camera. Upload an image or paste the code instead.');
      });
  }

  fileInput.addEventListener('change', function () {
    var file = fileInput.files && fileInput.files[0];
    if (!file) return;
    var url = URL.createObjectURL(file);
    var img = new Image();
    img.onload = function () {
      var scale = Math.min(1, 1024 / Math.max(img.width, img.height));
      var w = Math.floor(img.width * scale);
      var h = Math.floor(img.height * scale);
      canvas.width = w;
      canvas.height = h;
      ctx.drawImage(img, 0, 0, w, h);
      URL.revokeObjectURL(url);
      var text = decodeCanvas(w, h, 'attemptBoth');
      if (text) accept(text);
      else setStatus('No QR code found in that image.');
    };
    img.onerror = function () {
      URL.revokeObjectURL(url);
      setStatus('That file could not be read as an image.');
    };
    img.src = url;
  });

  startBtn.addEventListener('click', startCamera);
  window.addEventListener('pagehide', stopCamera);
  startCamera();
})();
