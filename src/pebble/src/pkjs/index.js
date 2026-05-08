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

function loginAndFetch(viewType) {
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
        fetchTasks(viewType);
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

function fetchTasks(viewType) {
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
          var state = t.state !== undefined ? parseInt(t.state, 10) : 0;
          var seqt = t.seqt !== undefined ? parseInt(t.seqt, 10) : 0;
          var deleted = t.deleted !== undefined ? parseInt(t.deleted, 10) : 0;
          var vt = parseInt(viewType, 10);

          if (deleted !== 0 && state === 9) continue;

          var include = false;
          if (vt === 0) {
            include = (state === 0);
          } else if (vt === 1) {
            include = (seqt !== 0 && state !== 7 && state !== 8 && state !== 9);
          } else if (vt === 2) {
            include = (state === 1);
          } else if (vt === 3) {
            include = (state === 2);
          }

          if (include) {
            // Keep the original state just in case, but string/number doesn't matter for dict
            tasks.push(t);
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

function createTask(name) {
  if (!authToken) {
    console.log("createTask: No auth token available");
    Pebble.sendAppMessage({ 'AppKeyTaskName': 'Cannot create: no auth', 'AppKeyTaskCount': 1, 'AppKeyTaskIndex': 0 });
    return;
  }

  console.log("createTask: Creating task -> " + name);

  var ts = new Date().getTime();
  var ts_sec = Math.floor(ts / 1000);
  var request_id = uuidv4();
  var task_id = uuidv4();

  var payload = [{
    "method": "task.save",
    "id": task_id,
    "type": 0, "_type": ts_sec, "_type_ms": ts,
    "parentid": "", "_parentid": ts_sec, "_parentid_ms": ts,
    "waitingfor": "", "_waitingfor": ts_sec, "_waitingfor_ms": ts,
    "state": 0, "_state": ts_sec, "_state_ms": ts,
    "completed": 0, "_completed": ts_sec, "_completed_ms": ts,
    "cancelled": 0, "_cancelled": ts_sec, "_cancelled_ms": ts,
    "seq": ts_sec, "_seq": ts_sec, "_seq_ms": ts,
    "seqt": 0, "_seqt": ts_sec, "_seqt_ms": ts,
    "seqp": 0, "_seqp": ts_sec, "_seqp_ms": ts,
    "name": name, "_name": ts_sec, "_name_ms": ts,
    "tags": "", "_tags": ts_sec, "_tags_ms": ts,
    "note": "", "_note": ts_sec, "_note_ms": ts,
    "ps": 0, "_ps": ts_sec, "_ps_ms": ts,
    "etime": 0, "_etime": ts_sec, "_etime_ms": ts,
    "energy": 0, "_energy": ts_sec, "_energy_ms": ts,
    "startdate": "", "_startdate": ts_sec, "_startdate_ms": ts,
    "duedate": "", "_duedate": ts_sec, "_duedate_ms": ts,
    "recurring": "", "_recurring": ts_sec, "_recurring_ms": ts,
    "deleted": 0, "_deleted": ts_sec, "_deleted_ms": ts
  }];

  var url = API_BASE + "/everything?&appid=" + APP_ID + "&appversion=" + APP_VERSION +
    "&return=everything&since_ms=0" +
    "&authtoken=" + authToken +
    "&clienttime_ms=" + ts +
    "&requestid=" + request_id;

  var req = new XMLHttpRequest();
  req.open('POST', url, true);
  req.setRequestHeader("Content-Type", "application/json");
  req.onload = function () {
    if (req.status === 200) {
      console.log("createTask: Task created successfully!");
      fetchTasks();
      Pebble.sendAppMessage({ 'AppKeyTaskCreatedSuccess': 1 });
    } else {
      console.log("createTask HTTP Error: " + req.status + " - " + req.responseText);
      Pebble.sendAppMessage({ 'AppKeyTaskName': 'Create error', 'AppKeyTaskCount': 1, 'AppKeyTaskIndex': 0 });
    }
  };
  req.send(JSON.stringify(payload));
}

Pebble.addEventListener('ready', function (e) {
  console.log('PebbleKit JS ready!');
  Pebble.sendAppMessage({ 'AppKeyReady': 1 });
});

Pebble.addEventListener('appmessage', function (e) {
  console.log('AppMessage received!');
  if (e.payload.AppKeyRequestTasks !== undefined) {
    loginAndFetch(e.payload.AppKeyRequestTasks);
  }
  if (e.payload.AppKeyCreateTask !== undefined) {
    createTask(e.payload.AppKeyCreateTask);
  }
  if (e.payload.AppKeyInitiateTaskCreation !== undefined) {
    var watchInfo = Pebble.getActiveWatchInfo && Pebble.getActiveWatchInfo();
    if (watchInfo && watchInfo.model && watchInfo.model.indexOf('qemu') > -1) {
      try {
        var devConfig = require('./dev_config.json');
        if (devConfig && devConfig.mock_dictation) {
          console.log("Using mock dictation from dev_config.json");
          createTask(devConfig.mock_dictation);
          return;
        }
      } catch (err) { }
    }
    Pebble.sendAppMessage({ 'AppKeyStartDictation': 1 });
  }
});
