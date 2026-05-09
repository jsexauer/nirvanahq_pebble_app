# Nirvana HQ API Documentation

Reverse-engineered from the Nirvana HQ web app (focus.nirvanahq.com, version 3.16.6). All capabilities documented here are derived from the client-side JavaScript bundle and observed HAR traffic.

---

## Overview

The Nirvana HQ API is a single-endpoint sync API. All reads and writes go through one URL:

```
https://api.nirvanahq.com/api/everything
```

Every request is a `POST` with a JSON body containing an array of operation objects. The server responds with the full current state (or delta) of the user's data.

Authentication uses a long-lived **auth token** passed as a URL query parameter on every request — there are no session cookies or bearer headers.

---

## Base URLs

| Service   | URL                                    |
|-----------|----------------------------------------|
| API       | `https://api.nirvanahq.com/api`        |
| Auth      | `https://auth.nirvanahq.com/`          |
| Account   | `https://account.nirvanahq.com/`       |
| App (web) | `https://focus.nirvanahq.com/`         |
| SSE       | `https://sse.nirvanahq.com/api`        |

---

## Authentication

### `POST /api/auth/new` — Log In

Obtains an auth token using username and **MD5-hashed** password.

**URL:** `https://api.nirvanahq.com/api/auth/new?&appid={appid}&appversion={appversion}`

**Request body:**
```json
{
  "gmtoffset": 0,
  "u": "user@example.com",
  "p": "5f4dcc3b5aa765d61d8327deb882cf99"
}
```

| Field       | Type   | Description                                        |
|-------------|--------|----------------------------------------------------|
| `gmtoffset` | number | UTC offset in hours (e.g. `-5` for EST)            |
| `u`         | string | Email address                                      |
| `p`         | string | **MD5 hash** of the plaintext password (not plaintext) |

**Success response:**
```json
{
  "results": [
    {
      "auth": {
        "token": "2da1b88164b56e849366bd641f09673..."
      }
    }
  ]
}
```

**Error response:**
```json
{
  "results": [
    {
      "error": {
        "code": 98,
        "message": "Incorrect username or password"
      }
    }
  ]
}
```

**Known error codes:**

| Code | Meaning                          |
|------|----------------------------------|
| 98   | Incorrect username or password   |
| 13   | Account-level issue              |
| 2    | Auth token invalid — forces logout |
| 6    | Invalid UUID in request          |
| 1062 | UUID collision                   |

### `POST /api/auth/destroy` — Log Out

Invalidates the auth token.

**URL:** `https://api.nirvanahq.com/api/auth/destroy?&authtoken={token}&appid={appid}&appversion={appversion}`

**Request body:** `{}` (empty JSON object)

---

## Standard Query Parameters

All API requests to `/api/everything` require these URL parameters:

| Parameter      | Description                                           |
|----------------|-------------------------------------------------------|
| `return`       | Always `everything`                                   |
| `since`        | Unix timestamp (seconds) of last sync. Use `0` for full sync |
| `since_ms`     | Unix timestamp (milliseconds) — preferred over `since` |
| `authtoken`    | Auth token from login                                 |
| `appid`        | App identifier, e.g. `com.nirvanahq.focus`            |
| `appversion`   | App version string, e.g. `3.16.6`                    |
| `clienttime`   | Current Unix time in seconds                          |
| `clienttime_ms`| Current Unix time in milliseconds                     |
| `requestid`    | UUID v7 unique request identifier                     |

**Example URL:**
```
https://api.nirvanahq.com/api/everything?&return=everything&since_ms=1777193212932&authtoken=TOKEN&appid=com.nirvanahq.focus&appversion=3.16.6&clienttime_ms=1777193213694&requestid=019dc8f8-5b19-77e8-88ea-70aaa09a0a27
```

**Request headers:**
```
Content-Type: application/json
X-Timestamp-Precision: ms
```

---

## The `/api/everything` Endpoint

### Reading Data (Full Sync)

**POST** to the URL above with body `[]` (empty array) and `since=0` to retrieve all data.

**Response structure:**
```json
{
  "request": {
    "return": "everything",
    "since_ms": 0,
    "authtoken": "...",
    "servertime_ms": 1777193212932,
    "timestamp_precision": "ms"
  },
  "results": [
    { "user": { ... } },
    { "pref": { ... } },
    { "tag": { ... } },
    { "task": { ... } }
  ]
}
```

Each element of `results` contains exactly one key, which is the entity type (`user`, `pref`, `tag`, or `task`).

### Delta Sync

Pass the `servertime_ms` from the previous response as the new `since_ms`. The server returns only entities changed since that timestamp.

### Writing Data

To write, include operation objects in the request body array:

```json
[
  { "method": "task.save", "id": "...", ... },
  { "method": "tag.save",  "key": "...", ... }
]
```

Reads and writes can be combined in a single request.

---

## Entity Types

### User Object

Returned in `results[n].user`. Read-only from the API perspective.

| Field           | Type   | Description                        |
|-----------------|--------|------------------------------------|
| `id`            | string | Numeric user ID                    |
| `servicelevel`  | string | `"basic"` or `"pro"`               |
| `maxareas`      | string | Maximum areas allowed              |
| `maxprojects`   | string | Maximum projects allowed           |
| `maxreflists`   | string | Maximum reference lists allowed    |
| `maxlogbook`    | number | Logbook retention days             |
| `emailaddress`  | string | User's email                       |
| `username`      | string | User's name                        |

---

### Task Object

Tasks are the core entity. They represent actions, projects, reference lists, reference items, and area folders depending on the `type` field.

#### Task `type` values (NIRV.CONST)

| Integer | Constant        | Description                        |
|---------|-----------------|------------------------------------|
| 0       | `TASK`          | Regular action item                |
| 1       | `PROJECT`       | Project container                  |
| 2       | `REFLIST`       | Reference list container           |
| 3       | `REFITEM`       | Item inside a reference list       |
| 4       | `AREAFOLDER`    | Area folder (organizational group) |

#### Task `state` values (NIRV.CONST)

| Integer | Constant        | Description                                 |
|---------|-----------------|---------------------------------------------|
| 0       | `INBOX`         | Inbox — uncategorized                       |
| 1       | `NEXT`          | Next actions                                |
| 2       | `WAITING`       | Waiting for someone                         |
| 3       | `SCHEDULED`     | Has a future start date                     |
| 4       | `SOMEDAY`       | Someday / maybe                             |
| 5       | `LATER`         | Later / inactive project                    |
| 6       | `ACTIVEPROJECT` | Active project (used when `type=PROJECT`)   |
| 7       | `LOGGED`        | Completed and moved to logbook              |
| 8       | `TRASHED`       | In trash                                    |
| 9       | `DELETED`       | Permanently deleted (soft-delete marker)    |
| 10      | `RECURRING`     | Recurring task template                     |

#### Task `energy` values

| Integer | Description      |
|---------|------------------|
| 0       | None             |
| 1       | Low energy (•)   |
| 2       | Medium energy (••) |
| 3       | High energy (•••)|

#### Task `etime` (estimated time) values (minutes)

Valid values: `0`, `5`, `10`, `15`, `20`, `30`, `45`, `60`, `120`, `180`, `240`, `360`, `480`, `600`

#### Full Task Field Reference

Every field has a companion timestamp field. For a field `name`, the timestamps are `_name` (Unix seconds) and `_name_ms` (Unix milliseconds) recording when that field was last changed. The server uses these to resolve conflicts during sync — whichever side has a later timestamp wins.

| Field          | Type    | Description                                                    |
|----------------|---------|----------------------------------------------------------------|
| `id`           | string  | UUID v7 — unique task identifier                               |
| `type`         | integer | Entity type (see table above)                                  |
| `state`        | integer | Task state / location (see table above)                        |
| `name`         | string  | Task title                                                     |
| `note`         | string  | Task notes / description                                       |
| `tags`         | string  | Comma-delimited tag string, e.g. `",@work,@home,"`             |
| `waitingfor`   | string  | Contact name (tag key) this task is waiting on                 |
| `parentid`     | string  | ID of the parent project or reference list (`""` if top-level) |
| `duedate`      | string  | Due date in `YYYYMMDD` format, e.g. `"20260501"` (`""` = none) |
| `startdate`    | string  | Start/scheduled date in `YYYYMMDD` format (`""` = none)        |
| `recurring`    | string  | JSON string describing the recurrence rule (see below)         |
| `completed`    | integer | Unix timestamp when completed, or `0` if not completed         |
| `cancelled`    | integer | `1` if cancelled, `0` otherwise                                |
| `deleted`      | integer | Unix timestamp when deleted, or `0`                            |
| `seq`          | integer | Sort order within a list (global)                              |
| `seqt`         | integer | Sort order in Focus view (non-zero = in Focus)                 |
| `seqp`         | integer | Sort order within a parent project                             |
| `ps`           | integer | Project status indicator                                       |
| `energy`       | integer | Energy level 0–3 (see table above)                             |
| `etime`        | integer | Estimated time in minutes (see valid values above)             |

#### Recurring Task Rule (JSON string)

When `state = 10` (RECURRING), the `recurring` field contains a JSON-encoded object:

```json
{
  "freq": "daily",
  "interval": 1,
  "on": [],
  "nextdate": "20260501",
  "hasduedate": true,
  "spawnxdaysbefore": 0,
  "count": null,
  "until": null,
  "paused": false
}
```

| Field               | Values                               | Description                                 |
|---------------------|--------------------------------------|---------------------------------------------|
| `freq`              | `"daily"`, `"weekly"`, `"monthly"`, `"yearly"` | Frequency                        |
| `interval`          | integer                              | Repeat every N units of `freq`              |
| `on`                | array                                | Days/dates within the period (see below)    |
| `nextdate`          | `"YYYYMMDD"`                         | Date the next instance spawns               |
| `hasduedate`        | boolean                              | Whether spawned tasks get a due date        |
| `spawnxdaysbefore`  | integer                              | Spawn tasks N days before the due date      |
| `count`             | integer or null                      | Max number of occurrences (`null` = infinite) |
| `until`             | `"YYYYMMDD"` or null                 | End date for recurrence                     |
| `paused`            | boolean                              | If `true`, no new instances are spawned     |

**Weekly `on` array:** day strings — `"sun"`, `"mon"`, `"tue"`, `"wed"`, `"thu"`, `"fri"`, `"sat"`

**Monthly/yearly `on` array:** objects like `{ "nth": 2, "day": "mon" }` or `{ "nth": "last", "day": "day" }` or `{ "nth": 1, "day": "day", "month": 3 }` (for yearly)

---

### Tag Object

Tags serve triple duty as contexts (e.g. `@home`), contacts (people you delegate to), and areas (organizational groupings).

#### Tag `type` values (NIRV.CONST)

| Integer | Constant   | Description                                    |
|---------|------------|------------------------------------------------|
| 0       | `CONTEXT`  | A context tag (e.g. `#tara`, `#keystone`, `calls`) |
| 1       | `AREA`     | An area of focus (e.g. `@work`, `@home`)       |
| 2       | `CONTACT`  | A person/contact (used with "waiting for") (e.g. `Marilyn`, `Jason Miller`) |

#### Tag Fields

| Field     | Type    | Description                                            |
|-----------|---------|--------------------------------------------------------|
| `key`     | string  | Tag name — the primary identifier (max 100 characters) |
| `type`    | integer | Tag type (see above)                                   |
| `color`   | string  | Display color                                          |
| `email`   | string  | Email address for contact tags                         |
| `meta`    | string  | Additional metadata                                    |
| `deleted` | integer | Unix timestamp if deleted, `0` otherwise               |

Tags use `key` as their identifier (not `id`). Tag keys are case-sensitive and must not contain `"`, `'`, `<`, `>`, or `\`.

---

### Preference Object

Preferences (prefs) are key-value pairs storing user settings.

| Field     | Type   | Description                            |
|-----------|--------|----------------------------------------|
| `key`     | string | Preference name                        |
| `value`   | string | Preference value                       |
| `deleted` | integer | `0` = active, non-zero = deleted      |

Notable preference keys include `UILanguage`, `UIDateLocale`, `UICounts`, `UIAreaFiltering`, `UICollectCompleted`, `UIBProjectNextActionsTopN`, `UIFocusShowCompleted`, `UINextShowCompleted`, `UILaterShowCompleted`, `UISomedayShowCompleted`, `UIWaitingShowCompleted`.

---

## Write Operations (API Methods)

All write operations are JSON objects in the request body array, identified by the `method` field.

### `task.save`

Creates or updates a task. Supply the complete task object — the server merges using field-level timestamps.

**Minimal new task:**
```json
{
  "method": "task.save",
  "id": "019dc8f9-8fe8-7ff6-9345-7389219ee699",
  "type": 0,
  "_type": 1777193325,
  "_type_ms": 1777193325619,
  "parentid": "",
  "_parentid": 1777193291,
  "_parentid_ms": 1777193291752,
  "waitingfor": "",
  "_waitingfor": 1777193291,
  "_waitingfor_ms": 1777193291752,
  "state": 1,
  "_state": 1777193325,
  "_state_ms": 1777193325619,
  "completed": 0,
  "_completed": 1777193291,
  "_completed_ms": 1777193291752,
  "cancelled": 0,
  "_cancelled": 1777193291,
  "_cancelled_ms": 1777193291,
  "seq": 1777193291,
  "_seq": 1777193291,
  "_seq_ms": 1777193291752,
  "seqt": 0,
  "_seqt": 1777193325,
  "_seqt_ms": 1777193325620,
  "seqp": 0,
  "_seqp": 1777193291,
  "_seqp_ms": 1777193291752,
  "name": "My Task",
  "_name": 1777193325,
  "_name_ms": 1777193325619,
  "tags": ",@work,",
  "_tags": 1777193325,
  "_tags_ms": 1777193325620,
  "note": "",
  "_note": 1777193325,
  "_note_ms": 1777193325620,
  "ps": 0,
  "_ps": 1777193325,
  "_ps_ms": 1777193325619,
  "etime": 0,
  "_etime": 1777193325,
  "_etime_ms": 1777193325619,
  "energy": 0,
  "_energy": 1777193325,
  "_energy_ms": 1777193325619,
  "startdate": "",
  "_startdate": 1777193291,
  "_startdate_ms": 1777193291752,
  "duedate": "20260501",
  "_duedate": 1777193325,
  "_duedate_ms": 1777193325619,
  "recurring": "",
  "_recurring": 1777193291,
  "_recurring_ms": 1777193291752,
  "deleted": 0,
  "_deleted": 1777193291,
  "_deleted_ms": 1777193291
}
```

**Timestamp rules:** For any field you are changing, set its `_field` and `_field_ms` to the current time. For fields you are not touching, set them to a low value like `1` (seconds) / `1000` (ms) for new entities, or carry forward the existing timestamps.

### `tag.save`

Creates or updates a tag/context/contact/area.

```json
{
  "method": "tag.save",
  "key": "@home",
  "type": 0,
  "_type": 1777193325,
  "_type_ms": 1777193325619,
  "color": "",
  "_color": 1777193325,
  "_color_ms": 1777193325619,
  "email": "",
  "_email": 1777193325,
  "_email_ms": 1777193325619,
  "meta": "",
  "_meta": 1777193325,
  "_meta_ms": 1777193325619,
  "deleted": 0,
  "_deleted": 1777193325,
  "_deleted_ms": 1777193325619
}
```

To delete a tag, set `deleted` to the current Unix timestamp.

### `pref.save`

Updates a user preference.

```json
{
  "method": "pref.save",
  "key": "UILanguage",
  "value": "en",
  "_value": 1777193325,
  "_value_ms": 1777193325619,
  "deleted": 0,
  "_deleted": 1777193291,
  "_deleted_ms": 1777193291752
}
```

### `fcmtoken.save`

Registers a Firebase Cloud Messaging token for push notifications (mobile clients).

```json
{
  "method": "fcmtoken.save",
  "token": "FCM_DEVICE_TOKEN_HERE"
}
```

---

## Deleting Tasks

Tasks are soft-deleted. To delete a task, update it with `state = 9` (DELETED) and `deleted = current_unix_time`:

```json
{
  "method": "task.save",
  "id": "TASK_ID",
  "state": 9,
  "_state": 1777193325,
  "_state_ms": 1777193325619,
  "deleted": 1777193325,
  "_deleted": 1777193325,
  "_deleted_ms": 1777193325619
}
```

To trash (move to Trash view) instead of permanently delete, use `state = 8` (TRASHED).

To permanently delete from trash (empty trash), set `state = 9` and `deleted = current_unix_time`.

---

## Completing Tasks

Set `completed` to the current Unix timestamp and `state` to `7` (LOGGED) to log a completed task:

```json
{
  "method": "task.save",
  "id": "TASK_ID",
  "state": 7,
  "_state": 1777193325,
  "_state_ms": 1777193325619,
  "completed": 1777193325,
  "_completed": 1777193325,
  "_completed_ms": 1777193325619
}
```

---

## Task Hierarchy

- **Projects** (`type=1`) are top-level task containers. Project tasks have `parentid = ""`.
- **Action tasks** (`type=0`) inside a project set `parentid` to the project's `id`.
- **Reference lists** (`type=2`) are containers for reference items (`type=3`), same parent/child pattern.
- **Area folders** (`type=4`) are organizational groups. Tasks, projects, and lists can be tagged with an area (`type=2` tag) to associate them with an area. Area folders have `type=4`.

---

## Focus (Starred) Tasks

The Focus view shows tasks where `seqt != 0`. To add a task to Focus, set `seqt` to the current Unix timestamp. To remove from Focus, set `seqt = 0`.

---

## Tag Formatting

Tags on tasks are stored as a comma-delimited string with leading and trailing commas:

```
",@work,@home,"         → tags: @work, @home
","                     → no tags
",@work,"               → one tag: @work
```

The `waitingfor` field is a plain string containing a single tag key (the contact name), not comma-wrapped.

---

## Server-Sent Events (SSE)

For real-time sync across devices, the app connects to the SSE endpoint:

```
GET https://sse.nirvanahq.com/api/stream?authtoken=TOKEN
```

When another client writes data, the server pushes a notification through this stream and the client triggers a fresh sync.

---

## Account Management

The account management page is accessible at:

```
https://account.nirvanahq.com/auth?token=TOKEN&go=dashboard&lang=en&source=web
```

The `authtoken` from login is passed directly to grant access without a separate login.

---

## Conflict Resolution

The API uses optimistic last-writer-wins conflict resolution at the field level. Each field has a millisecond-precision timestamp (`_fieldname_ms`). When the server receives an update, it compares each field's timestamp against the stored value and keeps whichever is newer. This means partial updates are safe — you only need to bump timestamps on fields you actually changed.

The client also tracks `clock.driftMs` by comparing `clienttime_ms` sent in the request with `servertime_ms` in the response, and uses this to adjust future timestamps.

---

## App Identifiers

| Client          | `appid`                      |
|-----------------|------------------------------|
| Web (Focus)     | `com.nirvanahq.focus`        |
| iOS             | (varies)                     |
| macOS desktop   | (varies)                     |

---

## Rate Limits & Sync Timing

The app uses exponential backoff for SSE reconnection and a 90-second timeout on sync requests. There is no publicly documented rate limit, but excessive requests may result in temporary throttling.

---

## Quick Reference: Task States by View

| View       | `state` value | Notes                                    |
|------------|---------------|------------------------------------------|
| Inbox      | 0             | Uncategorized items                      |
| Next       | 1             | Active next actions                      |
| Waiting    | 2             | Delegated / waiting for someone          |
| Scheduled  | 3             | Has a future `startdate`                 |
| Someday    | 4             | Someday / maybe                          |
| Later      | 5             | Inactive project or deferred action      |
| Projects   | 6             | Active project (`type=1`)                |
| Logbook    | 7             | Completed tasks                          |
| Trash      | 8             | Soft-deleted, recoverable                |
| Focus      | any non-logged | `seqt != 0` marks as focused           |

---

## Quick Reference: Tag Types

| Tag type integer | Use case                                              |
|------------------|-------------------------------------------------------|
| 0 (CONTEXT)      | Contexts like `@home`, `@computer`, `@errands`        |
| 1 (CONTACT)      | People — used as "waiting for" values                 |
| 2 (AREA)         | Areas of focus — filter the entire interface by area  |
