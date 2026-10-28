/* global bootstrap, append, init_plot, draw_plot */
/* exported liveDataStatus, liveDataStatusEl, liveDataStatusAz */
/**
 * See Javascript ES6 format for code style.
 *
 * https://www.w3schools.com/Js/js_es6.asp
 */

'use strict';
/**
 * Type of toasts following bootstrap color coding.
 */
const ToastType = Object.freeze({
  Primary: 0,
  Secondary: 1,
  Success: 2,
  Info: 3,
  Warning: 4,
  Danger: 5,
  Light: 6,
  Dark: 7
});

const AppStatus = Object.freeze({
  Stopped: 1,
  Running: 2,
  Paused: 3,
  Calibrated: 4
});

/**
 * Commands available on server side, keep in sync with server_command_e.
 */
const ServerCommand = Object.freeze({
  CmdNull: 0,
  CmdStart: 1,
  CmdStop: 2,
  CmdPause: 3,
  CmdCheck: 4,
  CmdPan: 5,
  CmdTilt: 6,
  CmdHome: 7,
  CmdCalib: 8
});

/* Plot selection: index into frequency list, elevation and azimut in degrees */
let liveDataStatus = 0;
let liveDataStatusEl = 0;
let liveDataStatusAz = 0;
let lastTotalTime = 0;

/* Websocket handle */
let wsocket = null;

function byId(id) {
  return document.getElementById(id);
}

/**
 * Set text of an element and its twin in the 3D dialog (id + "3d").
 */
function setBoth(id, text) {
  byId(id).textContent = text;
  byId(`${id}3d`).textContent = text;
}

function setProgress(percent) {
  ['progress', 'progress3d'].forEach((id) => {
    const bar = byId(id);
    bar.style.width = `${percent}%`;
    bar.textContent = `${percent}%`;
    bar.setAttribute('aria-valuenow', `${percent}`);
  });
}

function formatTime(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = String(Math.floor((seconds % 3600) / 60)).padStart(2, '0');
  const s = String(Math.floor(seconds % 60)).padStart(2, '0');
  return `${h}:${m}:${s}`;
}

function setTimeButtonState(running) {
  ['time', 'time3d'].forEach((id) => {
    byId(id).classList.replace(
      running ? 'btn-outline-success' : 'btn-outline-danger',
      running ? 'btn-outline-danger' : 'btn-outline-success'
    );
  });
}

/**
 * A long running server operation ended, see app_done on server side.
 */
function handleDone(what) {
  switch (what) {
    case 'scan':
      setBoth('time', `Total Time: ${formatTime(lastTotalTime)}`);
      setTimeButtonState(false);
      break;
    case 'home':
      byId('button_home').classList.remove('disabled');
      break;
    case 'check':
      byId('button_check').classList.remove('disabled');
      break;
    default:
      break;
  }
}

/*
 * Connects with port 10024
 */
function wsConnect() {
  console.info(`Location hostname: ${location.hostname}`);
  wsocket = new WebSocket(`ws://${location.hostname}:10024`, 'broadcast');

  /* Callback when something was received from server */
  wsocket.onmessage = (e) => {
    const msg = JSON.parse(e.data);
    /* Ensure we have the right json format */
    if (msg === null || typeof msg !== 'object' || !('cmd' in msg) || !('data' in msg)) {
      return;
    }
    switch (msg.cmd) {
      case 'primary':
        showToast(msg.data, ToastType.Primary);
        break;
      case 'secondary':
        showToast(msg.data, ToastType.Secondary);
        break;
      case 'success':
        showToast(msg.data, ToastType.Success);
        break;
      case 'info':
        showToast(msg.data, ToastType.Info);
        break;
      case 'warning':
        showToast(msg.data, ToastType.Warning);
        break;
      case 'danger':
        showToast(msg.data, ToastType.Danger);
        break;
      case 'light':
        showToast(msg.data, ToastType.Light);
        break;
      case 'dark':
        showToast(msg.data, ToastType.Dark);
        break;
      case 'status':
        app_status_handler(msg.data);
        break;
      case 'done':
        handleDone(msg.data);
        break;
      case 'point':
        append(msg.data[0], msg.data[1], msg.data[2], msg.data[3]);
        setBoth('currentAzimut', `Current Azimut: ${msg.data[0]}`);
        setBoth('currentElevation', `Current Elevation: ${msg.data[1]}`);
        break;
      case 'progress':
        setProgress(msg.data[0]);
        lastTotalTime = msg.data[1];
        setBoth('time', `Time Left: ${formatTime(msg.data[2])}`);
        break;
      default:
        break;
    }
  };
  /* Connection closed or lost callback, try reconnect every 5 seconds */
  wsocket.onclose = () => {
    showToast('No Connection', ToastType.Danger);
    wsocket = null;

    setTimeout(() => {
      wsConnect();
    }, 5000);
  };

  /* Callback for successful connection */
  wsocket.onopen = () => {
    showToast('Connected with websocket server', ToastType.Success);
  };
}

/**
 * Show toast element.
 */
function showToast(message, type) {
  /* Create toast */
  const toast = document.createElement('div');
  /* Add required bootstrap classes and attributes */
  toast.classList.add('toast');
  toast.classList.add('toast_effects_open');
  toast.setAttribute('role', 'alert');
  toast.setAttribute('aria-live', 'assertive');
  toast.setAttribute('aria-atomic', 'true');
  /* Create toast header and body */
  const toastHeader = document.createElement('div');
  toastHeader.classList.add('d-flex');

  const toastBody = document.createElement('div');
  toastBody.classList.add('toast-body');
  toastBody.textContent = message;

  toast.appendChild(toastHeader);
  toastHeader.appendChild(toastBody);
  /* Add close button */
  const toastButton = document.createElement('button');
  toastButton.classList.add('btn-close');
  toastButton.classList.add('me-2');
  toastButton.classList.add('m-auto');
  toastButton.setAttribute('data-bs-dismiss', 'toast');
  toastButton.setAttribute('aria-label', 'Close');
  toastHeader.appendChild(toastButton);
  /* Add toast color */
  switch (type) {
    case ToastType.Primary:
      toast.classList.add('text-bg-primary');
      toastButton.classList.add('btn-close-white');
      break;
    case ToastType.Secondary:
      toast.classList.add('text-bg-secondary');
      toastButton.classList.add('btn-close-white');
      break;
    case ToastType.Success:
      toast.classList.add('text-bg-success');
      toastButton.classList.add('btn-close-white');
      break;
    case ToastType.Info:
      toast.classList.add('text-bg-info');
      break;
    case ToastType.Warning:
      toast.classList.add('text-bg-warning');
      break;
    case ToastType.Danger:
      toast.classList.add('text-bg-danger');
      toastButton.classList.add('btn-close-white');
      break;
    case ToastType.Light:
      toast.classList.add('text-bg-light');
      break;
    case ToastType.Dark:
      toast.classList.add('text-bg-dark');
      toastButton.classList.add('btn-close-white');
      break;
    default:
      break;
  }
  /* Add fade out effect on toast when closing */
  toast.addEventListener('hide.bs.toast', (e) => {
    /* Do not execute default event handler in bootstrap */
    e.preventDefault();
    /* Wait for fade out animation end */
    e.currentTarget.addEventListener('animationend', (e) => {
      /* Then remove toast from DOM list */
      byId('toastContainer').removeChild(e.currentTarget);
    });
    /* Initiate close animation when toast shall be hidden */
    e.currentTarget.classList.replace(
      'toast_effects_open',
      'toast_effects_close'
    );
    /* Ensure we listen to event only one time */
    e.currentTarget.removeEventListener('hide.bs.toast', null);
  });

  /* Add to DOM, initialize toast via bootstrap and show */
  byId('toastContainer').appendChild(toast);
  const t = new bootstrap.Toast(toast, {
    autohide: true,
    animation: false,
    delay: 3000
  });
  t.show();
}

/**
 * Send server command. Returns false if there is no connection.
 */
function sendServerCommand(data) {
  if (wsocket === null || wsocket.readyState !== WebSocket.OPEN) {
    showToast('No Connection', ToastType.Danger);
    return false;
  }
  wsocket.send(JSON.stringify(data));
  return true;
}

/**
 * Mark input elements as valid or invalid, returns ok.
 */
function mark(ids, ok) {
  ids.forEach((id) => {
    const classes = byId(id).classList;
    classes.remove('is-valid', 'is-invalid');
    classes.add(ok ? 'is-valid' : 'is-invalid');
  });
  return ok;
}

/**
 * Split a comma separated list into numbers, empty entries become NaN.
 */
function parseList(text) {
  return text.split(',').map((v) => (v.trim() === '' ? NaN : Number(v)));
}

function isIntIn(value, min, max) {
  return Number.isInteger(value) && value >= min && value <= max;
}

/**
 * Validate the configuration form. Returns the server command frame or false.
 * Rules match parse_calibration() on the server.
 */
function createDataFrame() {
  const num = (id) => byId(id).valueAsNumber;
  const filename = byId('inputFilename').value;
  const azAngle = num('inputAzimutAngle');
  const azResolution = num('inputAzimutResolution');
  const elStart = num('inputElevStartAngle');
  const elStop = num('inputElevStopAngle');
  const elResolution = num('inputElevResolution');
  const startFrequency = num('inputStartFrequency');
  const stopFrequency = num('inputStopFrequency');
  const testFrequency = parseList(byId('inputMeasureFrequency').value);
  const refGain = parseList(byId('inputGainRefAntenna').value);

  /* Single field checks */
  let ok = true;
  [
    [['inputFilename'], /^[A-Za-z0-9_-]{0,24}$/.test(filename)],
    [['inputAzimutAngle'], isIntIn(azAngle, 0, 360) && azAngle % 2 === 0],
    [['inputAzimutResolution'], isIntIn(azResolution, 2, 360)],
    [['inputElevStartAngle'], isIntIn(elStart, -90, 90)],
    [['inputElevStopAngle'], isIntIn(elStop, -90, 90)],
    [['inputElevResolution'], isIntIn(elResolution, 2, 90)],
    [['inputStartFrequency'], isIntIn(startFrequency, 1, 7000)],
    [['inputStopFrequency'], isIntIn(stopFrequency, 1, 7000)],
    [['inputMeasureFrequency'], testFrequency.every((f) => isIntIn(f, startFrequency, stopFrequency)) &&
      new Set(testFrequency).size === testFrequency.length],
    [['inputGainRefAntenna'], refGain.every((g) => g >= -100 && g <= 100)]
  ].forEach(([ids, valid]) => {
    ok = mark(ids, valid) && ok;
  });

  /* Relations between fields, only ever mark invalid */
  [
    [['inputAzimutAngle', 'inputAzimutResolution'], azAngle % azResolution === 0],
    [['inputElevStartAngle', 'inputElevStopAngle'], elStart <= elStop],
    [['inputElevStartAngle', 'inputElevStopAngle', 'inputElevResolution'], Math.abs(elStop - elStart) % elResolution === 0],
    [['inputStartFrequency', 'inputStopFrequency'], startFrequency < stopFrequency],
    /* One reference gain for every measurement frequency */
    [['inputMeasureFrequency', 'inputGainRefAntenna'], testFrequency.length === refGain.length]
  ].forEach(([ids, valid]) => {
    if (!valid) {
      ok = mark(ids, false);
    }
  });
  if (!ok) {
    return false;
  }

  /* Server treats empty or all zero names as "use timestamp" */
  return [ServerCommand.CmdCalib, filename || '0'.repeat(24), azAngle, azResolution, elStart, elStop, elResolution,
    startFrequency, stopFrequency, testFrequency, refGain, testFrequency.length];
}

function createPanData() {
  const pan = byId('inputPanPosition').valueAsNumber;
  return mark(['inputPanPosition'], isIntIn(pan, 0, 360)) ? pan : false;
}

function createTiltData() {
  const tilt = byId('inputTiltPosition').valueAsNumber;
  return mark(['inputTiltPosition'], isIntIn(tilt, -90, 90)) ? tilt : false;
}

function app_status_handler(status) {
  const enable = (id, on) => byId(id).classList.toggle('disabled', !on);
  const has3dData = () => byId('elevationDropdown').childElementCount > 1;
  switch (status) {
    case AppStatus.Stopped:
      enable('button_start', false);
      enable('button_pause', false);
      enable('button_stop', false);
      enable('button_calib', true);
      enable('button_headcmd', true);
      enable('button_data', true);
      enable('button_data3d', has3dData());
      byId('button_pause').classList.replace('btn-warning', 'btn-light');
      break;
    case AppStatus.Calibrated:
      enable('button_start', true);
      break;
    case AppStatus.Running:
      enable('button_calib', false);
      enable('button_start', false);
      enable('button_headcmd', false);
      enable('button_stop', true);
      enable('button_data', true);
      enable('button_data3d', has3dData());
      enable('button_pause', true);
      byId('button_pause').classList.replace('btn-warning', 'btn-light');
      break;
    case AppStatus.Paused:
      byId('button_pause').classList.replace('btn-light', 'btn-warning');
      break;
    default:
      break;
  }
}

/**
 * Replace the entries of a dropdown list. items: [{ value, label }].
 */
function fillDropdown(listId, items, onClick) {
  const list = byId(listId);
  list.replaceChildren();
  items.forEach(({ value, label }) => {
    const li = document.createElement('li');
    const button = document.createElement('button');
    button.classList.add('dropdown-item');
    button.setAttribute('type', 'button');
    button.setAttribute('value', `${value}`);
    button.textContent = label;
    button.addEventListener('click', () => onClick(value));
    li.appendChild(button);
    list.appendChild(li);
  });
}

function init_dropdown(frequency, len, elStart, elStop, elRes, azSector, azRes) {
  const selectFrequency = (index) => {
    liveDataStatus = index;
    draw_plot();
  };
  const frequencies = frequency.slice(0, len).map((f, i) => ({ value: i, label: `${f}MHz` }));
  fillDropdown('frequencyDropdown', frequencies, selectFrequency);
  fillDropdown('frequencyDropdown3d', frequencies, selectFrequency);
  liveDataStatus = 0;

  const elevations = [];
  for (let el = elStart; el <= elStop; el += elRes) {
    elevations.push({ value: el, label: `${el}°` });
  }
  fillDropdown('elevationDropdown', elevations, (el) => {
    liveDataStatusEl = el;
    draw_plot();
  });
  liveDataStatusEl = elStart;

  /* Sector is centered on 0°, a full circle starts at 180° and must not repeat its first point */
  const azStart = 360 - azSector / 2;
  const azEnd = azSector / 2 + 360 - (azSector === 360 ? 1 : 0);
  const azimuths = [];
  for (let az = azStart; az <= azEnd; az += azRes) {
    azimuths.push({ value: az % 360, label: `${az % 360}°` });
  }
  fillDropdown('azimutDropdown', azimuths, (az) => {
    liveDataStatusAz = az;
    draw_plot();
  });
  liveDataStatusAz = azStart % 360;
}

/**
 * Start application as soon as DOM is fully loaded
 */
document.addEventListener('DOMContentLoaded', () => {
  /* Add click event listeners to each button */
  byId('button_calib').addEventListener('click', () => {
    const data = createDataFrame();
    if (data === false) {
      return;
    }
    if (sendServerCommand(data)) {
      setProgress(0);
      init_plot(data[9], data[11]);
      init_dropdown(data[9], data[11], data[4], data[5], data[6], data[2], data[3]);
    }
  });
  byId('button_start').addEventListener('click', () => {
    if (sendServerCommand([ServerCommand.CmdStart, 1])) {
      setTimeButtonState(true);
    }
  });
  byId('button_pause').addEventListener('click', () => {
    sendServerCommand([ServerCommand.CmdPause, 2]);
  });
  byId('button_stop').addEventListener('click', () => {
    if (sendServerCommand([ServerCommand.CmdStop, 3])) {
      setBoth('time', 'Time Left: 0:00:00');
    }
  });
  byId('button_pan').addEventListener('click', () => {
    const data = createPanData();
    if (data !== false) {
      sendServerCommand([ServerCommand.CmdPan, data]);
    }
  });
  byId('button_tilt').addEventListener('click', () => {
    const data = createTiltData();
    if (data !== false) {
      sendServerCommand([ServerCommand.CmdTilt, data]);
    }
  });
  byId('button_check').addEventListener('click', () => {
    if (sendServerCommand([ServerCommand.CmdCheck, 6])) {
      byId('button_check').classList.add('disabled');
    }
  });
  byId('button_home').addEventListener('click', () => {
    if (sendServerCommand([ServerCommand.CmdHome, 7])) {
      byId('button_home').classList.add('disabled');
    }
  });
  init_plot([], 0);
  wsConnect();
});
