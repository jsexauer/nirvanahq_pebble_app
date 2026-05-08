# NirvanaHQ Pebble App

This is a Pebble watch application designed to interface with the [NirvanaHQ](https://www.nirvanahq.com/) task management service. This document serves as a guide for AI assistants and developers to understand the project architecture, dependencies, and environment setup.

## Project Structure and Architecture

The project consists of three main components:

### 1. Native C App (Watch App)
- **Location:** `src/pebble/src/c/pebble.c`
- **Purpose:** Handles the native UI, menus, and Bluetooth communication on the Pebble watch.
- **Key Features:**
  - **Main Menu:** Offers "New Task" and "View Tasks".
  - **Task Viewing:** Displays tasks fetched from the Inbox.
  - **Voice Dictation:** Integrates Pebble's native `DictationSession` API for capturing voice input to create new tasks.
  - **Success Notifications:** Uses timers to show transient success screens.

### 2. PebbleKit JS (Phone Companion App)
- **Location:** `src/pebble/src/pkjs/`
- **Purpose:** Runs on the paired smartphone, handles HTTP requests to the NirvanaHQ API, and bridges data to the watch via `AppMessage`.
- **Key Files:**
  - `index.js`: Core logic for authenticating and creating/fetching tasks.
  - `md5.js`: MD5 hashing utility used for NirvanaHQ authentication.
  - `config.js`: Configuration for Pebble Clay (settings page).
  - `dev_config.json`: A local (git-ignored) configuration file used during emulator development. It stores mock credentials and `mock_dictation` strings to bypass the UI during testing.

### 3. Python API Client (Reference)
- **Location:** `src/py/nirvanahq_client/`
- **Purpose:** A fully functional Python client (`nirvana_api.py`) used for testing, reverse-engineering, and documenting the NirvanaHQ API payloads. Contains the data models for tasks and handles authentication/UUID generation.

### 4. API Documentation
- **Location:** `research/nirvanahq_api_docs.md`
- **Purpose:** Detailed documentation and insights on the NirvanaHQ API, including endpoints, payloads, authentication mechanisms, and expected data structures. Use this file as the primary reference when expanding or modifying API interactions.

## Development Environment & Build Scripts

The project is built inside a WSL (Windows Subsystem for Linux) environment, as the Pebble SDK relies on Linux tooling.

- **`package.json`**: Contains Pebble app metadata, capabilities (e.g., `voice`), and `messageKeys`. Whenever you add a new `AppKey` to `messageKeys`, you **must** rebuild the project.
- **`run_emulator.ps1` & `run_emulator.sh`**: Helper scripts to compile the project and launch the emulator (e.g., `emery`). 
  - *Note:* Both scripts automatically run `pebble clean` before `pebble build` to avoid caching issues with generated C headers (`message_keys.auto.h`).

## Communication (AppMessage)

The C and JS layers communicate using Pebble's `AppMessage` dictionary framework. Key message keys defined in `package.json` include:
- `AppKeyRequestTasks` & `AppKeyReady`: Initialization signals.
- `AppKeyTaskName`, `AppKeyTaskState`, etc.: Used to transmit task lists.
- `AppKeyInitiateTaskCreation`, `AppKeyStartDictation`, `AppKeyCreateTask`, `AppKeyTaskCreatedSuccess`: The lifecycle of creating a task via dictation.

## Quirks & Considerations
- **Dictation in Emulator:** The QEMU emulator does not fully support live dictation. To mitigate this, `index.js` checks the `watchInfo.model` string. If it detects `qemu`, it reads `dev_config.json` and skips telling the watch to invoke the dictation UI, directly passing the `mock_dictation` string to the Nirvana API.
- **UUIDs and Timestamps:** NirvanaHQ's API is highly reliant on milliseconds/seconds timestamps for conflict resolution and UUIDs (v4/v7) for entities. Ensure payload timestamps accurately match the `_ms` appended fields.
