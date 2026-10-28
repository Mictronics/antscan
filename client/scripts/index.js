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
 * Commands available on server side.
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
  CmdCalib: 8,
  CmdStatus: 9,
  CmdResult: 10
});

let liveDataStatus = 0;
let liveDataStatusEl = 0;
let liveDataStatusAz = 0;
let totalTime, totalSeconds, totalMinutes, totalHours, timeLeft, secondsLeft, minutesLeft, hoursLeft;

function make_time(){
  
}

/* Websocket handle */
let wsocket = null;
/* Check for websocket API availability */
const connection = navigator.connection || navigator.mozConnection || null;
if (connection === null) {
  console.error('Network Information API not supported.');
}

/*
 * Connects with port 10024
 */
function wsConnect() {
  console.info(`Location hostname: ${location.hostname}`);
  wsocket = new WebSocket(`ws://${location.hostname}:10024`, 'broadcast');

  /* Callback when something was received from server */
  wsocket.onmessage = (e) => {
    let msg = JSON.parse(e.data);
    /* Ensure we have the right json format */
    if (
      msg !== null &&
      typeof msg === 'object' &&
      msg.hasOwnProperty('cmd') &&
      msg.hasOwnProperty('data')
    ) {
      switch (msg.cmd) {
        case 'primary':
          showToast(msg.data, ToastType.Primary);
          break;
        case 'secondary':
          showToast(msg.data, ToastType.Secondary);
          break;
        case 'success':
          showToast(msg.data, ToastType.Success);
          if(msg.data == 'Scan process finished.') {
            if(totalMinutes < 10 && totalSeconds < 10) {
              document.getElementById('time').innerHTML = `Total Time: ${totalHours}:0${totalMinutes}:0${totalSeconds}`;
              document.getElementById('time3d').innerHTML = `Total Time: ${totalHours}:0${totalMinutes}:0${totalSeconds}`;
            } else if (totalMinutes < 10) {
              document.getElementById('time').innerHTML = `Total Time: ${totalHours}:0${totalMinutes}:${totalSeconds}`;
              document.getElementById('time3d').innerHTML = `Total Time: ${totalHours}:0${totalMinutes}:${totalSeconds}`;
            } else if(totalSeconds < 10) {
              document.getElementById('time').innerHTML = `Total Time: ${totalHours}:${totalMinutes}:0${totalSeconds}`;
              document.getElementById('time3d').innerHTML = `Total Time: ${totalHours}:${totalMinutes}:0${totalSeconds}`;
            } else {
              document.getElementById('time').innerHTML = `Total Time: ${totalHours}:${totalMinutes}:${totalSeconds}`;
              document.getElementById('time3d').innerHTML = `Total Time: ${totalHours}:${totalMinutes}:${totalSeconds}`;
            }
            document.getElementById('time').classList.replace('btn-outline-danger', 'btn-outline-success');
            document.getElementById('time3d').classList.replace('btn-outline-danger', 'btn-outline-success');
          } else if(msg.data == 'Home Position') {
            document.getElementById('button_home').classList.remove('disabled');
          } else if(msg.data == 'Self-Check finished') {
            document.getElementById('button_check').classList.remove('disabled');
          }
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
        case 'point':
          console.info(msg.data);
          append(msg.data[0], msg.data[1], msg.data[2], msg.data[3]);
          document.getElementById('currentAzimut').innerText = `Current Azimut: ${msg.data[0]}`;
          document.getElementById('currentElevation').innerText = `Current Elevation: ${msg.data[1]}`;
          document.getElementById('currentAzimut3d').innerText = `Current Azimut: ${msg.data[0]}`;
          document.getElementById('currentElevation3d').innerText = `Current Elevation: ${msg.data[1]}`;
          break;
        case 'progress':
          document.getElementById('progress').setAttribute("style", `width: ${msg.data[0]}%`);
          document.getElementById('progress').innerHTML = `${msg.data[0]}%`;
          document.getElementById('progress').setAttribute("aria-valuenow", `${msg.data[0]}`);
          document.getElementById('progress3d').setAttribute("style", `width: ${msg.data[0]}%`);
          document.getElementById('progress3d').innerHTML = `${msg.data[0]}%`;
          document.getElementById('progress3d').setAttribute("aria-valuenow", `${msg.data[0]}`);
          totalTime = msg.data[1];
          totalSeconds = totalTime % 60;
          totalTime -= totalSeconds;
          totalMinutes = totalTime % 3600;
          totalTime -= totalMinutes;
          totalMinutes /= 60;
          totalHours = totalTime / 3600;
          timeLeft = msg.data[2];
          secondsLeft = timeLeft % 60;
          timeLeft -= secondsLeft;
          minutesLeft = timeLeft % 3600;
          timeLeft -= minutesLeft;
          minutesLeft /= 60;
          hoursLeft = timeLeft / 3600;
          if(minutesLeft < 10 && secondsLeft < 10) {
            document.getElementById('time').innerHTML = `Time Left: ${hoursLeft}:0${minutesLeft}:0${secondsLeft}`;
            document.getElementById('time3d').innerHTML = `Time Left: ${hoursLeft}:0${minutesLeft}:0${secondsLeft}`;
          } else if(minutesLeft < 10) {
            document.getElementById('time').innerHTML = `Time Left: ${hoursLeft}:0${minutesLeft}:${secondsLeft}`;
            document.getElementById('time3d').innerHTML = `Time Left: ${hoursLeft}:0${minutesLeft}:${secondsLeft}`;
          } else if(secondsLeft < 10) {
            document.getElementById('time').innerHTML = `Time Left: ${hoursLeft}:${minutesLeft}:0${secondsLeft}`;
            document.getElementById('time3d').innerHTML = `Time Left: ${hoursLeft}:${minutesLeft}:0${secondsLeft}`;
          } else {
            document.getElementById('time').innerHTML = `Time Left: ${hoursLeft}:${minutesLeft}:${secondsLeft}`;
            document.getElementById('time3d').innerHTML = `Time Left: ${hoursLeft}:${minutesLeft}:${secondsLeft}`;
          }
          break;
        default:
          break;
      }
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
  toastBody.innerHTML = message;

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
      document.getElementById('toastContainer').removeChild(e.currentTarget);
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
  document.getElementById('toastContainer').appendChild(toast);
  const t = new bootstrap.Toast(toast, {
    autohide: true,
    animation: false,
    delay: 3000
  });
  t.show();
}

/**
 * Send server command.
 */
function sendServerCommand(data) {
  if (wsocket !== null) {
    /* Create json string and send via websocket */
    wsocket.send(JSON.stringify(data));
  }
}

function createDataFrame() {
  let filename = document.getElementById('inputFilename').value;
  document.getElementById('inputFilename').classList.remove('is-valid', 'is-invalid');
  if(filename.length > 25) {
    document.getElementById('inputFilename').classList.add('is-invalid');
    return false;
  }
  if(filename.length === 0) {
    filename = "000000000000000000000000";
  }
  document.getElementById('inputFilename').classList.add('is-valid');

  let azAngle = document.getElementById('inputAzimutAngle').valueAsNumber;
  document.getElementById('inputAzimutAngle').classList.remove('is-valid', 'is-invalid');
  if(azAngle < 0 || azAngle > 360 || Number.isNaN(azAngle)) {
    document.getElementById('inputAzimutAngle').classList.add('is-invalid');
    return false;
  }
  document.getElementById('inputAzimutAngle').classList.add('is-valid');

  let azResolution = document.getElementById('inputAzimutResolution').valueAsNumber;
  document.getElementById('inputAzimutResolution').classList.remove('is-valid', 'is-invalid');
  if(azResolution < 2 || azResolution > 360 || Number.isNaN(azResolution)) {
    document.getElementById('inputAzimutResolution').classList.add('is-invalid');
    return false;
  }
  document.getElementById('inputAzimutResolution').classList.add('is-valid');
  if(azAngle % azResolution != 0) {
    document.getElementById('inputAzimutAngle').classList.remove('is-valid', 'is-invalid');
    document.getElementById('inputAzimutResolution').classList.remove('is-valid', 'is-invalid');
    document.getElementById('inputAzimutAngle').classList.add('is-invalid');
    document.getElementById('inputAzimutResolution').classList.add('is-invalid');
    return false;
  }

  let elStartAngle = document.getElementById('inputElevStartAngle').valueAsNumber;
  document.getElementById('inputElevStartAngle').classList.remove('is-valid', 'is-invalid');
  if(elStartAngle < -90 || elStartAngle > 90 || Number.isNaN(elStartAngle)) {
    document.getElementById('inputElevStartAngle').classList.add('is-invalid');
    return false;
  }

  let elStopAngle = document.getElementById('inputElevStopAngle').valueAsNumber;
  document.getElementById('inputElevStopAngle').classList.remove('is-valid', 'is-invalid');
  if(elStopAngle < -90 || elStopAngle > 90 || Number.isNaN(elStopAngle)) {
    document.getElementById('inputElevStopAngle').classList.add('is-invalid');
    return false;
  }

  if(elStartAngle > elStopAngle) {
    document.getElementById('inputElevStartAngle').classList.add('is-invalid');
    document.getElementById('inputElevStopAngle').classList.add('is-invalid');
    return false;
  }
  document.getElementById('inputElevStartAngle').classList.add('is-valid');
  document.getElementById('inputElevStopAngle').classList.add('is-valid');

  let elResolution = document.getElementById('inputElevResolution').valueAsNumber;
  document.getElementById('inputElevResolution').classList.remove('is-valid', 'is-invalid');
  if(elResolution < 2 || elResolution > 90 || Number.isNaN(elResolution)) {
    document.getElementById('inputElevResolution').classList.add('is-invalid');
    return false;
  }
  document.getElementById('inputElevResolution').classList.add('is-valid');
  if((Math.abs(elStartAngle - elStopAngle)) % elResolution != 0) {
    document.getElementById('inputElevStartAngle').classList.remove('is-valid', 'is-invalid');
    document.getElementById('inputElevStopAngle').classList.remove('is-valid', 'is-invalid');
    document.getElementById('inputElevResolution').classList.remove('is-valid', 'is-invalid');
    document.getElementById('inputElevStartAngle').classList.add('is-invalid');
    document.getElementById('inputElevStopAngle').classList.add('is-invalid');
    document.getElementById('inputElevResolution').classList.add('is-invalid');
    return false;
  }

  let startFrequency = document.getElementById('inputStartFrequency').valueAsNumber;
  document.getElementById('inputStartFrequency').classList.remove('is-valid', 'is-invalid');
  if(startFrequency < 1 || startFrequency > 7000 || Number.isNaN(startFrequency)) {
    document.getElementById('inputStartFrequency').classList.add('is-invalid');
    return false;
  }

  let stopFrequency = document.getElementById('inputStopFrequency').valueAsNumber;
  document.getElementById('inputStopFrequency').classList.remove('is-valid', 'is-invalid');
  if(stopFrequency < 1 || stopFrequency > 7000 || Number.isNaN(stopFrequency)) {
    document.getElementById('inputStopFrequency').classList.add('is-invalid');
    return false;
  }

  if(startFrequency >= stopFrequency) {
    document.getElementById('inputStartFrequency').classList.add('is-invalid');
    document.getElementById('inputStopFrequency').classList.add('is-invalid');
    return false;
  }
  document.getElementById('inputStartFrequency').classList.add('is-valid');
  document.getElementById('inputStopFrequency').classList.add('is-valid');

  let testFrequency = document.getElementById('inputMeasureFrequency').value.split(',');
  document.getElementById('inputMeasureFrequency').classList.remove('is-valid', 'is-invalid');
  if(testFrequency.length === 0) {
    document.getElementById('inputMeasureFrequency').classList.add('is-invalid');
    return false;
  }
  for(let i = 0; i < testFrequency.length; i++) {
    let f_val = Number.parseInt(testFrequency[i], 10);
    if(f_val < 1 || f_val > 7000 || Number.isNaN(f_val) || f_val < startFrequency || f_val > stopFrequency) {
      document.getElementById('inputMeasureFrequency').classList.add('is-invalid');
      return false;
    }
    testFrequency[i] = f_val;
  }
  document.getElementById('inputMeasureFrequency').classList.add('is-valid');

  let refGain = document.getElementById('inputGainRefAntenna').value.split(',');
  document.getElementById('inputGainRefAntenna').classList.remove('is-valid', 'is-invalid');
  if(refGain.length === 0) {
    document.getElementById('inputGainRefAntenna').classList.add('is-invalid');
    return false;
  }
  for(let i = 0; i < refGain.length; i++) {
    let g_val = Number.parseFloat(refGain[i], 10);
    if(g_val < -100 || g_val > 100 || Number.isNaN(g_val)) {
      document.getElementById('inputGainRefAntenna').classList.add('is-invalid');
      return false;
    }
    refGain[i] = g_val;
  }
  document.getElementById('inputGainRefAntenna').classList.add('is-valid');
  /* One reference gain for every measurement frequency */
  if(testFrequency.length != refGain.length) {
    document.getElementById('inputMeasureFrequency').classList.remove('is-valid', 'is-invalid');
    document.getElementById('inputGainRefAntenna').classList.remove('is-valid', 'is-invalid');
    document.getElementById('inputMeasureFrequency').classList.add('is-invalid');
    document.getElementById('inputGainRefAntenna').classList.add('is-invalid');
    return false;
  }

  init_plot(testFrequency, testFrequency.length);
  
  init_dropdown(testFrequency, testFrequency.length, elStartAngle, elStopAngle, elResolution, azAngle, azResolution);

  return [ServerCommand.CmdCalib, filename, azAngle, azResolution, elStartAngle, elStopAngle, elResolution, startFrequency, stopFrequency, testFrequency, refGain, testFrequency.length];

}

function createPanData() {
  document.getElementById('inputPanPosition').classList.remove('is-valid', 'is-invalid');
  const pan = document.getElementById("inputPanPosition").valueAsNumber;
  if(pan < 0 || pan > 360 || Number.isNaN(pan)) {
    document.getElementById('inputPanPosition').classList.add('is-invalid');
    return false;
  }
  document.getElementById('inputPanPosition').classList.add('is-valid');

  return pan;
}

function createTiltData() {
  document.getElementById('inputTiltPosition').classList.remove('is-valid', 'is-invalid');
  const tilt = document.getElementById('inputTiltPosition').valueAsNumber;
  if(tilt < -90 || tilt > 90 || Number.isNaN(tilt)) {
    document.getElementById('inputTiltPosition').classList.add('is-invalid');
    return false;
  }
  document.getElementById('inputTiltPosition').classList.add('is-valid');

  return tilt;
}

function app_status_handler(status) {
  switch (status) {
    case AppStatus.Stopped:
      document.getElementById('button_start').classList.add('disabled');
      document.getElementById('button_pause').classList.add('disabled');
      document.getElementById('button_stop').classList.add('disabled');
      document.getElementById('button_calib').classList.remove('disabled');
      document.getElementById('button_headcmd').classList.remove('disabled');
      document.getElementById('button_data').classList.remove('disabled');
      if(document.getElementById('elevationDropdown').childElementCount > 1) {
        document.getElementById('button_data3d').classList.remove('disabled');
      }
      document.getElementById('button_pause').classList.replace('btn-warning', 'btn-light');
      break;
    case AppStatus.Calibrated:
      document.getElementById('button_start').classList.remove('disabled');
      break;
    case AppStatus.Running:
      document.getElementById('button_data3d').classList.add('disabled');
      document.getElementById('button_calib').classList.add('disabled');
      document.getElementById('button_start').classList.add('disabled');
      document.getElementById('button_headcmd').classList.add('disabled');
      document.getElementById('button_stop').classList.remove('disabled');
      document.getElementById('button_data').classList.remove('disabled');
      if(document.getElementById('elevationDropdown').childElementCount > 1) {
        document.getElementById('button_data3d').classList.remove('disabled');
      }
      document.getElementById('button_pause').classList.remove('disabled');
      document.getElementById('button_pause').classList.replace('btn-warning', 'btn-light');
      break;
    case AppStatus.Paused:
      document.getElementById('button_pause').classList.replace('btn-light', 'btn-warning');
      break;
    default:
      break;
  }
}

function init_dropdown(frequency, len, elStart, elStop, elRes, azSector, azRes) {
  var completeList = document.getElementById('frequencyDropdown');
  while(completeList.childElementCount != 0) {
    completeList.firstChild.remove();
  }
  for(let i = 0; i < len; i++) {
    var li = document.createElement("li");
    var button = document.createElement("button");
    button.classList.add('dropdown-item');
    button.setAttribute("type", "button");
    button.setAttribute("value", `${frequency[i]}`);
    button.setAttribute("id", `${frequency[i]}`);
    button.innerText = `${frequency[i]}MHz`;
    li.appendChild(button);
    completeList.appendChild(li);
    button.addEventListener('click', () => {
      liveDataStatus = i;
      draw_plot();
    });
  }

  completeList = document.getElementById('frequencyDropdown3d');
  while(completeList.childElementCount != 0) {
    completeList.firstChild.remove();
  }
  for(let i = 0; i < len; i++) {
    var li = document.createElement("li");
    var button = document.createElement("button");
    button.classList.add('dropdown-item');
    button.setAttribute("type", "button");
    button.setAttribute("value", `${frequency[i]}`);
    button.setAttribute("id", `${frequency[i]}-3d`);
    button.innerText = `${frequency[i]}MHz`;
    li.appendChild(button);
    completeList.appendChild(li);
    button.addEventListener('click', () => {
      liveDataStatus = i;
      draw_plot();
    });
  }

  completeList = document.getElementById('elevationDropdown');
  while(completeList.childElementCount != 0) {
    completeList.firstChild.remove();
  }
  for(let i = elStart; i <= elStop; i+=elRes) {
    var li = document.createElement("li");
    var button = document.createElement("button");
    button.classList.add('dropdown-item');
    button.setAttribute("type", "button");
    button.setAttribute("value", `${i}`);
    button.setAttribute("id", `${i}el`);
    button.innerText = `${i}°`;
    li.appendChild(button);
    completeList.appendChild(li);
    button.addEventListener('click', () => {
      liveDataStatusEl = i;
      draw_plot();
    })
  }
  liveDataStatusEl = elStart;

  completeList = document.getElementById('azimutDropdown');
  while(completeList.childElementCount != 0) {
    completeList.firstChild.remove();
  }
  var azStart = ((360 - (azSector / 2.0)));
  var azStop = azSector / 2.0;
  if(azStop < azStart || azSector == 360) {
    azStop += 360;
  }
  for(let i = azStart; i <= azStop; i+=azRes) {
    var li = document.createElement("li");
    var button = document.createElement("button");
    button.classList.add('dropdown-item');
    button.setAttribute("type", "button");
    button.setAttribute("value", `${(i%360)}`);
    button.setAttribute("id", `${(i%360)}az`);
    button.innerText = `${(i%360)}°`;
    li.appendChild(button);
    completeList.appendChild(li);
    button.addEventListener('click', () => {
      liveDataStatusAz = i % 360;
      draw_plot();
    })
  }
  liveDataStatusAz = azStart;
}

/**
 * Start application as soon as DOM is fully loaded
 */
document.addEventListener('DOMContentLoaded', () => {
  /* Add click event listeners to each button */
  document.getElementById('button_calib').addEventListener('click', () => {
    const data = createDataFrame();
    document.getElementById('progress').setAttribute("style", "width: 0%");
    document.getElementById('progress').innerHTML = "0%";
    document.getElementById('progress').setAttribute("aria-valuenow", "0");
    if(data !== false) {
      sendServerCommand(data); //calib receiver
    }
  });
  document.getElementById('button_start').addEventListener('click', () => {
    sendServerCommand([ServerCommand.CmdStart, 1]); //start measurement loop
    document.getElementById('time').classList.replace('btn-outline-success', 'btn-outline-danger');
    document.getElementById('time3d').classList.replace('btn-outline-success', 'btn-outline-danger');
  });
  document.getElementById('button_pause').addEventListener('click', () => {
    sendServerCommand([ServerCommand.CmdPause, 2]); //pause measurement loop
  });
  document.getElementById('button_stop').addEventListener('click', () => {
    sendServerCommand([ServerCommand.CmdStop, 3]); //stop measurement loop
    document.getElementById('time').innerHTML = `Time Left: 0:00:00`;
    document.getElementById('time3d').innerHTML = `Time Left: 0:00:00`;
  });
  document.getElementById('button_pan').addEventListener('click', () => {
    const data = createPanData();
    if(data !== false) {
      sendServerCommand([ServerCommand.CmdPan, data]); //set pan position
    }
  });
  document.getElementById('button_tilt').addEventListener('click', () => {
    const data = createTiltData();
    if(data !== false) {
      sendServerCommand([ServerCommand.CmdTilt, data]); //set tilt position
    }
  });
  document.getElementById('button_check').addEventListener('click', () => {
    sendServerCommand([ServerCommand.CmdCheck, 6]); //self check
    document.getElementById('button_check').classList.add('disabled');
  });
  document.getElementById('button_home').addEventListener('click', () => {
    sendServerCommand([ServerCommand.CmdHome, 7]); //home
    document.getElementById('button_home').classList.add('disabled');
  });
  init_plot();
  /* Finally, connect to weather station */
  wsConnect();
});
