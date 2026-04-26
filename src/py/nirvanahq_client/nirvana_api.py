"""
Nirvana HQ API Client

Provides an API class to interact with Nirvana HQ, encapsulating authentication
and task management features (list, create, edit, delete).
"""

import hashlib
import time
import uuid
import requests
from dataclasses import dataclass, field
from typing import Optional, List, Dict, Any

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

# --- Task States ---
class TaskState:
    INBOX = 0
    NEXT = 1
    WAITING = 2
    SCHEDULED = 3
    SOMEDAY = 4
    LATER = 5
    ACTIVEPROJECT = 6
    LOGGED = 7
    TRASHED = 8
    DELETED = 9
    RECURRING = 10


def _now_seconds() -> int:
    return int(time.time())

def _now_ms() -> int:
    return int(time.time() * 1000)

def _uuid7() -> str:
    """Generate a UUID v7-style ID (time-ordered). Falls back to uuid4."""
    return str(uuid.uuid4())

def _md5(text: str) -> str:
    return hashlib.md5(text.encode()).hexdigest()

@dataclass
class Task:
    id: str
    name: str
    state: int = TaskState.INBOX
    type: int = 0
    note: str = ""
    tags: List[str] = field(default_factory=list)
    duedate: str = ""
    deleted: int = 0
    completed: int = 0
    parentid: str = ""
    _raw_payload: dict = field(default_factory=dict)
    
    @classmethod
    def from_api_payload(cls, payload: dict) -> "Task":
        tags_str = payload.get("tags", "")
        tags_list = [t for t in tags_str.split(",") if t]
        
        return cls(
            id=payload.get("id", ""),
            name=payload.get("name", ""),
            state=payload.get("state", TaskState.INBOX),
            type=payload.get("type", 0),
            note=payload.get("note", ""),
            tags=tags_list,
            duedate=payload.get("duedate", ""),
            deleted=payload.get("deleted", 0),
            completed=payload.get("completed", 0),
            parentid=payload.get("parentid", ""),
            _raw_payload=payload
        )

    def to_api_payload(self) -> dict:
        ts = _now_ms()
        ts_sec = _now_seconds()
        
        if not self._raw_payload:
            # First time creation, start from a complete skeleton
            payload = {
                "method": "task.save",
                "id": self.id,
                "type": self.type,
                "_type": ts_sec,
                "_type_ms": ts,
                "parentid": self.parentid,
                "_parentid": ts_sec,
                "_parentid_ms": ts,
                "waitingfor": "",
                "_waitingfor": ts_sec,
                "_waitingfor_ms": ts,
                "state": self.state,
                "_state": ts_sec,
                "_state_ms": ts,
                "completed": self.completed,
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
                "name": self.name,
                "_name": ts_sec,
                "_name_ms": ts,
                "tags": "",
                "_tags": ts_sec,
                "_tags_ms": ts,
                "note": self.note,
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
                "duedate": self.duedate,
                "_duedate": ts_sec,
                "_duedate_ms": ts,
                "recurring": "",
                "_recurring": ts_sec,
                "_recurring_ms": ts,
                "deleted": self.deleted,
                "_deleted": ts_sec,
                "_deleted_ms": ts,
            }
        else:
            # We are editing an existing task, just overwrite and update timestamps
            payload = self._raw_payload.copy()
            payload["method"] = "task.save"
            payload["id"] = self.id
            
        def update_field(key, val):
            if not self._raw_payload or payload.get(key) != val:
                payload[key] = val
                payload[f"_{key}"] = ts_sec
                payload[f"_{key}_ms"] = ts
                
        update_field("name", self.name)
        update_field("state", self.state)
        update_field("type", self.type)
        update_field("note", self.note)
        
        tag_string = ""
        if self.tags:
            cleaned = [t.strip().lstrip(",") for t in self.tags if t.strip()]
            tag_string = "," + ",".join(cleaned) + "," if cleaned else ""
        update_field("tags", tag_string)
        
        update_field("duedate", self.duedate)
        update_field("deleted", self.deleted)
        update_field("completed", self.completed)
        update_field("parentid", self.parentid)
        
        return payload

class NirvanaApi:
    def __init__(self):
        self.authtoken: Optional[str] = None
        
    def _build_url(self, endpoint: str, since_ms: Optional[int] = None) -> str:
        if not self.authtoken and endpoint != "auth/new":
            raise ValueError("Not authenticated. Please call login() first.")
            
        ts = _now_ms()
        since_param = f"&since_ms={since_ms}" if since_ms is not None else "&since=0"
        request_id = _uuid7()
        
        url = (
            f"{API_BASE}/{endpoint}?&appid={APP_ID}&appversion={APP_VERSION}"
        )
        if endpoint != "auth/new":
            url += (
                f"&return=everything"
                f"{since_param}"
                f"&authtoken={self.authtoken}"
                f"&clienttime_ms={ts}"
                f"&requestid={request_id}"
            )
        return url

    def login(self, username: str, password: str) -> str:
        """
        Authenticate with Nirvana HQ and store the auth token.
        """
        url = self._build_url("auth/new")
        payload = {
            "gmtoffset": 0,
            "u": username,
            "p": _md5(password),
        }
        
        response = requests.post(url, json=payload, headers=HEADERS, timeout=30)
        response.raise_for_status()
        
        data = response.json()
        result = data.get("results", [{}])[0]
        
        if "error" in result:
            error = result["error"]
            code = error.get("code")
            msg = error.get("message", "Unknown error")
            raise ValueError(f"Login failed (code {code}): {msg}")
            
        self.authtoken = result["auth"]["token"]
        return self.authtoken

    def list_tasks(self, area: Optional[str] = None, state: Optional[int] = None) -> List[Task]:
        """
        List all tasks. Optionally filter by area tag (e.g. '@work') and/or task state.
        """
        url = self._build_url("everything", since_ms=0)
        response = requests.post(url, json=[], headers=HEADERS, timeout=30)
        response.raise_for_status()
        
        data = response.json()
        results = data.get("results", [])
        
        tasks = []
        for entity in results:
            if "task" in entity:
                task_payload = entity["task"]
                # Skip permanently deleted
                if task_payload.get("deleted", 0) != 0 and task_payload.get("state") == TaskState.DELETED:
                    continue
                    
                task = Task.from_api_payload(task_payload)
                
                # Filter by state
                if state is not None and task.state != state:
                    continue
                    
                # Filter by area (assuming area is stored as a tag)
                if area is not None and area not in task.tags:
                    continue
                    
                tasks.append(task)
                
        return tasks

    def create_task(self, name: str, note: str = "", duedate: str = "", 
                    area: Optional[str] = None, contexts: Optional[List[str]] = None, 
                    state: int = TaskState.NEXT) -> Task:
        """
        Create a new task.
        """
        tags = []
        if contexts:
            tags.extend(contexts)
        if area:
            tags.append(area)
            
        task = Task(
            id=_uuid7(),
            name=name,
            note=note,
            duedate=duedate,
            tags=tags,
            state=state,
            type=0
        )
        
        payload = task.to_api_payload()
        url = self._build_url("everything", since_ms=0)
        
        response = requests.post(url, json=[payload], headers=HEADERS, timeout=30)
        response.raise_for_status()
        
        data = response.json()
        self._check_for_errors(data)
        
        # We can extract the actual task from response to return a Task object with fully set attributes
        # but for simplicity, we update the object locally with the response payload
        results = data.get("results", [{}])
        # Depending on Nirvana API, response with `changed.tasks` usually contains the newly assigned task state
        for result in results:
            changed_tasks = result.get("changed", {}).get("task", [])
            if not changed_tasks:
                changed_tasks = result.get("changed", {}).get("tasks", [])
            for t in changed_tasks:
                if t.get("id") == task.id:
                    return Task.from_api_payload(t)
        
        # Fallback to the requested task object
        return task

    def edit_task(self, task: Task) -> Task:
        """
        Submit edits for a given task.
        Modifies the backend task entity with the fields present in the Task object.
        """
        payload = task.to_api_payload()
        url = self._build_url("everything", since_ms=0)
        
        response = requests.post(url, json=[payload], headers=HEADERS, timeout=30)
        response.raise_for_status()
        
        data = response.json()
        self._check_for_errors(data)
        return task

    def delete_task(self, task: Task) -> Task:
        """
        Soft-delete a task by setting state to DELETED (9) and updating deleted timestamp.
        """
        task.state = TaskState.DELETED
        task.deleted = _now_seconds()
        
        return self.edit_task(task)

    def _check_for_errors(self, data: dict):
        results = data.get("results", [{}])
        for result in results:
            if "error" in result:
                error = result["error"]
                raise ValueError(f"API operation failed: {error.get('message', error)}")


if __name__ == "__main__":
    import getpass
    
    # Prompt for credentials
    print("--- Nirvana API Test ---")
    USERNAME = input("Nirvana HQ Email: ")
    PASSWORD = getpass.getpass("Nirvana HQ Password: ")

    api = NirvanaApi()
    print("\n[+] Logging in...")
    try:
        api.login(USERNAME, PASSWORD)
        print("[+] Logged in successfully!")
    except Exception as e:
        print(f"[-] Login failed: {e}")
        exit(1)

    print("\n[+] Fetching tasks...")
    tasks = api.list_tasks()
    print(f"[+] Found {len(tasks)} tasks.")
    for i, t in enumerate(tasks[:5]):
        print(f"    {i+1}. {t.name} (state={t.state})")
    if len(tasks) > 5:
        print("    ...")

    print("\n[+] Creating a test task...")
    new_task = api.create_task(
        name="HELLO WORLD - Automated Test Task",
        note="This was created by the python API test script.",
        contexts=["@home"]
    )
    print(f"[+] Created task: '{new_task.name}' with ID: {new_task.id}")

    input("\n[P] Press Enter to delete this test task...")

    print(f"\n[+] Deleting task {new_task.id}...")
    api.delete_task(new_task)
    print("[+] Task deleted successfully!")
