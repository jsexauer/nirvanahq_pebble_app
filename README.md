# NirvanaHQ Pebble App

This is a Pebble watch application designed to interface with the [NirvanaHQ](https://www.nirvanahq.com/) task management service. It allows you to quickly view your Inbox tasks and add new tasks directly from your wrist using Pebble's native voice dictation.

## Features
- **View Inbox:** Scroll through the tasks currently sitting in your NirvanaHQ Inbox.
- **Voice Dictation:** Create new tasks entirely hands-free using Pebble's native voice dictation API.
- **Dynamic Updates:** Successfully added tasks display a brief confirmation screen on the watch and immediately refresh your Inbox list.

## Project Structure

The project consists of three main components:

### 1. Native C App (Watch App)
- **Location:** `src/pebble/src/c/pebble.c`
- **Purpose:** Handles the native UI, menus, Bluetooth communication, and dictation sessions on the Pebble watch.

### 2. PebbleKit JS (Phone Companion App)
- **Location:** `src/pebble/src/pkjs/`
- **Purpose:** Runs on the paired smartphone, handling HTTP requests to the NirvanaHQ API and bridging data to the watch via `AppMessage`.

### 3. Python API Client (Reference)
- **Location:** `src/py/nirvanahq_client/`
- **Purpose:** A fully functional Python client (`nirvana_api.py`) used for testing and reverse-engineering the NirvanaHQ API.

### 4. API Documentation
- **Location:** `research/nirvanahq_api_docs.md`
- **Purpose:** Detailed documentation and insights on the NirvanaHQ API, including endpoints, payloads, and authentication mechanisms.

## Development & Building

The project is built inside a WSL (Windows Subsystem for Linux) environment, as the Pebble SDK relies on Linux tooling.

**Prerequisites:** You must have the Pebble SDK installed. 

**Build Scripts:**
Helper scripts are provided to compile the project and launch the emulator:
- `.\src\pebble\run_emulator.ps1` (PowerShell)
- `./src/pebble/run_emulator.sh` (Bash)

*Note: Both scripts automatically run `pebble clean` before `pebble build` to ensure header files regenerate properly.*

## Emulation Quirks

**Dictation in Emulator:** The QEMU emulator does not fully support live dictation. 
To test task creation in the emulator, the app looks for a `mock_dictation` property in `src/pebble/src/pkjs/dev_config.json`. If it finds it while running under `qemu`, it will bypass the dictation UI entirely and automatically create a task using the mock string.

*(Note: `dev_config.json` is git-ignored to prevent leaking credentials. You can copy `src/pebble/src/pkjs/dev_config.example.json` to `dev_config.json` to get started.)*

## Note for AI Assistants
For a deeper technical dive and rules to follow when contributing to this repository, please review `AGENTS.md`.
