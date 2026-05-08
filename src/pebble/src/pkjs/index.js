var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: true });
var md5 = require('./md5');

var APP_ID = "com.nirvanahq.focus";
var APP_VERSION = "3.16.6";
var API_BASE = "https://api.nirvanahq.com/api";
var authToken = null;

function uuidv4() {
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function (c) {
    var r = Math.random() * 16 | 0, v = c == 'x' ? r : (r & 0x3 | 0x8);
    return v.toString(16);
  });
}

function loginAndFetch() {
  var dict = JSON.parse(localStorage.getItem('clay-settings')) || {};
  var username = dict.username;
  var password = dict.password;

  var watchInfo = Pebble.getActiveWatchInfo && Pebble.getActiveWatchInfo();
  if (watchInfo && watchInfo.model && watchInfo.model.indexOf('qemu') > -1) {
    try {
      var devConfig = require('./dev_config.json');
      if (devConfig && devConfig.username && devConfig.password) {
        username = devConfig.username;
        password = devConfig.password;
        console.log("Using credentials from dev_config.json (Emulator mode)");
      }
    } catch (e) {
      console.log("Could not load dev_config.json");
    }
  }

  if (!username || !password) {
    console.log("No credentials provided");
    Pebble.sendAppMessage({ 'AppKeyTaskName': 'Please login in settings', 'AppKeyTaskCount': 1, 'AppKeyTaskIndex': 0 });
    return;
  }

  var url = API_BASE + "/auth/new?&appid=" + APP_ID + "&appversion=" + APP_VERSION;
  var payload = {
    gmtoffset: 0,
    u: username,
    p: md5(password)
  };

  var req = new XMLHttpRequest();
  req.open('POST', url, true);
  req.setRequestHeader("Content-Type", "application/json");
  req.onload = function () {
    if (req.status === 200) {
      var res = JSON.parse(req.responseText);
      var result = res.results && res.results[0];
      if (result && result.auth && result.auth.token) {
        authToken = result.auth.token;
        fetchTasks();
      } else {
        console.log("Login failed: " + req.responseText);
        Pebble.sendAppMessage({ 'AppKeyTaskName': 'Login failed', 'AppKeyTaskCount': 1, 'AppKeyTaskIndex': 0 });
      }
    } else {
      console.log("Login HTTP Error: " + req.status);
      Pebble.sendAppMessage({ 'AppKeyTaskName': 'Login error', 'AppKeyTaskCount': 1, 'AppKeyTaskIndex': 0 });
    }
  };
  req.send(JSON.stringify(payload));
}

function fetchTasks() {
  if (!authToken) {
    console.log("fetchTasks: No auth token available");
    return;
  }

  console.log("fetchTasks: Starting fetch request...");

  var ts = new Date().getTime();
  var request_id = uuidv4();
  var url = API_BASE + "/everything?&appid=" + APP_ID + "&appversion=" + APP_VERSION +
    "&return=everything&since_ms=0" +
    "&authtoken=" + authToken +
    "&clienttime_ms=" + ts +
    "&requestid=" + request_id;

  console.log("fetchTasks: URL generated -> " + url);

  var req = new XMLHttpRequest();
  req.open('POST', url, true);
  req.setRequestHeader("Content-Type", "application/json");
  req.onload = function () {
    console.log("fetchTasks: Request returned with status " + req.status);
    if (req.status === 200) {
      var res = JSON.parse(req.responseText);
      var results = res.results || [];
      console.log("fetchTasks: Received " + results.length + " total items from API.");
      var tasks = [];

      for (var i = 0; i < results.length; i++) {
        if (results[i].task) {
          var t = results[i].task;
          if (t.deleted !== 0 && t.state === 9) continue;

          if (t.state === '1') { // NEXT only
            tasks.push(t);
          } else {
            console.log(t.state)
          }
        }
      }

      console.log("fetchTasks: Filtered down to " + tasks.length + " actionable tasks.");
      sendTasksToWatch(tasks);
    } else {
      console.log("Fetch HTTP Error: " + req.status + " - " + req.responseText);
      Pebble.sendAppMessage({ 'AppKeyTaskName': 'Fetch error', 'AppKeyTaskCount': 1, 'AppKeyTaskIndex': 0 });
    }
  };
  req.send("[]");
}

function sendTasksToWatch(tasks) {
  if (tasks.length === 0) {
    Pebble.sendAppMessage({ 'AppKeyTaskCount': 1, 'AppKeyTaskIndex': 0, 'AppKeyTaskName': 'No tasks' });
    return;
  }

  var index = 0;
  var maxTasks = Math.min(tasks.length, 20);

  function sendNext() {
    if (index >= maxTasks) {
      return;
    }

    var dict = {
      'AppKeyTaskIndex': index,
      'AppKeyTaskCount': maxTasks,
      'AppKeyTaskName': tasks[index].name,
      'AppKeyTaskState': tasks[index].state
    };

    Pebble.sendAppMessage(dict, function () {
      index++;
      sendNext();
    }, function (e) {
      console.log('Error sending task info for index ' + index + ' Error: ' + JSON.stringify(e));
      index++;
      sendNext();
    });
  }

  sendNext();
}

Pebble.addEventListener('ready', function (e) {
  console.log('PebbleKit JS ready!');
  Pebble.sendAppMessage({ 'AppKeyReady': 1 });
});

Pebble.addEventListener('appmessage', function (e) {
  console.log('AppMessage received!');
  if (e.payload.AppKeyRequestTasks !== undefined) {
    loginAndFetch();
  }
});
