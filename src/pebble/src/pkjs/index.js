var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: true });
var md5 = require('./md5');

var APP_ID = "com.nirvanahq.focus";
var APP_VERSION = "3.16.6";
var API_BASE = "https://api.nirvanahq.com/api";
var authToken = null;

// Cache of full task objects from last fetch, keyed by task id
var taskCache = {};
// Cache of project name lookup by task id
var projectCache = {};
// Cache of tags
var tagCache = {};

function uuidv4() {
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function (c) {
    var r = Math.random() * 16 | 0, v = c == 'x' ? r : (r & 0x3 | 0x8);
    return v.toString(16);
  });
}

// Format YYYYMMDD -> MM/DD/YYYY, return "" if blank
function formatDate(d) {
  if (!d || d.length !== 8) return "";
  return d.substring(4, 6) + "/" + d.substring(6, 8) + "/" + d.substring(0, 4);
}

// Strip leading/trailing commas from tags string
function formatTags(tags) {
  if (!tags) return "";
  return tags.replace(/^,+|,+$/g, '').replace(/,/g, ', ');
}

// Truncate string to max length for AppMessage
function trunc(str, max) {
  if (!str) return "";
  return str.length > max ? str.substring(0, max - 1) : str;
}

function getCredentials() {
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
      }
    } catch (e) { }
  }
  return { username: username, password: password };
}

function loginAndFetch(viewType) {
  var creds = getCredentials();
  var username = creds.username;
  var password = creds.password;

  if (!username || !password) {
    console.log("No credentials provided");
    Pebble.sendAppMessage({ 'AppKeyTaskName': 'Please login in settings', 'AppKeyTaskCount': 1, 'AppKeyTaskIndex': 0 });
    return;
  }

  var url = API_BASE + "/auth/new?&appid=" + APP_ID + "&appversion=" + APP_VERSION;
  var payload = { gmtoffset: 0, u: username, p: md5(password) };

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
        Pebble.sendAppMessage({ 'AppKeyTaskName': 'Login failed', 'AppKeyTaskCount': 1, 'AppKeyTaskIndex': 0 });
      }
    } else {
      Pebble.sendAppMessage({ 'AppKeyTaskName': 'Login error', 'AppKeyTaskCount': 1, 'AppKeyTaskIndex': 0 });
    }
  };
  req.send(JSON.stringify(payload));
}

function loginIfNeeded(callback) {
  if (authToken) { callback(); return; }
  var creds = getCredentials();
  if (!creds.username || !creds.password) {
    console.log("No credentials for loginIfNeeded");
    return;
  }
  var url = API_BASE + "/auth/new?&appid=" + APP_ID + "&appversion=" + APP_VERSION;
  var payload = { gmtoffset: 0, u: creds.username, p: md5(creds.password) };
  var req = new XMLHttpRequest();
  req.open('POST', url, true);
  req.setRequestHeader("Content-Type", "application/json");
  req.onload = function () {
    if (req.status === 200) {
      var res = JSON.parse(req.responseText);
      var result = res.results && res.results[0];
      if (result && result.auth && result.auth.token) {
        authToken = result.auth.token;
        callback();
      }
    }
  };
  req.send(JSON.stringify(payload));
}

function fetchTasks(viewType) {
  if (!authToken) return;

  var ts = new Date().getTime();
  var request_id = uuidv4();
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
      var res = JSON.parse(req.responseText);
      var results = res.results || [];
      var tasks = [];

      // Reset caches
      taskCache = {};
      projectCache = {};
      tagCache = {};

      // First pass: build lookups
      for (var i = 0; i < results.length; i++) {
        if (results[i].task) {
          var t = results[i].task;
          if (parseInt(t.type, 10) === 1) {
            projectCache[t.id] = t.name;
            console.log("Cached project: " + t.name + " (" + t.id + ")");
          }
          taskCache[t.id] = t;
        } else if (results[i].tag) {
          tagCache[results[i].tag.key] = results[i].tag;
        }
      }

      // Second pass: filter tasks for view
      for (var j = 0; j < results.length; j++) {
        if (results[j].task) {
          var t = results[j].task;
          var state = t.state !== undefined ? parseInt(t.state, 10) : 0;
          var seqt = t.seqt !== undefined ? parseInt(t.seqt, 10) : 0;
          var deleted = t.deleted !== undefined ? parseInt(t.deleted, 10) : 0;
          var vt = parseInt(viewType, 10);

          if (deleted !== 0 && state === 9) continue;

          var include = false;
          if (vt === 0) include = (state === 0);
          else if (vt === 1) include = (seqt !== 0 && state !== 7 && state !== 8 && state !== 9);
          else if (vt === 2) include = (state === 1);
          else if (vt === 3) include = (state === 2);

          if (include) tasks.push(t);
        }
      }

      sendTasksToWatch(tasks);
    } else {
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
  var maxTasks = Math.min(tasks.length, 50);

  function sendNext() {
    if (index >= maxTasks) return;

    var dict = {
      'AppKeyTaskIndex': index,
      'AppKeyTaskCount': maxTasks,
      'AppKeyTaskName': trunc(tasks[index].name, 60),
      'AppKeyTaskState': tasks[index].state,
      'AppKeyTaskId': trunc(tasks[index].id, 60)
    };

    Pebble.sendAppMessage(dict, function () {
      index++;
      sendNext();
    }, function (e) {
      console.log('Error sending task index ' + index + ': ' + JSON.stringify(e));
      index++;
      sendNext();
    });
  }

  sendNext();
}

function splitTags(tagsStr) {
  if (!tagsStr) return { areas: "", contexts: "" };
  var parts = tagsStr.split(",");
  var areas = [];
  var contexts = [];
  for (var i = 0; i < parts.length; i++) {
    var key = parts[i].trim();
    if (!key) continue;
    var tag = tagCache[key];
    if (tag) {
      if (tag.type == 1) {
        // type 1 = AREA (e.g. @work, @home)
        areas.push(key);
      } else if (tag.type == 2) {
        // type 2 = CONTACT (e.g. Marilyn, Jason Miller) — skip, don't display on watch
      } else if (tag.type == 0) {
        // type 0 = CONTEXT (e.g. #tara, #keystone, calls)
        contexts.push(key);
      }
      // unknown types: silently ignored
    } else {
      if (key.charAt(0) === '@') {
        areas.push(key);
      } else {
        contexts.push(key);
      }
    }
  }
  return {
    areas: areas.join(", "),
    contexts: contexts.join(", ")
  };
}

function sendDetailToWatch(task) {
  console.log("sendDetailToWatch task object: " + JSON.stringify(task));
  var projectName = "";
  if (task.parentid && taskCache[task.parentid]) {
    projectName = taskCache[task.parentid].name;
  } else if (task.parentid && projectCache[task.parentid]) {
    projectName = projectCache[task.parentid];
  }
  console.log("sendDetailToWatch resolved projectName: '" + projectName + "' for parentid: '" + task.parentid + "'");

  var tagSplit = splitTags(task.tags);
  var due = formatDate(task.duedate);
  var note = trunc(task.note || "", 120);
  var seqt = task.seqt !== undefined ? parseInt(task.seqt, 10) : 0;
  var state = task.state !== undefined ? parseInt(task.state, 10) : 0;
  // If focused (seqt != 0) report a synthetic state indicator via the existing state field
  // We keep the real state value; focus is indicated by seqt on the server side

  Pebble.sendAppMessage({
    'AppKeyTaskDetail': 1,
    'AppKeyTaskId': trunc(task.id, 60),
    'AppKeyTaskName': trunc(task.name || "", 60),
    'AppKeyTaskState': state,
    'AppKeyTaskNote': note,
    'AppKeyTaskDueDate': due,
    'AppKeyTaskTags': trunc(tagSplit.contexts, 60),
    'AppKeyTaskAreas': trunc(tagSplit.areas, 60),
    'AppKeyTaskProject': trunc(projectName, 60),
    'AppKeyTaskFocus': seqt !== 0 ? 1 : 0
  }, function () {
    console.log("Detail sent for task: " + task.id);
  }, function (e) {
    console.log("Error sending detail: " + JSON.stringify(e));
  });
}

function apiSave(payload, callback) {
  if (!authToken) {
    loginIfNeeded(function () { apiSave(payload, callback); });
    return;
  }
  var ts = new Date().getTime();
  var url = API_BASE + "/everything?&appid=" + APP_ID + "&appversion=" + APP_VERSION +
    "&return=everything&since_ms=0" +
    "&authtoken=" + authToken +
    "&clienttime_ms=" + ts +
    "&requestid=" + uuidv4();

  var req = new XMLHttpRequest();
  req.open('POST', url, true);
  req.setRequestHeader("Content-Type", "application/json");
  req.onload = function () {
    if (req.status === 200) {
      var res = JSON.parse(req.responseText);
      // Update cache with returned tasks
      var results = res.results || [];
      for (var i = 0; i < results.length; i++) {
        if (results[i].task) {
          taskCache[results[i].task.id] = results[i].task;
        } else if (results[i].tag) {
          tagCache[results[i].tag.key] = results[i].tag;
        }
      }
      if (callback) callback(null);
    } else {
      if (callback) callback(new Error("HTTP " + req.status));
    }
  };
  req.send(JSON.stringify(payload));
}

function createTask(name) {
  if (!authToken) {
    loginIfNeeded(function () { createTask(name); });
    return;
  }

  var ts = new Date().getTime();
  var ts_sec = Math.floor(ts / 1000);
  var task_id = uuidv4();

  var newTask = {
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
  };

  // Store in cache immediately
  taskCache[task_id] = newTask;

  apiSave([newTask], function (err) {
    if (!err) {
      console.log("createTask: Task created -> " + task_id);
      // Send detail screen data for the new task
      sendDetailToWatch(taskCache[task_id]);
    } else {
      Pebble.sendAppMessage({ 'AppKeyTaskName': 'Create error', 'AppKeyTaskCount': 1, 'AppKeyTaskIndex': 0 });
    }
  });
}

function sendDetailById(taskId) {
  var task = taskCache[taskId];
  if (task) {
    sendDetailToWatch(task);
  } else {
    console.log("sendDetailById: task not in cache -> " + taskId);
    // Task not in cache; send a minimal placeholder
    Pebble.sendAppMessage({
      'AppKeyTaskDetail': 1,
      'AppKeyTaskId': taskId,
      'AppKeyTaskName': 'Loading...',
      'AppKeyTaskState': 0,
      'AppKeyTaskNote': '',
      'AppKeyTaskDueDate': '',
      'AppKeyTaskTags': '',
      'AppKeyTaskProject': ''
    });
  }
}

function completeTask(taskId) {
  var task = taskCache[taskId];
  if (!task) { console.log("completeTask: not in cache"); return; }

  var ts = new Date().getTime();
  var ts_sec = Math.floor(ts / 1000);

  var payload = [{
    "method": "task.save", "id": taskId,
    "state": 7, "_state": ts_sec, "_state_ms": ts,
    "completed": ts_sec, "_completed": ts_sec, "_completed_ms": ts
  }];

  apiSave(payload, function (err) {
    if (!err) {
      console.log("completeTask: done -> " + taskId);
    }
  });
}

function renameTask(taskId, newName) {
  var task = taskCache[taskId];
  if (!task) { console.log("renameTask: not in cache"); return; }

  var ts = new Date().getTime();
  var ts_sec = Math.floor(ts / 1000);

  var payload = [{
    "method": "task.save", "id": taskId,
    "name": newName, "_name": ts_sec, "_name_ms": ts
  }];

  apiSave(payload, function (err) {
    if (!err) {
      // Update local cache
      taskCache[taskId].name = newName;
      // Send refreshed detail back to watch
      sendDetailToWatch(taskCache[taskId]);
    } else {
      console.log("renameTask error");
    }
  });
}

function changeTaskState(taskId, newState) {
  var task = taskCache[taskId];
  if (!task) { console.log("changeTaskState: not in cache"); return; }

  var ts = new Date().getTime();
  var ts_sec = Math.floor(ts / 1000);

  var isFocus = (newState === -1);
  var actualState = isFocus ? 1 : newState; // Move to Next if focused
  var newSeqt = isFocus ? ts_sec : 0;

  var payload = [{
    "method": "task.save", "id": taskId,
    "state": actualState, "_state": ts_sec, "_state_ms": ts,
    "seqt": newSeqt, "_seqt": ts_sec, "_seqt_ms": ts
  }];

  apiSave(payload, function (err) {
    if (!err) {
      taskCache[taskId].state = actualState;
      taskCache[taskId].seqt = newSeqt;
      sendDetailToWatch(taskCache[taskId]);
    } else {
      console.log("changeTaskState error");
    }
  });
}

function editTaskField(taskId, field, value) {
  console.log("editTaskField: taskId=" + taskId + ", field=" + field + ", value=" + value);
  var task = taskCache[taskId];
  if (!task) {
    console.log("editTaskField: taskId " + taskId + " not found in taskCache!");
    return;
  }

  var ts = new Date().getTime();
  var ts_sec = Math.floor(ts / 1000);
  var payload = [{ "method": "task.save", "id": taskId }];

  if (field === 'tags') {
    var tags = taskCache[taskId].tags || "";
    if (tags.indexOf("," + value + ",") === -1) {
      tags = tags + value + ",";
      if (tags.charAt(0) !== ',') tags = "," + tags;
    }
    payload[0]["tags"] = tags;
    payload[0]["_tags"] = ts_sec;
    payload[0]["_tags_ms"] = ts;
  } else if (field === 'project') {
    payload[0]["parentid"] = value;
    payload[0]["_parentid"] = ts_sec;
    payload[0]["_parentid_ms"] = ts;
  } else if (field === 'area') {
    if (value.length > 20) {
      payload[0]["parentid"] = value;
      payload[0]["_parentid"] = ts_sec;
      payload[0]["_parentid_ms"] = ts;
    } else {
      var tags = taskCache[taskId].tags || "";
      if (tags.indexOf("," + value + ",") === -1) {
        tags = tags + value + ",";
        if (tags.charAt(0) !== ',') tags = "," + tags;
      }
      payload[0]["tags"] = tags;
      payload[0]["_tags"] = ts_sec;
      payload[0]["_tags_ms"] = ts;
    }
  } else if (field === 'note') {
    payload[0]["note"] = value;
    payload[0]["_note"] = ts_sec;
    payload[0]["_note_ms"] = ts;
  }

  apiSave(payload, function (err) {
    if (!err) {
      if (field === 'tags') {
        taskCache[taskId].tags = payload[0]["tags"];
      } else if (field === 'project') {
        taskCache[taskId].parentid = payload[0]["parentid"];
      } else if (field === 'area') {
        if (payload[0]["parentid"]) {
          taskCache[taskId].parentid = payload[0]["parentid"];
        } else if (payload[0]["tags"]) {
          taskCache[taskId].tags = payload[0]["tags"];
        }
      } else if (field === 'note') {
        taskCache[taskId].note = payload[0]["note"];
      }
      sendDetailToWatch(taskCache[taskId]);
    }
  });
}

function sendListToWatch(items, listType) {
  var index = 0;
  var maxItems = Math.min(items.length, 50);

  if (maxItems === 0) {
    Pebble.sendAppMessage({ 'AppKeyListCount': 0, 'AppKeyListType': listType, 'AppKeyListIndex': 0, 'AppKeyListItemName': 'No items' });
    return;
  }

  function sendNext() {
    if (index >= maxItems) return;
    var dict = {
      'AppKeyListType': listType,
      'AppKeyListIndex': index,
      'AppKeyListCount': maxItems,
      'AppKeyListItemName': trunc(items[index].name, 60),
      'AppKeyListItemId': trunc(items[index].id, 60)
    };
    Pebble.sendAppMessage(dict, function () {
      index++;
      sendNext();
    }, function (e) {
      console.log('Error sending list item ' + index);
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
  var p = e.payload;
  console.log('AppMessage received: ' + JSON.stringify(Object.keys(p)));

  if (p.AppKeyRequestTasks !== undefined) {
    loginAndFetch(p.AppKeyRequestTasks);
  }

  if (p.AppKeyCreateTask !== undefined) {
    createTask(p.AppKeyCreateTask);
  }

  if (p.AppKeyInitiateTaskCreation !== undefined) {
    var watchInfo = Pebble.getActiveWatchInfo && Pebble.getActiveWatchInfo();
    if (watchInfo && watchInfo.model && watchInfo.model.indexOf('qemu') > -1) {
      try {
        var devConfig = require('./dev_config.json');
        if (devConfig && devConfig.mock_dictation) {
          createTask(devConfig.mock_dictation);
          return;
        }
      } catch (err) { }
    }
    Pebble.sendAppMessage({ 'AppKeyStartDictation': 1 });
  }

  if (p.AppKeyRequestList !== undefined) {
    var listType = p.AppKeyRequestList;
    var items = [];
    if (listType === 0) { // Title
      var names = {};
      for (var id in taskCache) {
        if (taskCache[id].type == 0 && taskCache[id].name) {
          names[taskCache[id].name] = 1;
        }
      }
      for (var name in names) items.push({ name: name, id: name });
    } else if (listType === 1) { // Area
      for (var key in tagCache) {
        if (tagCache[key].type == 1) { // 1 = Area of Focus in reality
          items.push({ name: key, id: key });
        }
      }
      for (var id in taskCache) {
        if (taskCache[id].type == 4) {
          items.push({ name: taskCache[id].name, id: id });
        }
      }
    } else if (listType === 2) { // Tags
      for (var key in tagCache) {
        if (tagCache[key].type == 0 || tagCache[key].type == 2) { // 0 = Context, 2 = Contact
          items.push({ name: key, id: key });
        }
      }
    } else if (listType === 3) { // Project
      console.log("Requesting project list. Current projectCache size: " + Object.keys(projectCache).length);
      for (var id in projectCache) {
        items.push({ name: projectCache[id], id: id });
      }
      console.log("Projects found for watch list: " + JSON.stringify(items));
    }
    sendListToWatch(items, listType);
  }

  if (p.AppKeyTaskId !== undefined && p.AppKeyCompleteTask === undefined &&
    p.AppKeyRenameTask === undefined && p.AppKeyChangeTaskState === undefined) {
    // Watch requesting task detail
    sendDetailById(p.AppKeyTaskId);
  }

  if (p.AppKeyCompleteTask !== undefined && p.AppKeyTaskId !== undefined) {
    completeTask(p.AppKeyTaskId);
  }

  if (p.AppKeyRenameTask !== undefined && p.AppKeyTaskId !== undefined) {
    renameTask(p.AppKeyTaskId, p.AppKeyRenameTask);
  }

  if (p.AppKeyEditTags !== undefined && p.AppKeyTaskId !== undefined) {
    editTaskField(p.AppKeyTaskId, 'tags', p.AppKeyEditTags);
  }

  if (p.AppKeyEditNote !== undefined && p.AppKeyTaskId !== undefined) {
    editTaskField(p.AppKeyTaskId, 'note', p.AppKeyEditNote);
  }

  if (p.AppKeyEditProject !== undefined && p.AppKeyTaskId !== undefined) {
    editTaskField(p.AppKeyTaskId, 'project', p.AppKeyEditProject);
  }

  if (p.AppKeyEditArea !== undefined && p.AppKeyTaskId !== undefined) {
    editTaskField(p.AppKeyTaskId, 'area', p.AppKeyEditArea);
  }

  if (p.AppKeyChangeTaskState !== undefined && p.AppKeyTaskId !== undefined) {
    changeTaskState(p.AppKeyTaskId, p.AppKeyChangeTaskState);
  }
});
