"""
Nirvana HQ API Client
Provides functions to authenticate and create tasks via the Nirvana HQ API.

Usage:
    python nirvanahq_api.py

Dependencies:
    pip install requests
    pip install pymd5  # or use hashlib (built-in)
"""

import hashlib
import time
import uuid
import requests
import getpass
from typing import Optional

# --- Constants ---
APP_ID = "com.nirvanahq.focus"
APP_VERSION = "3.16.6"
API_BASE = "https://api.nirvanahq.com/api"

HEADERS = {
    "Content-Type": "application/json",
    "Origin": "https://focus.nirvanahq.com",
    "Referer": "https://focus.nirvanahq.com/",
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/147.0.0.0 Safari/537.36"
    ),
    "X-Timestamp-Precision": "ms",
}


# --- Helpers ---

def _now_seconds() -> int:
    return int(time.time())

def _now_ms() -> int:
    return int(time.time() * 1000)

def _uuid7() -> str:
    """Generate a UUID v7-style ID (time-ordered). Falls back to uuid4."""
    return str(uuid.uuid4())

def _md5(text: str) -> str:
    return hashlib.md5(text.encode()).hexdigest()

def _build_url(endpoint: str, authtoken: str, since_ms: Optional[int] = None) -> str:
    """Build an API URL with standard query parameters."""
    ts = _now_ms()
    since_param = f"&since_ms={since_ms}" if since_ms is not None else "&since=0"
    request_id = _uuid7()
    return (
        f"{API_BASE}/{endpoint}?&return=everything"
        f"{since_param}"
        f"&authtoken={authtoken}"
        f"&appid={APP_ID}"
        f"&appversion={APP_VERSION}"
        f"&clienttime_ms={ts}"
        f"&requestid={request_id}"
    )


# --- Core Functions ---

def get_auth_token(username: str, password: str) -> str:
    """
    Authenticate with Nirvana HQ and return an auth token.

    The API expects the password as an MD5 hash, not plaintext.

    Args:
        username: Your Nirvana HQ email address.
        password: Your Nirvana HQ password (plaintext — this function hashes it).

    Returns:
        The auth token string to use in subsequent requests.

    Raises:
        ValueError: If login fails (wrong credentials or API error).
        requests.HTTPError: On unexpected HTTP errors.
    """
    url = f"{API_BASE}/auth/new?&appid={APP_ID}&appversion={APP_VERSION}"

    payload = {
        "gmtoffset": 0,       # UTC offset in hours; adjust if needed
        "u": username,
        "p": _md5(password),  # API requires MD5 of password
    }

    response = requests.post(url, json=payload, headers=HEADERS, timeout=30)
    response.raise_for_status()

    data = response.json()
    result = data.get("results", [{}])[0]

    if "error" in result:
        error = result["error"]
        code = error.get("code")
        msg = error.get("message", "Unknown error")
        if code == 98:
            raise ValueError(f"Incorrect username or password (code {code})")
        raise ValueError(f"Login failed — code {code}: {msg}")

    token = result["auth"]["token"]
    print(f"[+] Authenticated successfully. Token: {token[:8]}...")
    return token


def create_task(
    authtoken: str,
    name: str,
    note: str = "",
    tags: list[str] | None = None,
    duedate: str = "",
    state: int = 1,
) -> dict:
    """
    Create a new task in Nirvana HQ.

    Args:
        authtoken:  Auth token obtained from get_auth_token().
        name:       Task title/name.
        note:       Optional task notes/description.
        tags:       Optional list of tags (e.g. ["@work", "@home"]).
                    Tags are stored as a comma-delimited string like ",@work,@home,".
        duedate:    Optional due date as a string in YYYYMMDD format (e.g. "20260501").
                    Pass an empty string for no due date.
        state:      Task state integer. Common values:
                      0 = Inbox
                      1 = Next (default)
                      6 = Someday

    Returns:
        The full API response dict. The created task is in:
            response["results"][0]["changed"]["tasks"][0]

    Raises:
        requests.HTTPError: On HTTP errors.
        ValueError: If the API returns an error.
    """
    ts = _now_ms()
    ts_sec = _now_seconds()
    task_id = _uuid7()

    # Build the comma-delimited tags string Nirvana expects: ",tag1,tag2,"
    tag_string = ""
    if tags:
        cleaned = [t.strip().lstrip(",") for t in tags if t.strip()]
        tag_string = "," + ",".join(cleaned) + "," if cleaned else ""

    task = {
        "method": "task.save",
        "id": task_id,
        "type": 0,
        "_type": ts_sec,
        "_type_ms": ts,
        "parentid": "",
        "_parentid": ts_sec,
        "_parentid_ms": ts,
        "waitingfor": "",
        "_waitingfor": ts_sec,
        "_waitingfor_ms": ts,
        "state": state,
        "_state": ts_sec,
        "_state_ms": ts,
        "completed": 0,
        "_completed": ts_sec,
        "_completed_ms": ts,
        "cancelled": 0,
        "_cancelled": ts_sec,
        "_cancelled_ms": ts,
        "seq": ts_sec,
        "_seq": ts_sec,
        "_seq_ms": ts,
        "seqt": 0,
        "_seqt": ts_sec,
        "_seqt_ms": ts,
        "seqp": 0,
        "_seqp": ts_sec,
        "_seqp_ms": ts,
        "name": name,
        "_name": ts_sec,
        "_name_ms": ts,
        "tags": tag_string,
        "_tags": ts_sec,
        "_tags_ms": ts,
        "note": note,
        "_note": ts_sec,
        "_note_ms": ts,
        "ps": 0,
        "_ps": ts_sec,
        "_ps_ms": ts,
        "etime": 0,
        "_etime": ts_sec,
        "_etime_ms": ts,
        "energy": 0,
        "_energy": ts_sec,
        "_energy_ms": ts,
        "startdate": "",
        "_startdate": ts_sec,
        "_startdate_ms": ts,
        "duedate": duedate,
        "_duedate": ts_sec,
        "_duedate_ms": ts,
        "recurring": "",
        "_recurring": ts_sec,
        "_recurring_ms": ts,
        "deleted": 0,
        "_deleted": ts_sec,
        "_deleted_ms": ts,
    }

    url = _build_url("everything", authtoken, since_ms=0)
    response = requests.post(url, json=[task], headers=HEADERS, timeout=30)
    response.raise_for_status()

    data = response.json()
    results = data.get("results", [{}])

    # Check for errors
    for result in results:
        if "error" in result:
            error = result["error"]
            raise ValueError(f"Task creation failed — {error.get('message', error)}")

    print(f"[+] Task created: '{name}' (id: {task_id})")
    return data


# --- Example usage ---

if __name__ == "__main__":
    # Prompt for credentials
    USERNAME = input("Nirvana HQ Email: ")
    PASSWORD = getpass.getpass("Nirvana HQ Password: ")

    # Step 1: Log in and get auth token
    token = get_auth_token(USERNAME, PASSWORD)

    # Step 2: Create a task using the token
    result = create_task(
        authtoken=token,
        name="HELLO WORLD - My new task",
        note="Created via Python script",
        tags=["@work"],
        duedate="20260501",   # YYYYMMDD format, or "" for no due date
        state=1,              # 1 = Next
    )

    # The created/updated tasks are in the response
    changed = result.get("results", [{}])[0].get("changed", {})
    tasks = changed.get("tasks", [])
    if tasks:
        t = tasks[0]
        print(f"\nCreated task details:")
        print(f"  ID:      {t.get('id')}")
        print(f"  Name:    {t.get('name')}")
        print(f"  Note:    {t.get('note')}")
        print(f"  Tags:    {t.get('tags')}")
        print(f"  Due:     {t.get('duedate')}")
        print(f"  State:   {t.get('state')}")
